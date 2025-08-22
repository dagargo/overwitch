/*
 *   main-play.c
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
#include "utils.h"
#include "common.h"

static struct ow_context context;
static struct ow_engine *engine;
static SF_INFO sfinfo;
static SNDFILE *sf;
static float max[OB_MAX_TRACKS];
static float min[OB_MAX_TRACKS];
static char *file;
static sf_count_t frames;

static struct option options[] = {
  {"use-device-number", 1, NULL, 'n'},
  {"use-device", 1, NULL, 'd'},
  {"bus-device-address", 1, NULL, 'a'},
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
  fprintf (stderr, "%lu frames read\n", frames);
}

static size_t
buffer_read_space (void *data)
{
  const struct ow_device_desc *desc = &ow_engine_get_device (engine)->desc;
  size_t rbsp = (sfinfo.frames - frames) * desc->inputs * OW_BYTES_PER_SAMPLE;

  if (!rbsp)
    {
      ow_engine_stop (engine);
    }

  return rbsp;
}

static size_t
buffer_read (void *data, char *buf, size_t size)
{
  const struct ow_device_desc *desc = &ow_engine_get_device (engine)->desc;
  int bytes_per_frame = desc->inputs * OW_BYTES_PER_SAMPLE;
  sf_count_t wanted_frames, read_frames;

  wanted_frames = size / bytes_per_frame;

  debug_print (2, "Reading %ld bytes (%ld frames) from file...", size,
	       wanted_frames);

  read_frames = sf_readf_float (sf, (float *) buf, wanted_frames);

  if (read_frames < wanted_frames)
    {
      ow_engine_stop (engine);
    }

  for (int i = 0; i < read_frames; i++)
    {
      for (int j = 0; j < desc->inputs; j++)
	{
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
	  buf += OW_BYTES_PER_SAMPLE;
	}
    }

  frames += read_frames;
  return read_frames * bytes_per_frame;
}

static void
signal_handler (int signo)
{
  const struct ow_device_desc *desc = &ow_engine_get_device (engine)->desc;
  print_status ();
  if (debug_level)
    {
      for (int i = 0; i < desc->inputs; i++)
	{
	  fprintf (stderr, "%s: max: %f; min: %f\n",
		   desc->input_tracks[i].name, max[i], min[i]);
	}
    }
  if (signo == SIGHUP || signo == SIGINT || signo == SIGTERM
      || signo == SIGTSTP)
    {
      ow_engine_stop (engine);
    }
}

static int
run_play (int device_num, const char *device_name, uint8_t bus,
	  uint8_t address, unsigned int blocks_per_transfer,
	  unsigned int xfr_timeout, const char *file)
{
  ow_err_t err;
  struct ow_device *device;

  if (ow_get_device_from_device_attrs (device_num, device_name, bus,
				       address, &device))
    {
      return OW_GENERIC_ERROR;
    }

  err = ow_engine_init_from_device (&engine, device, blocks_per_transfer,
				    xfr_timeout);
  if (err)
    {
      free (device);
      goto end;
    }

  sf = sf_open (file, SFM_READ, &sfinfo);
  if (!sf)
    {
      error_print ("Audio file could not be opened");
      err = OW_GENERIC_ERROR;
      goto cleanup_engine;
    }

  if (sfinfo.channels != device->desc.inputs)
    {
      error_print ("Number of channels do not match inputs");
      err = OW_GENERIC_ERROR;
      goto cleanup_audio;
    }

  for (int i = 0; i < device->desc.inputs; i++)
    {
      max[i] = 0.0f;
      min[i] = 0.0f;
    }

  ow_set_thread_rt_priority (pthread_self (), OW_DEFAULT_RT_PRIORITY);

  context.dll = NULL;
  context.read_space = buffer_read_space;
  context.read = buffer_read;
  context.h2o_audio = sf;
  context.options = OW_ENGINE_OPTION_H2O_AUDIO;

  err = ow_engine_start (engine, &context);
  if (!err)
    {
      ow_engine_wait (engine);
      print_status ();
    }

cleanup_audio:
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
  int nflg = 0, dflg = 0, aflg = 0, bflg = 0, tflg = 0;
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

  while ((opt = getopt_long (argc, argv, "n:d:a:b:t:lvh",
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
	  print_help (argv[0], PACKAGE_STRING, options, "file");
	  exit (EXIT_SUCCESS);
	case '?':
	  errflg++;
	}
    }

  if (optind + 1 == argc)
    {
      file = argv[optind];
    }
  else if (!lflg)
    {
      errflg++;
    }

  if (errflg > 0)
    {
      print_help (argv[0], PACKAGE_STRING, options, "audio_file");
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

  if (nflg + dflg == 1)
    {
      return run_play (device_num, device_name, bus, address,
		       blocks_per_transfer, xfr_timeout, file);
    }
  else
    {
      fprintf (stderr, "Device not provided properly\n");
      exit (EXIT_FAILURE);
    }

  return EXIT_SUCCESS;
}
