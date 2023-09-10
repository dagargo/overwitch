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

static size_t jclient_count;
static struct jclient *jclients;

static struct option options[] = {
  {"use-device-number", 1, NULL, 'n'},
  {"use-device", 1, NULL, 'd'},
  {"resampling-quality", 1, NULL, 'q'},
  {"blocks-per-transfer", 1, NULL, 'b'},
  {"usb-transfer-timeout", 1, NULL, 't'},
  {"rt-priority", 1, NULL, 'p'},
  {"list-devices", 0, NULL, 'l'},
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
};

static void
signal_handler (int signo)
{
  if (signo == SIGHUP || signo == SIGINT || signo == SIGTERM
      || signo == SIGTSTP)
    {
      struct jclient *jclient = jclients;
      for (int i = 0; i < jclient_count; i++, jclient++)
	{
	  jclient_stop (jclient);
	}
    }
  else if (signo == SIGUSR1)
    {
      struct jclient *jclient = jclients;
      for (int i = 0; i < jclient_count; i++, jclient++)
	{
	  ow_resampler_report_status (jclient->resampler);
	}
    }
}

static int
run_single (int device_num, const char *device_name,
	    unsigned int blocks_per_transfer, unsigned int xfr_timeout,
	    int quality, int priority)
{
  struct ow_usb_device *device;
  ow_err_t err = OW_OK;

  if (ow_get_usb_device_from_device_attrs (device_num, device_name, &device))
    {
      return OW_GENERIC_ERROR;
    }

  jclient_count = 1;
  jclients = malloc (sizeof (struct jclient));
  jclients->bus = device->bus;
  jclients->address = device->address;
  jclients->blocks_per_transfer = blocks_per_transfer;
  jclients->xfr_timeout = xfr_timeout;
  jclients->quality = quality;
  jclients->priority = priority;
  jclients->end_notifier = NULL;

  free (device);

  if (jclient_init (jclients))
    {
      err = OW_GENERIC_ERROR;
      goto end;
    }

  jclient_start (jclients);
  jclient_wait (jclients);

end:
  free (jclients);
  return err;
}

static int
run_all (unsigned int blocks_per_transfer, unsigned int xfr_timeout,
	 int quality, int priority)
{
  struct ow_usb_device *devices;
  struct ow_usb_device *device;
  struct jclient *jclient;
  ow_err_t err = ow_get_usb_device_list (&devices, &jclient_count);

  if (err)
    {
      return err;
    }

  jclients = malloc (sizeof (struct jclient) * jclient_count);

  device = devices;
  jclient = jclients;
  for (int i = 0; i < jclient_count; i++, jclient++, device++)
    {
      jclient->bus = device->bus;
      jclient->address = device->address;
      jclient->blocks_per_transfer = blocks_per_transfer;
      jclient->xfr_timeout = xfr_timeout;
      jclient->quality = quality;
      jclient->priority = priority;
      jclient->end_notifier = NULL;

      if (jclient_init (jclient))
	{
	  continue;
	}

      jclient_start (jclient);
    }

  ow_free_usb_device_list (devices, jclient_count);

  jclient = jclients;
  for (int i = 0; i < jclient_count; i++, jclient++)
    {
      jclient_wait (jclient);
    }

  free (jclients);

  return OW_OK;
}

int
main (int argc, char *argv[])
{
  int opt;
  int vflg = 0, lflg = 0, dflg = 0, bflg = 0, pflg = 0, tflg = 0, nflg =
    0, errflg = 0;
  char *endstr;
  char *device_name = NULL;
  int long_index = 0;
  ow_err_t ow_err;
  struct sigaction action;
  int device_num = -1;
  int blocks_per_transfer = OW_DEFAULT_BLOCKS;
  int quality = DEFAULT_QUALITY;
  int priority = JCLIENT_DEFAULT_PRIORITY;
  int xfr_timeout = OW_DEFAULT_XFR_TIMEOUT;

  action.sa_handler = signal_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGUSR1, &action, NULL);
  sigaction (SIGTSTP, &action, NULL);

  while ((opt = getopt_long (argc, argv, "n:d:q:b:t:p:lvh",
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

  if (bflg > 1)
    {
      fprintf (stderr, "Undetermined blocks\n");
      exit (EXIT_FAILURE);
    }

  if (pflg > 1)
    {
      fprintf (stderr, "Undetermined priority\n");
      exit (EXIT_FAILURE);
    }

  if (tflg > 1)
    {
      fprintf (stderr, "Undetermined timeout\n");
      exit (EXIT_FAILURE);
    }

  if (nflg + dflg == 0)
    {
      return run_all (blocks_per_transfer, xfr_timeout, quality, priority);
    }
  else if (nflg + dflg == 1)
    {
      return run_single (device_num, device_name, blocks_per_transfer,
			 xfr_timeout, quality, priority);
    }
  else
    {
      fprintf (stderr, "Device not provided properly\n");
      exit (EXIT_FAILURE);
    }

  return EXIT_SUCCESS;
}
