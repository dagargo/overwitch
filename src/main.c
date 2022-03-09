/*
 *   main.c
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

#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <libgen.h>
#define _GNU_SOURCE
#include <getopt.h>
#include <string.h>
#include "../config.h"
#include "jclient.h"

#define DEFAULT_QUALITY 2
#define DEFAULT_BLOCKS 24
#define DEFAULT_PRIORITY -1	//With this value the default priority will be used.

struct jclient_thread
{
  pthread_t thread;
  struct jclient jclient;
};

static int overwitch_count;
static struct jclient_thread **overwitch;

static struct option options[] = {
  {"use-device-number", 1, NULL, 'n'},
  {"use-device", 1, NULL, 'd'},
  {"resampling-quality", 1, NULL, 'q'},
  {"transfer-blocks", 1, NULL, 'b'},
  {"rt-overwitch-priority", 1, NULL, 'p'},
  {"list-devices", 0, NULL, 'l'},
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
};

static void
overwitch_signal_handler (int signo)
{
  if (signo == SIGHUP || signo == SIGINT || signo == SIGTERM)
    {
      for (int i = 0; i < overwitch_count; i++)
	{
	  jclient_exit (&overwitch[i]->jclient);
	}
    }
  else if (signo == SIGUSR1)
    {
      for (int i = 0; i < overwitch_count; i++)
	{
	  jclient_print_latencies (&overwitch[i]->jclient.resampler, "\n");
	}
    }
}

void
overwitch_print_help (char *executable_path)
{
  char *exec_name;
  struct option *option;

  fprintf (stderr, "%s\n", PACKAGE_STRING);
  exec_name = basename (executable_path);
  fprintf (stderr, "Usage: %s [options]\n", exec_name);
  fprintf (stderr, "Options:\n");
  option = options;
  while (option->name)
    {
      fprintf (stderr, "  --%s, -%c", option->name, option->val);
      if (option->has_arg)
	{
	  fprintf (stderr, " value");
	}
      fprintf (stderr, "\n");
      option++;
    }
}

static int
overwitch_run_single (int device_num, char *device_name,
		      int blocks_per_transfer, int quality, int priority)
{
  uint8_t bus, address;
  int status;

  if (overwitch_get_bus_address (device_num, device_name, &bus, &address))
    {
      return EXIT_FAILURE;
    }

  overwitch_count = 1;
  overwitch = malloc (sizeof (struct jclient_thread *));
  overwitch[0] = malloc (sizeof (struct jclient_thread));
  overwitch[0]->jclient.bus = bus;
  overwitch[0]->jclient.address = address;
  overwitch[0]->jclient.blocks_per_transfer = blocks_per_transfer;
  overwitch[0]->jclient.quality = quality;
  overwitch[0]->jclient.priority = priority;

  pthread_create (&overwitch[0]->thread, NULL, jclient_run_thread,
		  &overwitch[0]->jclient);
  pthread_join (overwitch[0]->thread, NULL);
  status = overwitch[0]->jclient.resampler.status;
  free (overwitch[0]);
  free (overwitch);

  return status;
}

static int
overwitch_run_all (int blocks_per_transfer, int quality, int priority)
{
  uint8_t bus, address;
  libusb_context *context = NULL;
  libusb_device **list = NULL;
  int err, i;
  ssize_t count = 0;
  libusb_device *device = NULL;
  struct libusb_device_descriptor desc;
  char *dev_name;
  int status;

  err = libusb_init (&context);
  if (err != LIBUSB_SUCCESS)
    {
      return err;
    }

  overwitch_count = 0;
  count = libusb_get_device_list (context, &list);
  overwitch = malloc (sizeof (struct jclient_thread *) * count);
  for (i = 0; i < count; i++)
    {
      device = list[i];
      err = libusb_get_device_descriptor (device, &desc);
      if (err)
	{
	  continue;
	}

      if (overwitch_is_valid_device
	  (desc.idVendor, desc.idProduct, &dev_name))
	{
	  overwitch[overwitch_count] =
	    malloc (sizeof (struct jclient_thread));
	  bus = libusb_get_bus_number (device);
	  address = libusb_get_device_address (device);

	  overwitch[overwitch_count]->jclient.bus = bus;
	  overwitch[overwitch_count]->jclient.address = address;
	  overwitch[overwitch_count]->jclient.blocks_per_transfer =
	    blocks_per_transfer;
	  overwitch[overwitch_count]->jclient.quality = quality;
	  overwitch[overwitch_count]->jclient.priority = priority;

	  pthread_create (&overwitch[overwitch_count]->thread, NULL,
			  jclient_run_thread,
			  &overwitch[overwitch_count]->jclient);

	  overwitch_count++;
	}
    }

  libusb_free_device_list (list, count);
  libusb_exit (context);

  if (overwitch_count)
    {
      status = 0;
      for (int i = 0; i < overwitch_count; i++)
	{
	  pthread_join (overwitch[i]->thread, NULL);
	  status |= overwitch[i]->jclient.resampler.status;
	  free (overwitch[i]);
	}
      free (overwitch);
    }
  else
    {
      error_print ("No device found\n");
    }

  return status;
}

int
main (int argc, char *argv[])
{
  int opt;
  int vflg = 0, lflg = 0, dflg = 0, bflg = 0, pflg = 0, nflg = 0, errflg = 0;
  char *endstr;
  char *device_name = NULL;
  int long_index = 0;
  overwitch_err_t ob_status;
  struct sigaction action;
  int device_num = -1;
  int blocks_per_transfer = DEFAULT_BLOCKS;
  int quality = DEFAULT_QUALITY;
  int priority = DEFAULT_PRIORITY;

  action.sa_handler = overwitch_signal_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGUSR1, &action, NULL);

  while ((opt = getopt_long (argc, argv, "n:d:q:b:p:lvh",
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
	  blocks_per_transfer = (int) strtol (optarg, &endstr, 10);
	  if (errno || endstr == optarg || *endstr != '\0'
	      || blocks_per_transfer < 2 || blocks_per_transfer > 32)
	    {
	      blocks_per_transfer = DEFAULT_BLOCKS;
	      fprintf (stderr,
		       "Blocks value must be in [2..32]. Using value %d...\n",
		       blocks_per_transfer);
	    }
	  bflg++;
	  break;
	case 'p':
	  priority = (int) strtol (optarg, &endstr, 10);
	  if (errno || endstr == optarg || *endstr != '\0' || priority < 0
	      || priority > 99)
	    {
	      priority = -1;
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
	  overwitch_print_help (argv[0]);
	  exit (EXIT_SUCCESS);
	case '?':
	  errflg++;
	}
    }

  if (errflg > 0)
    {
      overwitch_print_help (argv[0]);
      exit (EXIT_FAILURE);
    }

  if (vflg)
    {
      debug_level = vflg;
    }

  if (lflg)
    {
      ob_status = overwitch_list_devices ();
      if (ob_status)
	{
	  fprintf (stderr, "USB error: %s\n",
		   overbrigde_get_err_str (ob_status));
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

  if (nflg + dflg == 0)
    {
      return overwitch_run_all (blocks_per_transfer, quality, priority);
    }
  else if (nflg + dflg == 1)
    {
      return overwitch_run_single (device_num, device_name,
				   blocks_per_transfer, quality, priority);
    }
  else
    {
      fprintf (stderr, "Device not provided properly\n");
      exit (EXIT_FAILURE);
    }

  return 0;
}
