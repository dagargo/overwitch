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
#include "../config.h"
#include "jclient.h"

#define DEFAULT_QUALITY 2
#define DEFAULT_BLOCKS 24
#define DEFAULT_PRIORITY -1	//With this value the default priority will be used.

static struct option options[] = {
  {"use-device", 1, NULL, 'd'},
  {"resampling-quality", 1, NULL, 'q'},
  {"transfer-blocks", 1, NULL, 'b'},
  {"rt-overbridge-priority", 1, NULL, 'p'},
  {"list-devices", 0, NULL, 'l'},
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
};

static struct jclient jclient;

static void
overwitch_signal_handler (int signo)
{
  if (signo == SIGHUP || signo == SIGINT || signo == SIGTERM)
    {
      jclient_exit (&jclient);
    }
  else if (signo == SIGUSR1)
    {
      jclient_print_latencies (&jclient, "\n");
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

int
main (int argc, char *argv[])
{
  int opt;
  int vflg = 0, lflg = 0, dflg = 0, bflg = 0, pflg = 0, errflg = 0;
  char *endstr;
  char *device = NULL;
  int long_index = 0;
  overbridge_err_t ob_status;
  struct sigaction action;
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

  while ((opt = getopt_long (argc, argv, "d:q:b:p:lvh",
			     options, &long_index)) != -1)
    {
      switch (opt)
	{
	case 'd':
	  device = optarg;
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
      ob_status = overbridge_list_devices ();
      if (ob_status)
	{
	  fprintf (stderr, "USB error: %s\n",
		   overbrigde_get_err_str (ob_status));
	  exit (EXIT_FAILURE);
	}
      exit (EXIT_SUCCESS);
    }

  if (dflg != 1)
    {
      fprintf (stderr, "No device provided\n");
      exit (EXIT_FAILURE);
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

  return jclient_run (&jclient, device, blocks_per_transfer, quality,
		      priority);
}
