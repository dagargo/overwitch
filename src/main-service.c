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
#include <systemd/sd-daemon.h>
#include <time.h>
#include "../config.h"
#include "jclient.h"
#include "utils.h"
#include "preferences.h"
#include "message.h"

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

static struct ow_preferences preferences;
static struct pooled_jclient jcpool[POOLED_JCLIENT_LEN];
static pthread_spinlock_t lock;	//Needed for signal handling
static gint hotplug_running;
static pthread_t hotplug_thread;
static gint force_stop;
static GApplication *app;

static GDBusNodeInfo *introspection_data = NULL;

static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='" PACKAGE_SERVICE_DBUS_NAME "'>"
  "    <method name='Exit'>"
  "    </method>"
  "    <method name='Start'>"
  "    </method>"
  "    <method name='Stop'>"
  "    </method>"
  "    <method name='GetState'>"
  "      <arg type='s' name='status' direction='out'/>"
  "    </method>"
  "    <method name='SetDeviceName'>"
  "      <arg type='u' name='id' direction='in'/>"
  "      <arg type='s' name='name' direction='in'/>"
  "      <arg type='i' name='error' direction='out'/>"
  "    </method>" "  </interface>" "</node>";

static void startup ();
static void handle_stop ();
static void handle_start ();

static gboolean
overwitch_increment_debug_level (const gchar *option_name,
				 const gchar *value,
				 gpointer data, GError **error)
{
  debug_level++;
  return TRUE;
}

const GOptionEntry CMD_PARAMS[] = {
  {
   .long_name = "verbosity",
   .short_name = 'v',
   .flags = G_OPTION_FLAG_NO_ARG,
   .arg = G_OPTION_ARG_CALLBACK,
   .arg_data = overwitch_increment_debug_level,
   .description =
   "Increase verbosity. For more verbosity use it more than once.",
   .arg_description = NULL,
   },
  {NULL}
};

static void
stop_all ()
{
  struct pooled_jclient *pjc = jcpool;

  pthread_spin_lock (&lock);
  force_stop = 1;
  hotplug_running = 0;

  for (guint32 i = 0; i < POOLED_JCLIENT_LEN; i++, pjc++)
    {
      if (pjc->status == PJC_RUNNING)
	{
	  jclient_stop (&pjc->jclient);
	}
    }
  pthread_spin_unlock (&lock);
}

static void
signal_handler (int signum)
{
  static int handling = 0;

  if (signum == SIGTERM || signum == SIGINT)
    {
      if (handling)
	{
	  error_print
	    ("A signal is already being processed. Doing nothing...") return;
	}

      handling = 1;
      handle_stop ();
      g_application_release (app);
      handling = 0;
    }
  else if (signum == SIGHUP)
    {
      struct timespec ts;
      clock_gettime (CLOCK_MONOTONIC, &ts);
      sd_notifyf (0, "RELOADING=1\nMONOTONIC_USEC=%" PRIdMAX "%06d",
		  (intmax_t) ts.tv_sec, (int) (ts.tv_nsec / 1000));
      handle_start ();
      sd_notify (0, "READY=1");
    }
  else
    {
      error_print ("Signal not handled");
    }

}

static void *
jclient_runner (void *data)
{
  struct pooled_jclient *pjc = data;

  jclient_start (&pjc->jclient);

  pthread_spin_lock (&lock);
  if (force_stop)
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

static void
start_single (struct pooled_jclient *pjc, guint id, struct ow_device *device)
{
  if (jclient_init (&pjc->jclient, device, preferences.blocks,
		    preferences.timeout, preferences.quality,
		    JCLIENT_DEFAULT_PRIORITY))
    {
      free (device);
      return;
    }

  debug_print (1, "Starting pooled jclient %d...", id);
  pjc->status = PJC_RUNNING;
  if (pthread_create (&pjc->thread, NULL, jclient_runner, pjc))
    {
      error_print ("Could not start thread");
      jclient_destroy (&pjc->jclient);
      pjc->status = PJC_STOPPED;
      return;
    }

  gchar name[OW_LABEL_MAX_LEN];
  snprintf (name, OW_LABEL_MAX_LEN, "srv-%02d-%.8s", id, device->desc.name);
  pthread_setname_np (pjc->thread, name);
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
  if (force_stop)
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

      pthread_spin_lock (&lock);
      start_single (pjc, i, copy);
      pthread_spin_unlock (&lock);

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

  for (guint32 i = 0; i < POOLED_JCLIENT_LEN; i++)
    {
      pooled_jclient_status_t status;

      pthread_spin_lock (&lock);
      status = pjc->status;
      pthread_spin_unlock (&lock);

      if (status != PJC_AVAILABLE)
	{
	  debug_print (1, "Waiting pooled jclient %d...", i);
	  pthread_join (pjc->thread, NULL);
	  pjc->status = PJC_AVAILABLE;
	}

      pjc++;
    }
}

static void
hotplug_callback (struct ow_device *device)
{
  guint32 i;
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
  if (force_stop)
    {
      pthread_spin_unlock (&lock);
      return;
    }

  start_single (pjc, i, device);
  pthread_spin_unlock (&lock);
}

static void *
hotplug_runner (void *data)
{
  hotplug_running = 1;
  ow_hotplug_loop (&hotplug_running, &lock, hotplug_callback);
  return NULL;
}

static void
handle_stop ()
{
  stop_all ();
  wait_all ();
  if (hotplug_thread)
    {
      pthread_join (hotplug_thread, NULL);
    }
}

static void
handle_start ()
{
  handle_stop ();
  sleep (1);
  startup ();
}

static gchar *
handle_get_state ()
{
  struct pooled_jclient *pjc = jcpool;
  struct ow_resampler *resampler = NULL;

  JsonBuilder *builder = message_state_builder_start ();

  pthread_spin_lock (&lock);

  for (guint32 i = 0; i < POOLED_JCLIENT_LEN; i++)
    {
      if (pjc->status == PJC_RUNNING)
	{
	  struct ow_resampler_state state;
	  resampler = pjc->jclient.resampler;
	  struct ow_engine *engine = ow_resampler_get_engine (resampler);
	  const struct ow_device *device = ow_engine_get_device (engine);
	  const gchar *name = ow_engine_get_overbridge_name (engine);
	  ow_resampler_get_state_copy (resampler, &state);
	  message_state_builder_add_device (builder, i, name, device, &state);
	}

      pjc++;
    }

  guint32 bufsize = 0;
  guint32 samplerate = 0;
  gdouble target_delay_ms = 0;

  if (resampler)
    {
      samplerate = ow_resampler_get_samplerate (resampler);
      bufsize = ow_resampler_get_buffer_size (resampler);
      target_delay_ms = ow_resampler_get_target_delay_ms (resampler);
    }

  pthread_spin_unlock (&lock);

  return message_state_builder_end (builder, samplerate, bufsize,
				    target_delay_ms);
}

static gint
handle_set_device_name (guint id, const gchar *name)
{
  gint err = -1;

  pthread_spin_lock (&lock);

  struct pooled_jclient *pjc = &jcpool[id];

  if (pjc->status == PJC_RUNNING)
    {
      err = 0;

      struct ow_resampler *resampler = pjc->jclient.resampler;
      struct ow_engine *engine = ow_resampler_get_engine (resampler);

      struct ow_device *copy = malloc (sizeof (struct ow_device));
      memcpy (copy, pjc->jclient.device, sizeof (struct ow_device));

      ow_engine_set_overbridge_name (engine, name);

      jclient_stop (&pjc->jclient);
      pthread_join (pjc->thread, NULL);
      pjc->status = PJC_AVAILABLE;

      start_single (pjc, id, copy);
    }

  pthread_spin_unlock (&lock);

  return err;
}

static void
handle_method_call (GDBusConnection *connection, const gchar *sender,
		    const gchar *object_path, const gchar *interface_name,
		    const gchar *method_name, GVariant *parameters,
		    GDBusMethodInvocation *invocation, gpointer user_data)
{
  if (g_strcmp0 (method_name, "Exit") == 0)
    {
      handle_stop ();
      g_dbus_method_invocation_return_value (invocation, NULL);
      g_application_release (app);
    }
  else if (g_strcmp0 (method_name, "Start") == 0)
    {
      handle_start ();
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else if (g_strcmp0 (method_name, "Stop") == 0)
    {
      handle_stop ();
      g_dbus_method_invocation_return_value (invocation, NULL);
    }
  else if (g_strcmp0 (method_name, "GetState") == 0)
    {
      gchar *state = handle_get_state ();
      GVariant *v = g_variant_new ("(s)", state);
      g_dbus_method_invocation_return_value (invocation, v);
      g_free (state);
    }
  else if (g_strcmp0 (method_name, "SetDeviceName") == 0)
    {
      guint id;
      gchar *name;
      GVariant *params = g_dbus_method_invocation_get_parameters (invocation);
      g_variant_get (params, "(us)", &id, &name);
      gint err = handle_set_device_name (id, name);
      GVariant *v = g_variant_new ("(i)", err);
      g_dbus_method_invocation_return_value (invocation, v);
    }
  else
    {
      error_print ("Method not handled");
    }
}

static const GDBusInterfaceVTable interface_vtable = {
  handle_method_call,
  NULL,
  NULL,
};

static void
startup ()
{
  g_free (preferences.pipewire_props);

  ow_load_preferences (&preferences);

  // When under PipeWire, it is desirable to run Overwitch as a follower of the hardware driver.
  // This could be achieved by setting the node.group property to the same value the hardware has but there are other ways.
  // See https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/3612 for a thorough explanation of the need of this.
  // As setting an environment variable does not need additional libraries, this is backwards compatible.

  if (preferences.pipewire_props)
    {
      debug_print (1, "Setting %s to '%s'...", PIPEWIRE_PROPS_ENV_VAR,
		   preferences.pipewire_props);
      setenv (PIPEWIRE_PROPS_ENV_VAR, preferences.pipewire_props, TRUE);
    }

  force_stop = 0;
  if (start_all ())
    {
      return;
    }

  if (pthread_create (&hotplug_thread, NULL, hotplug_runner, NULL))
    {
      error_print ("Could not start hotplug thread");
    }
  else
    {
      pthread_setname_np (hotplug_thread, "srv-hotplug");
    }
}

static void
app_startup (GApplication *app, gpointer *user_data)
{
  guint id;
  GDBusConnection *conn;

  introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
  conn = g_application_get_dbus_connection (app);
  id = g_dbus_connection_register_object (conn,
					  "/io/github/dagargo/OverwitchService",
					  introspection_data->interfaces[0],
					  &interface_vtable, NULL, NULL,
					  NULL);
  g_dbus_node_info_unref (introspection_data);

  if (id)
    {
      startup ();
      g_application_hold (app);
      g_application_activate (app);
      sd_notify (0, "READY=1");
    }
}

static void
app_activate (GApplication *app, gpointer *user_data)
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
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);

  pthread_setname_np (pthread_self (), "overwitch-srv");

  pthread_spin_init (&lock, PTHREAD_PROCESS_PRIVATE);

  app = g_application_new (PACKAGE_SERVICE_DBUS_NAME,
			   G_APPLICATION_IS_SERVICE);

  g_signal_connect (app, "startup", G_CALLBACK (app_startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (app_activate), NULL);

  g_application_add_main_option_entries (G_APPLICATION (app), CMD_PARAMS);

  status = g_application_run (G_APPLICATION (app), argc, argv);

  g_object_unref (app);

  g_free (preferences.pipewire_props);

  pthread_spin_destroy (&lock);

  return status;
}
