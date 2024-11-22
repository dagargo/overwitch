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
#define POOLED_JCLIENT_LEN 64

struct pooled_jclient
{
  int used;
  struct jclient jclient;
};

static int stop_all;
static struct pooled_jclient jcpool[POOLED_JCLIENT_LEN];
static pthread_spinlock_t lock;	//Needed for signal handling

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
signal_handler (int signum)
{
  if (signum == SIGHUP || signum == SIGINT || signum == SIGTERM
      || signum == SIGTSTP)
    {
      struct pooled_jclient *pjc = jcpool;

      pthread_spin_lock (&lock);
      stop_all = 1;

      for (int i = 0; i < POOLED_JCLIENT_LEN; i++, pjc++)
	{
	  if (pjc->used)
	    {
	      jclient_stop (&pjc->jclient);
	    }
	}
      pthread_spin_unlock (&lock);
    }
  else if (signum == SIGUSR1)
    {
      debug_level++;
      debug_print (1, "Debug level: %d", debug_level);
    }
  else if (signum == SIGUSR2)
    {
      debug_level--;
      debug_level = debug_level < 0 ? 0 : debug_level;
      debug_print (1, "Debug level: %d", debug_level);
    }
}

static int
run_single (int device_num, const char *device_name,
	    unsigned int blocks_per_transfer, unsigned int xfr_timeout,
	    int quality, int priority)
{
  struct ow_usb_device *device;
  struct pooled_jclient *pjc;
  int err;

  if (ow_get_usb_device_from_device_attrs (device_num, device_name, &device))
    {
      return EXIT_FAILURE;
    }

  pthread_spin_lock (&lock);
  if (stop_all)
    {
      pthread_spin_unlock (&lock);
      goto end;
    }

  pjc = jcpool;

  if (jclient_init (&pjc->jclient, device->bus, device->address,
		    blocks_per_transfer, xfr_timeout, quality, priority))
    {
      pthread_spin_unlock (&lock);
      err = EXIT_FAILURE;
      goto end;
    }

  pjc->used = 1;
  jclient_start (&pjc->jclient);
  pthread_spin_unlock (&lock);

  if (stop_all)
    {
      jclient_stop (&pjc->jclient);
    }

  jclient_wait (&pjc->jclient);
  jclient_destroy (&pjc->jclient);

  pthread_spin_lock (&lock);
  pjc->used = 0;
  pthread_spin_unlock (&lock);

end:
  free (device);
  return err;
}

static int
run_all (unsigned int blocks_per_transfer, unsigned int xfr_timeout,
	 int quality, int priority)
{
  struct ow_usb_device *devices;
  struct ow_usb_device *device;
  struct pooled_jclient *pjc;
  size_t jclient_total_count;

  if (ow_get_usb_device_list (&devices, &jclient_total_count))
    {
      return EXIT_FAILURE;
    }

  pthread_spin_lock (&lock);
  if (stop_all)
    {
      pthread_spin_unlock (&lock);
      goto end;
    }

  device = devices;
  pjc = jcpool;
  jclient_total_count =
    jclient_total_count >
    POOLED_JCLIENT_LEN ? POOLED_JCLIENT_LEN : jclient_total_count;
  for (int i = 0; i < jclient_total_count; i++, device++)
    {
      if (jclient_init (&pjc->jclient, device->bus, device->address,
			blocks_per_transfer, xfr_timeout, quality, priority))
	{
	  continue;
	}

      pjc->used = 1;
      jclient_start (&pjc->jclient);
      pjc++;
    }
  pthread_spin_unlock (&lock);

  pjc = jcpool;
  for (int i = 0; i < POOLED_JCLIENT_LEN; i++, pjc++)
    {
      int used;

      pthread_spin_lock (&lock);
      used = pjc->used;
      pthread_spin_unlock (&lock);

      if (used)
	{
	  if (stop_all)
	    {
	      jclient_stop (&pjc->jclient);
	    }

	  jclient_wait (&pjc->jclient);
	  jclient_destroy (&pjc->jclient);

	  pthread_spin_lock (&lock);
	  pjc->used = 0;
	  pthread_spin_unlock (&lock);
	}
    }

end:
  free (devices);
  return EXIT_SUCCESS;
}

int
main (int argc, char *argv[])
{
  int opt, err = EXIT_SUCCESS;
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
	  err = EXIT_SUCCESS;
	case '?':
	  errflg++;
	}
    }

  if (errflg > 0)
    {
      print_help (argv[0], PACKAGE_STRING, options, NULL);
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
	  goto cleanup;
	}
      exit (EXIT_SUCCESS);
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
      err = EXIT_FAILURE;
    }

cleanup:
  pthread_spin_destroy (&lock);

  return err;
}
