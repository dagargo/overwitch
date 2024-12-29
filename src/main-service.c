/*
 *   main-service.c
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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

#define _GNU_SOURCE
#include <signal.h>
#include <errno.h>
#include "../config.h"
#include "jclient.h"
#include "utils.h"
#include "config.h"

#define POOLED_JCLIENT_LEN 64

typedef enum
{
  PJC_AVAILABLE = 0,
  PJC_RUNNING = 1,
  PJC_STOPPED = 2,
} pooled_jclient_status_t;

struct pooled_jclient
{
  pooled_jclient_status_t status;
  pthread_t thread;
  struct jclient jclient;
};

static struct ow_config config;
static struct pooled_jclient jcpool[POOLED_JCLIENT_LEN];
static pthread_spinlock_t lock;	//Needed for signal handling
static gint hotplug_running;
static pthread_t hotplug_thread;
static gint stop_all;
static GApplication *app;

static void
signal_handler (int signum)
{
  struct pooled_jclient *pjc = jcpool;

  pthread_spin_lock (&lock);
  stop_all = 1;
  hotplug_running = 0;

  for (guint i = 0; i < POOLED_JCLIENT_LEN; i++, pjc++)
    {
      if (pjc->status == PJC_RUNNING)
	{
	  jclient_stop (&pjc->jclient);
	}
    }
  pthread_spin_unlock (&lock);

  g_application_release (app);
}

static void *
jclient_runner (void *data)
{
  struct pooled_jclient *pjc = data;

  jclient_start (&pjc->jclient);

  pthread_spin_lock (&lock);
  if (stop_all)
    {
      jclient_stop (&pjc->jclient);
    }
  pthread_spin_unlock (&lock);

  jclient_wait (&pjc->jclient);
  jclient_destroy (&pjc->jclient);

  pthread_spin_unlock (&lock);
  pjc->status = PJC_STOPPED;
  pthread_spin_unlock (&lock);

  return NULL;
}

static int
start_all ()
{
  struct ow_device *devices;
  struct ow_device *device;
  struct pooled_jclient *pjc;
  size_t jclient_total_count;

  if (ow_get_device_list (&devices, &jclient_total_count))
    {
      return EXIT_FAILURE;
    }

  pthread_spin_lock (&lock);
  if (stop_all)
    {
      pthread_spin_unlock (&lock);
      goto end;
    }
  pthread_spin_unlock (&lock);

  device = devices;
  pjc = jcpool;
  jclient_total_count =
    jclient_total_count >
    POOLED_JCLIENT_LEN ? POOLED_JCLIENT_LEN : jclient_total_count;
  for (guint i = 0; i < jclient_total_count; i++, device++)
    {
      struct ow_device *copy = malloc (sizeof (struct ow_device));
      memcpy (copy, device, sizeof (struct ow_device));
      if (jclient_init (&pjc->jclient, copy, config.blocks, config.timeout,
			config.quality, JCLIENT_DEFAULT_PRIORITY))
	{
	  free (copy);
	  continue;
	}

      debug_print (1, "Starting pooled jclient %d...", i);
      pjc->status = PJC_RUNNING;
      if (pthread_create (&pjc->thread, NULL, jclient_runner, pjc))
	{
	  error_print ("Could not start thread");
	  goto end;
	}
      pthread_setname_np (pjc->thread, "cli-worker");
      pjc++;
    }

end:
  free (devices);
  return EXIT_SUCCESS;
}

static void
wait_all ()
{
  struct pooled_jclient *pjc = jcpool;

  for (guint i = 0; i < POOLED_JCLIENT_LEN; i++)
    {
      pooled_jclient_status_t status;

      pthread_spin_lock (&lock);
      status = pjc->status;
      pthread_spin_unlock (&lock);

      if (status != PJC_AVAILABLE)
	{
	  debug_print (1, "Waiting pooled jclient %d...", i);
	  pthread_join (pjc->thread, NULL);
	}

      pjc++;
    }
}

static void
hotplug_callback (struct ow_device *device)
{
  guint i;
  struct pooled_jclient *pjc;

  pjc = jcpool;

  for (i = 0; i < POOLED_JCLIENT_LEN; i++)
    {
      pooled_jclient_status_t status;

      pthread_spin_lock (&lock);
      status = pjc->status;
      pthread_spin_unlock (&lock);

      if (status == PJC_STOPPED)
	{
	  debug_print (1, "Recycling pooled jclient %d...", i);
	  pthread_join (pjc->thread, NULL);

	  pthread_spin_lock (&lock);
	  pjc->status = PJC_AVAILABLE;
	  pthread_spin_unlock (&lock);
	}

      pjc++;
    }

  pjc = jcpool;

  for (i = 0; i < POOLED_JCLIENT_LEN; i++)
    {
      pooled_jclient_status_t status;

      pthread_spin_lock (&lock);
      status = pjc->status;
      pthread_spin_unlock (&lock);

      if (status == PJC_AVAILABLE)
	{
	  debug_print (1, "Pooled jclient %d available...", i);
	  break;
	}

      pjc++;
    }

  if (i == POOLED_JCLIENT_LEN)
    {
      error_print ("No pooled jclients available");
      return;
    }

  pthread_spin_lock (&lock);
  if (stop_all)
    {
      pthread_spin_unlock (&lock);
      return;
    }

  if (jclient_init (&pjc->jclient, device, config.blocks, config.timeout,
		    config.quality, JCLIENT_DEFAULT_PRIORITY))
    {
      pthread_spin_unlock (&lock);
      return;
    }

  pjc->status = PJC_RUNNING;
  pthread_spin_unlock (&lock);

  if (!pthread_create (&pjc->thread, NULL, jclient_runner, pjc))
    {
      pthread_setname_np (pjc->thread, "cli-worker");
    }
}

static void *
hotplug_runner (void *data)
{
  hotplug_running = 1;
  ow_hotplug_loop (&hotplug_running, &lock, hotplug_callback);
  return NULL;
}

static void
overwitch_startup (GApplication *app, gpointer *user_data)
{
  ow_load_config (&config);

  if (config.pipewire_props)
    {
      debug_print (1, "Setting %s to '%s'...", PIPEWIRE_PROPS_ENV_VAR,
		   config.pipewire_props);
      setenv (PIPEWIRE_PROPS_ENV_VAR, config.pipewire_props, TRUE);
    }

  if (start_all ())
    {
      return;
    }

  g_application_hold (app);

  if (pthread_create (&hotplug_thread, NULL, hotplug_runner, NULL))
    {
      error_print ("Could not start hotplug thread");
    }
  else
    {
      pthread_setname_np (hotplug_thread, "hotplug-worker");
    }
}

static void
overwitch_activate (GApplication *app, gpointer *user_data)
{
}

gint
main (gint argc, gchar *argv[])
{
  gint status;
  struct sigaction action;

  debug_level = 1;

  action.sa_handler = signal_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGTERM, &action, NULL);

  pthread_spin_init (&lock, PTHREAD_PROCESS_PRIVATE);

  app = g_application_new (PACKAGE_SERVICE_NAME, G_APPLICATION_DEFAULT_FLAGS);

  g_signal_connect (app, "startup", G_CALLBACK (overwitch_startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (overwitch_activate), NULL);

  status = g_application_run (G_APPLICATION (app), argc, argv);

  g_object_unref (app);

  g_free (config.pipewire_props);

  wait_all ();

  if (hotplug_thread)
    {
      pthread_join (hotplug_thread, NULL);
    }

  pthread_spin_destroy (&lock);

  return status;
}
