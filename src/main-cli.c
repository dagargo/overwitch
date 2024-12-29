/*
 *   main-cli.c
 *   Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
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
#include <errno.h>
#include "../config.h"
#include "jclient.h"
#include "utils.h"
#include "common.h"

#define DEFAULT_QUALITY 2

static int blocks_per_transfer = OW_DEFAULT_BLOCKS;
static int quality = DEFAULT_QUALITY;
static int priority = JCLIENT_DEFAULT_PRIORITY;
static int xfr_timeout = OW_DEFAULT_XFR_TIMEOUT;

struct jclient jclient;
static int stop;
static int running;
static pthread_spinlock_t lock;	//Needed for signal handling

static struct option options[] = {
  {"use-device-number", 1, NULL, 'n'},
  {"use-device", 1, NULL, 'd'},
  {"bus-device-address", 1, NULL, 'a'},
  {"resampling-quality", 1, NULL, 'q'},
  {"blocks-per-transfer", 1, NULL, 'b'},
  {"usb-transfer-timeout", 1, NULL, 't'},
  {"rt-priority", 1, NULL, 'p'},
  {"rename", 1, NULL, 'r'},
  {"list-devices", 0, NULL, 'l'},
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
};

static void
signal_handler (int signum)
{
  switch (signum)
    {
    case SIGHUP:
    case SIGINT:
    case SIGTERM:
    case SIGTSTP:
      int r;
      pthread_spin_lock (&lock);
      stop = 1;
      r = running;
      pthread_spin_unlock (&lock);
      if (r)
	{
	  jclient_stop (&jclient);
	}
      break;
    case SIGUSR1:
      debug_level++;
      debug_print (1, "Debug level: %d", debug_level);
      break;
    case SIGUSR2:
      debug_level--;
      debug_level = debug_level < 0 ? 0 : debug_level;
      debug_print (1, "Debug level: %d", debug_level);
    }
}

static int
run_jclient (int device_num, const char *device_name, uint8_t bus,
	     uint8_t address)
{
  struct ow_device *device;
  int err;

  if (ow_get_device_from_device_attrs (device_num, device_name, bus, address,
				       &device))
    {
      return EXIT_FAILURE;
    }

  pthread_spin_lock (&lock);
  if (stop)
    {
      pthread_spin_unlock (&lock);
      return EXIT_SUCCESS;
    }
  pthread_spin_unlock (&lock);

  if (jclient_init (&jclient, device, blocks_per_transfer, xfr_timeout,
		    quality, priority))
    {
      free (device);
      return EXIT_FAILURE;
    }

  jclient_start (&jclient);

  pthread_spin_lock (&lock);
  if (stop)
    {
      jclient_stop (&jclient);
    }
  else
    {
      running = 1;
    }
  pthread_spin_unlock (&lock);

  jclient_wait (&jclient);
  jclient_destroy (&jclient);

  return err;
}

static int
rename_device (int device_num, const char *device_name, uint8_t bus,
	       uint8_t address, const char *name)
{
  struct ow_device *device;
  struct ow_engine *engine;
  struct ow_context context;
  int err;

  if (ow_get_device_from_device_attrs (device_num, device_name, bus,
				       address, &device))
    {
      return EXIT_FAILURE;
    }

  err = ow_engine_init_from_device (&engine, device, OW_DEFAULT_BLOCKS,
				    OW_DEFAULT_XFR_TIMEOUT);
  if (err)
    {
      free (device);
      goto end;
    }

  pthread_spin_lock (&lock);
  if (stop)
    {
      pthread_spin_unlock (&lock);
      err = EXIT_SUCCESS;
      goto end;
    }
  pthread_spin_unlock (&lock);

  context.dll = NULL;
  context.read_space = NULL;
  context.read = NULL;
  context.h2o_audio = NULL;
  context.options = 0;
  context.set_rt_priority = NULL;

  err = ow_engine_start (engine, &context);
  if (!err)
    {
      ow_engine_set_overbridge_name (engine, name);
      ow_engine_stop (engine);
      ow_engine_wait (engine);
    }

end:
  return err;
}

int
main (int argc, char *argv[])
{
  int opt, err = EXIT_SUCCESS;
  int vflg = 0, lflg = 0, dflg = 0, bflg = 0, pflg = 0, tflg = 0, nflg =
    0, aflg = 0, rflg = 0, errflg = 0;
  char *endstr;
  char *device_name = NULL, *name = NULL;
  uint8_t bus = 0, address = 0;
  int long_index = 0;
  ow_err_t ow_err;
  struct sigaction action;
  int device_num = -1;

  running = 0;
  pthread_spin_init (&lock, PTHREAD_PROCESS_PRIVATE);

  action.sa_handler = signal_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGTSTP, &action, NULL);
  sigaction (SIGUSR1, &action, NULL);
  sigaction (SIGUSR2, &action, NULL);

  while ((opt = getopt_long (argc, argv, "sn:d:a:q:b:t:p:r:lvh",
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
	  err = get_bus_address_from_str (optarg, &bus, &address);
	  if (err)
	    {
	      error_print ("Bus and address not provided properly");
	      goto cleanup;
	    }
	  aflg++;
	  break;
	case 'q':
	  errno = 0;
	  quality = (int) strtol (optarg, &endstr, 10);
	  if (errno || endstr == optarg || *endstr != '\0' || quality > 4
	      || quality < 0)
	    {
	      quality = DEFAULT_QUALITY;
	      fprintf (stderr,
		       "Resampling quality value must be in [0..4]. Using value %d...\n",
		       quality);
	    }
	  break;
	case 'b':
	  blocks_per_transfer = get_ow_blocks_per_transfer_argument (optarg);
	  bflg++;
	  break;
	case 't':
	  xfr_timeout = get_ow_xfr_timeout_argument (optarg);
	  tflg++;
	  break;
	case 'p':
	  errno = 0;
	  priority = (int) strtol (optarg, &endstr, 10);
	  if (errno || endstr == optarg || *endstr != '\0' || priority < 0
	      || priority > 99)
	    {
	      priority = JCLIENT_DEFAULT_PRIORITY;
	      fprintf (stderr,
		       "Priority value must be in [0..99]. Using default JACK value...\n");
	    }
	  pflg++;
	  break;
	case 'r':
	  name = optarg;
	  rflg++;
	  break;
	case 'l':
	  lflg++;
	  break;
	case 'v':
	  vflg++;
	  break;
	case 'h':
	  print_help (argv[0], PACKAGE_STRING, options, NULL);
	  goto cleanup;
	case '?':
	  errflg++;
	}
    }

  if (errflg > 0)
    {
      err = EXIT_FAILURE;
      goto cleanup;
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
	  err = EXIT_FAILURE;
	}
      goto cleanup;
    }

  if (bflg > 1)
    {
      fprintf (stderr, "Undetermined blocks\n");
      err = EXIT_FAILURE;
      goto cleanup;
    }

  if (pflg > 1)
    {
      fprintf (stderr, "Undetermined priority\n");
      err = EXIT_FAILURE;
      goto cleanup;
    }

  if (tflg > 1)
    {
      fprintf (stderr, "Undetermined timeout\n");
      err = EXIT_FAILURE;
      goto cleanup;
    }

  if (rflg > 1)
    {
      fprintf (stderr, "Undetermined name\n");
      err = EXIT_FAILURE;
      goto cleanup;
    }

  if (nflg + dflg + aflg == 1)
    {
      if (rflg)
	{
	  err = rename_device (device_num, device_name, bus, address, name);
	}
      else
	{
	  err = run_jclient (device_num, device_name, bus, address);
	}
    }
  else
    {
      fprintf (stderr, "Device not provided properly\n");
      err = EXIT_FAILURE;
    }

cleanup:
  pthread_spin_destroy (&lock);

  return err;
}
