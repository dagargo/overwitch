/*
 *   main-record.c
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
 *
 *   This file is part of Overwitch.
 *
 *   Overwitch is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Overwitch is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Overwitch. If not, see <http://www.gnu.org/licenses/>.
 */

#include <signal.h>
#include <sndfile.h>
#include <unistd.h>
#include <time.h>
#include "../config.h"
#include "utils.h"
#include "common.h"

#define TRACK_BUF_KB 256
#define MAX_FILENAME_LEN 128
#define MAX_TIME_LEN 32

static struct ow_context context;
static struct ow_engine *engine;
static SF_INFO sfinfo;
static SNDFILE *sf;
static const struct ow_device_desc *desc;
static const char *track_mask;
static size_t track_buf_size_kb = TRACK_BUF_KB;
static float max[OB_MAX_TRACKS];
static float min[OB_MAX_TRACKS];
static char filename[MAX_FILENAME_LEN];

typedef enum
{
  END = -1,
  EMPTY,
  READY
} buffer_status_t;

static struct
{
  char *mem;
  char *disk;
  size_t len;
  pthread_t pthread;
  size_t disk_samples;
  size_t disk_frames;
  pthread_spinlock_t lock;
  buffer_status_t status;
  int outputs;
  int outputs_mask_len;
} buffer;

static struct option options[] = {
  {"use-device-number", 1, NULL, 'n'},
  {"use-device", 1, NULL, 'd'},
  {"bus-device-address", 1, NULL, 'a'},
  {"track-mask", 1, NULL, 'm'},
  {"track-buffer-size-kilobytes", 1, NULL, 's'},
  {"blocks-per-transfer", 1, NULL, 'b'},
  {"usb-transfer-timeout", 1, NULL, 't'},
  {"list-devices", 0, NULL, 'l'},
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
};

static void
print_status ()
{
  fprintf (stderr, "%lu frames written\n", sfinfo.frames);
}

static size_t
buffer_dummy_rw_space (void *data)
{
  return OW_DEFAULT_BLOCKS * OB_FRAMES_PER_BLOCK * OB_MAX_TRACKS *
    OW_BYTES_PER_SAMPLE;
}

static buffer_status_t
get_buffer_status ()
{
  buffer_status_t status;

  pthread_spin_lock (&buffer.lock);
  status = buffer.status;
  pthread_spin_unlock (&buffer.lock);

  return status;
}

static void *
dump_buffer (void *data)
{
  buffer_status_t status = get_buffer_status ();
  while (status >= EMPTY)
    {
      if (status == READY)
	{
	  pthread_spin_lock (&buffer.lock);
	  debug_print (2, "Writing %ld frames to disk...",
		       buffer.disk_frames);
	  sf_write_float (sf, (float *) buffer.disk, buffer.disk_samples);
	  debug_print (2, "Done");

	  buffer.status = EMPTY;
	  pthread_spin_unlock (&buffer.lock);
	}

      usleep (100);

      status = get_buffer_status ();
    }
  return NULL;
}

static size_t
buffer_write (void *data, const char *buf, size_t size)
{
  static int print_control = 0;
  static size_t pos = 0;
  size_t new_pos;
  void *dst;
  size_t frames = size / (desc->outputs * OW_BYTES_PER_SAMPLE);

  debug_print (2, "Writing %ld bytes (%ld frames) to buffer...", size,
	       frames);
  new_pos = pos + frames * buffer.outputs * OW_BYTES_PER_SAMPLE;
  if (new_pos >= buffer.len)
    {
      pthread_spin_lock (&buffer.lock);
      buffer.status = READY;
      buffer.disk_samples = pos / OW_BYTES_PER_SAMPLE;
      buffer.disk_frames = buffer.disk_samples / buffer.outputs;
      memcpy (buffer.disk, buffer.mem,
	      buffer.disk_frames * buffer.outputs * OW_BYTES_PER_SAMPLE);
      pthread_spin_unlock (&buffer.lock);
      sfinfo.frames += buffer.disk_frames;
      pos = 0;
    }

  dst = &buffer.mem[pos];
  for (int i = 0; i < frames; i++)
    {
      for (int j = 0; j < desc->outputs; j++)
	{
	  if (!track_mask
	      || (j < buffer.outputs_mask_len && (track_mask[j] != '0')))
	    {
	      memcpy (dst, buf, OW_BYTES_PER_SAMPLE);
	      dst += OW_BYTES_PER_SAMPLE;
	      pos += OW_BYTES_PER_SAMPLE;
	      float x = *((float *) buf);
	      if (x >= 0.0)
		{
		  if (x > max[j])
		    {
		      max[j] = x;
		    }
		}
	      else
		{
		  if (x < min[j])
		    {
		      min[j] = x;
		    }
		}
	    }
	  buf += OW_BYTES_PER_SAMPLE;
	}
    }

  if (debug_level)
    {
      print_control += frames;
      if (print_control >= OB_SAMPLE_RATE)
	{
	  print_control -= OB_SAMPLE_RATE;
	  print_status ();
	}
    }

  return size;
}

static void
signal_handler (int signo)
{
  print_status ();
  if (debug_level)
    {
      for (int i = 0; i < desc->outputs; i++)
	{
	  if (!track_mask
	      || (i < buffer.outputs_mask_len && (track_mask[i] != '0')))
	    {
	      fprintf (stderr, "%s: max: %f; min: %f\n",
		       desc->output_tracks[i].name, max[i], min[i]);
	    }
	}
    }
  if (signo == SIGHUP || signo == SIGINT || signo == SIGTERM
      || signo == SIGTSTP)
    {
      ow_engine_stop (engine);
      fprintf (stderr, "%s file created\n", filename);
    }
}

static int
run_record (int device_num, const char *device_name, uint8_t bus,
	    uint8_t address, unsigned int blocks_per_transfer,
	    unsigned int xfr_timeout)
{
  char curr_time_string[MAX_TIME_LEN];
  time_t curr_time;
  struct tm tm;
  ow_err_t err;
  struct ow_usb_device *device;

  if (ow_get_usb_device_from_device_attrs (device_num, device_name, bus,
					   address, &device))
    {
      return OW_GENERIC_ERROR;
    }

  err = ow_engine_init_from_bus_address (&engine, device->bus,
					 device->address, blocks_per_transfer,
					 xfr_timeout);
  free (device);
  if (err)
    {
      goto end;
    }

  desc = ow_engine_get_device_desc (engine);

  if (track_mask)
    {
      buffer.outputs = 0;
      const char *c = track_mask;
      for (int i = 0; i < strlen (track_mask); i++, c++)
	{
	  if (*c != '0')
	    {
	      buffer.outputs++;
	    }
	}
    }
  else
    {
      buffer.outputs = desc->outputs;
    }

  if (buffer.outputs == 0)
    {
      err = OW_GENERIC_ERROR;
      goto cleanup_engine;
    }

  sfinfo.frames = 0;
  sfinfo.samplerate = OB_SAMPLE_RATE;
  sfinfo.channels = buffer.outputs;
  sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

  curr_time = time (NULL);
  localtime_r (&curr_time, &tm);
  strftime (curr_time_string, MAX_TIME_LEN, "%FT%T", &tm);

  snprintf (filename, MAX_FILENAME_LEN, "%s_%s.wav", desc->name,
	    curr_time_string);

  debug_print (1, "Creating sample (%d channels)...", buffer.outputs);
  sf = sf_open (filename, SFM_WRITE, &sfinfo);

  context.dll = NULL;
  context.write_space = buffer_dummy_rw_space;
  context.read_space = buffer_dummy_rw_space;
  context.write = buffer_write;
  context.o2h_audio = sf;
  context.options = OW_ENGINE_OPTION_O2H_AUDIO;

  err = ow_engine_start (engine, &context);
  if (err)
    {
      goto cleanup;
    }

  buffer.len = track_buf_size_kb * 1000 * buffer.outputs;
  buffer.mem = malloc (buffer.len * OW_BYTES_PER_SAMPLE);
  buffer.disk = malloc (buffer.len * OW_BYTES_PER_SAMPLE);
  buffer.outputs_mask_len = track_mask ? strlen (track_mask) : 0;

  for (int i = 0; i < desc->outputs; i++)
    {
      max[i] = 0.0f;
      min[i] = 0.0f;
    }

  buffer.status = READY;
  pthread_spin_init (&buffer.lock, PTHREAD_PROCESS_SHARED);
  if (pthread_create (&buffer.pthread, NULL, dump_buffer, NULL))
    {
      error_print ("Could not start recording thread");
      goto cleanup;
    }
  ow_set_thread_rt_priority (buffer.pthread, OW_DEFAULT_RT_PROPERTY);

  ow_engine_wait (engine);

  pthread_spin_lock (&buffer.lock);
  buffer.status = END;
  pthread_spin_unlock (&buffer.lock);
  pthread_join (buffer.pthread, NULL);

cleanup:
  pthread_spin_destroy (&buffer.lock);
  free (buffer.mem);
  free (buffer.disk);
  sf_close (sf);
cleanup_engine:
  ow_engine_destroy (engine);
end:
  if (err)
    {
      error_print ("%s", ow_get_err_str (err));
    }
  return err;
}

int
main (int argc, char *argv[])
{
  int opt;
  int lflg = 0, vflg = 0, errflg = 0;
  int nflg = 0, dflg = 0, aflg = 0, mflg = 0, sflg = 0, bflg = 0, tflg = 0;
  char *endstr;
  const char *device_name = NULL;
  uint8_t bus = 0, address = 0;
  int long_index = 0;
  ow_err_t ow_err;
  struct sigaction action;
  int device_num = -1;
  unsigned int blocks_per_transfer = OW_DEFAULT_BLOCKS;
  unsigned int xfr_timeout = OW_DEFAULT_XFR_TIMEOUT;

  action.sa_handler = signal_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGUSR1, &action, NULL);
  sigaction (SIGTSTP, &action, NULL);

  while ((opt = getopt_long (argc, argv, "n:d:a:m:s:b:t:lvh",
			     options, &long_index)) != -1)
    {
      switch (opt)
	{
	case 'n':
	  device_num = (int) strtol (optarg, &endstr, 10);
	  nflg++;
	  break;
	case 'd':
	  device_name = optarg;
	  dflg++;
	  break;
	case 'a':
	  get_bus_address_from_str (optarg, &bus, &address);
	  aflg++;
	  break;
	case 'm':
	  track_mask = optarg;
	  mflg++;
	  break;
	case 's':
	  track_buf_size_kb = atoi (optarg);
	  sflg++;
	  break;
	case 'b':
	  blocks_per_transfer = get_ow_blocks_per_transfer_argument (optarg);
	  bflg++;
	  break;
	case 't':
	  xfr_timeout = get_ow_xfr_timeout_argument (optarg);
	  tflg++;
	  break;
	case 'l':
	  lflg++;
	  break;
	case 'v':
	  vflg++;
	  break;
	case 'h':
	  print_help (argv[0], PACKAGE_STRING, options, NULL);
	  exit (EXIT_SUCCESS);
	case '?':
	  errflg++;
	}
    }

  if (errflg > 0)
    {
      print_help (argv[0], PACKAGE_STRING, options, NULL);
      exit (EXIT_FAILURE);
    }

  if (vflg)
    {
      debug_level = vflg;
    }

  if (lflg)
    {
      ow_err = print_devices ();
      if (ow_err)
	{
	  fprintf (stderr, "USB error: %s\n", ow_get_err_str (ow_err));
	  exit (EXIT_FAILURE);
	}
      exit (EXIT_SUCCESS);
    }

  if (mflg > 1)
    {
      fprintf (stderr, "Undetermined track mask\n");
      exit (EXIT_FAILURE);
    }

  if (bflg > 1)
    {
      fprintf (stderr, "Undetermined blocks\n");
      exit (EXIT_FAILURE);
    }

  if (tflg > 1)
    {
      fprintf (stderr, "Undetermined timeout\n");
      exit (EXIT_FAILURE);
    }

  if (nflg + dflg + aflg == 1)
    {
      return run_record (device_num, device_name, bus, address,
			 blocks_per_transfer, xfr_timeout);
    }
  else
    {
      fprintf (stderr, "Device not provided properly\n");
      exit (EXIT_FAILURE);
    }

  return EXIT_SUCCESS;
}
