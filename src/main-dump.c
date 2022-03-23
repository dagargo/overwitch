/*
 *   main-dump.c
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
#include "../config.h"
#include "overwitch.h"
#include "utils.h"
#include "common.h"

#define DEFAULT_BLOCKS 24
#define DISK_BUF_LEN_MULT (256 * 1024)
#define ALL_TRACKS_MASK "111111111111"	//OB_MAX_TRACKS == 12

static struct ow_context context;
static struct ow_engine *engine;
static SF_INFO sfinfo;
static SNDFILE *sf;
static const struct ow_device_desc *desc;
static const char *track_mask = ALL_TRACKS_MASK;
static int outputs;

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
} buffer;

static struct option options[] = {
  {"use-device-number", 1, NULL, 'n'},
  {"use-device", 1, NULL, 'd'},
  {"list-devices", 0, NULL, 'l'},
  {"track-mask", 1, NULL, 'm'},
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
};

static void
print_status ()
{
  printf ("%lu frames written\n", sfinfo.frames);
}

static size_t
buffer_write_space (void *data)
{
  return DEFAULT_BLOCKS * OB_FRAMES_PER_BLOCK * OB_MAX_TRACKS *
    OB_BYTES_PER_SAMPLE;
}

buffer_status_t
get_buffer_status ()
{
  buffer_status_t status;

  pthread_spin_lock (&buffer.lock);
  status = buffer.status;
  pthread_spin_unlock (&buffer.lock);

  return status;
}

void *
dump_buffer (void *data)
{
  buffer_status_t status = get_buffer_status ();
  while (buffer.status >= EMPTY)
    {
      pthread_spin_lock (&buffer.lock);
      if (status == READY)
	{
	  debug_print (2, "Writing %ld frames to disk...\n",
		       buffer.disk_frames);
	  sf_write_float (sf, (float *) buffer.disk, buffer.disk_samples);
	  debug_print (2, "Done\n");

	  buffer.status = EMPTY;
	}
      pthread_spin_unlock (&buffer.lock);

      usleep (1000);

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
  size_t frames = size / (desc->outputs * OB_BYTES_PER_SAMPLE);

  debug_print (2, "Writing %ld bytes (%ld frames) to buffer...\n", size,
	       frames);
  new_pos = pos + frames * outputs * OB_BYTES_PER_SAMPLE;
  if (new_pos >= buffer.len)
    {
      pthread_spin_lock (&buffer.lock);
      buffer.status = READY;
      buffer.disk_samples = pos / OB_BYTES_PER_SAMPLE;
      buffer.disk_frames = buffer.disk_samples / outputs;
      memcpy (buffer.disk, buffer.mem,
	      buffer.disk_frames * outputs * OB_BYTES_PER_SAMPLE);
      pthread_spin_unlock (&buffer.lock);
      sfinfo.frames += buffer.disk_frames;
      pos = 0;
      new_pos = size;
    }

  void *dst = &buffer.mem[pos];
  for (int i = 0; i < frames; i++)
    {
      const char *c = track_mask;
      for (int j = 0; j < desc->outputs; j++, c++, buf += OB_BYTES_PER_SAMPLE)
	{
	  if (*c != '0')
	    {
	      memcpy (dst, buf, OB_BYTES_PER_SAMPLE);
	      dst += OB_BYTES_PER_SAMPLE;
	      pos += OB_BYTES_PER_SAMPLE;
	    }
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
  if (signo == SIGHUP || signo == SIGINT || signo == SIGTERM)
    {
      ow_engine_stop (engine);
    }
}

static int
run_dump (int device_num, const char *device_name)
{
  ow_err_t err;
  struct ow_usb_device *device;

  if (ow_get_usb_device_from_device_attrs (device_num, device_name, &device))
    {
      return OW_GENERIC_ERROR;
    }

  err =
    ow_engine_init (&engine, device->bus, device->address, DEFAULT_BLOCKS);
  free (device);
  if (err)
    {
      goto end;
    }

  desc = ow_engine_get_device_desc (engine);
  outputs = 0;
  const char *c = track_mask;
  for (int i = 0; i < desc->outputs; i++, c++)
    {
      if (*c != '0')
	{
	  outputs++;
	}
    }

  sfinfo.frames = 0;
  sfinfo.samplerate = OB_SAMPLE_RATE;
  sfinfo.channels = outputs;
  sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

  debug_print (1, "Creating sample (%d channels)...\n", outputs);
  sf = sf_open ("dump.wav", SFM_WRITE, &sfinfo);

  context.write_space = buffer_write_space;
  context.write = buffer_write;
  context.o2p_audio = sf;
  context.options = OW_ENGINE_OPTION_O2P_AUDIO;

  err = ow_engine_activate (engine, &context);
  if (err)
    {
      goto cleanup;
    }

  buffer.len = DISK_BUF_LEN_MULT * outputs;
  buffer.mem = malloc (buffer.len * OB_BYTES_PER_SAMPLE);
  buffer.disk = malloc (buffer.len * OB_BYTES_PER_SAMPLE);

  buffer.status = READY;
  pthread_spin_init (&buffer.lock, PTHREAD_PROCESS_SHARED);
  if (pthread_create (&buffer.pthread, NULL, dump_buffer, NULL))
    {
      error_print ("Could not start dump thread\n");
      goto cleanup;
    }
  ow_set_thread_rt_priority (&buffer.pthread, OW_DEFAULT_RT_PROPERTY);

  ow_engine_wait (engine);

  pthread_spin_lock (&buffer.lock);
  buffer.status = END;
  pthread_spin_unlock (&buffer.lock);
  pthread_join (buffer.pthread, NULL);

cleanup:
  pthread_spin_destroy (&buffer.lock);
  ow_engine_destroy (engine);
  sf_close (sf);
  free (buffer.mem);
  free (buffer.disk);
end:
  if (err)
    {
      error_print ("%s\n", ow_get_err_str (err));
    }
  return err;
}

int
main (int argc, char *argv[])
{
  int opt;
  int vflg = 0, lflg = 0, dflg = 0, nflg = 0, mflg = 0, errflg = 0;
  char *endstr;
  const char *device_name = NULL;
  int long_index = 0;
  ow_err_t ow_err;
  struct sigaction action;
  int device_num = -1;

  action.sa_handler = signal_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGUSR1, &action, NULL);

  while ((opt = getopt_long (argc, argv, "n:d:m:lvh",
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
	case 'm':
	  track_mask = optarg;
	  mflg++;
	  break;
	case 'l':
	  lflg++;
	  break;
	case 'v':
	  vflg++;
	  break;
	case 'h':
	  print_help (argv[0], PACKAGE_STRING, options);
	  exit (EXIT_SUCCESS);
	case '?':
	  errflg++;
	}
    }

  if (errflg > 0)
    {
      print_help (argv[0], PACKAGE_STRING, options);
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

  if (nflg + dflg == 1)
    {
      return run_dump (device_num, device_name);
    }
  else
    {
      fprintf (stderr, "Device not provided properly\n");
      exit (EXIT_FAILURE);
    }

  return EXIT_SUCCESS;
}
