/*
 *   main.c
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

#define _GNU_SOURCE
#include <errno.h>
#include <libusb.h>
#include <string.h>
#include <sched.h>
#include <gtk/gtk.h>
#include <jack/jack.h>
#include <json-glib/json-glib.h>
#include <glib/gi18n.h>
#include "common.h"
#include "jclient.h"
#include "utils.h"
#include "config.h"
#include "overwitch_device.h"

#define PAUSE_TO_BE_NOTIFIED_USECS 500000

struct overwitch_instance
{
  struct ow_resampler_latency latency;
  gdouble o2j_ratio;
  gdouble j2o_ratio;
  struct jclient jclient;
};

static jack_client_t *control_client;
static jack_nframes_t jack_sample_rate;
static jack_nframes_t jack_buffer_size;
static gchar *pipewire_props;
static gboolean pipewire_env_var_set;

static GtkApplication *app;

static GtkWidget *main_window;
static GtkAboutDialog *about_dialog;
static GtkWidget *preferences_window;
static GtkWidget *preferences_window_cancel_button;
static GtkWidget *preferences_window_save_button;
static GtkWidget *pipewire_props_dialog_entry;
static GtkWidget *refresh_button;
static GtkWidget *stop_button;
static GtkSpinButton *blocks_spin_button;
static GtkSpinButton *timeout_spin_button;
static GtkDropDown *quality_drop_down;
static GtkCellRendererText *name_cell_renderer;
static GtkColumnViewColumn *device_column;
static GtkColumnViewColumn *bus_column;
static GtkColumnViewColumn *address_column;
static GtkColumnViewColumn *o2j_ratio_column;
static GtkColumnViewColumn *j2o_ratio_column;
static GListStore *status_list_store;
static GtkLabel *jack_status_label;
static GtkLabel *target_delay_label;

static pthread_spinlock_t lock;	//Needed for signal handling
static gint hotplug_running;
static pthread_t hotplug_thread;

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

static const char *
get_status_string (ow_resampler_status_t status)
{
  switch (status)
    {
    case OW_RESAMPLER_STATUS_ERROR:
      return _("Error");
    case OW_RESAMPLER_STATUS_STOP:
      return _("Stopped");
    case OW_RESAMPLER_STATUS_READY:
      return _("Ready");
    case OW_RESAMPLER_STATUS_BOOT:
      return _("Booting");
    case OW_RESAMPLER_STATUS_TUNE:
      return _("Tuning");
    case OW_RESAMPLER_STATUS_RUN:
      return _("Running");
    }
  return NULL;
}

static void
set_dll_target_delay ()
{
  static char msg[OW_LABEL_MAX_LEN];
  GListModel *model = G_LIST_MODEL (status_list_store);

  if (g_list_model_get_n_items (model))
    {
      OverwitchDevice *dev = g_list_model_get_item (model, 0);
      struct overwitch_instance *instance = dev->instance;
      struct ow_resampler *resampler = instance->jclient.resampler;
      double target_delay_ms = ow_resampler_get_target_delay_ms (resampler);

      snprintf (msg, OW_LABEL_MAX_LEN, _("Target latency: %.1f ms"),
		target_delay_ms);
      gtk_label_set_text (target_delay_label, msg);
    }
  else
    {
      gtk_label_set_text (target_delay_label, "");
    }
}

static void
start_instance (struct overwitch_instance *instance)
{
  struct ow_resampler *resampler = instance->jclient.resampler;
  struct ow_engine *engine = ow_resampler_get_engine (resampler);
  debug_print (1, "Starting %s...", ow_engine_get_overbridge_name (engine));
  jclient_start (&instance->jclient);

  usleep (PAUSE_TO_BE_NOTIFIED_USECS);
  set_dll_target_delay ();
}

static void
stop_instance (struct overwitch_instance *instance)
{
  struct ow_resampler *resampler = instance->jclient.resampler;
  struct ow_engine *engine = ow_resampler_get_engine (resampler);
  debug_print (1, "Stopping %s...", ow_engine_get_overbridge_name (engine));
  jclient_stop (&instance->jclient);
}

static gboolean
set_overwitch_instance_status (gpointer data)
{
  struct overwitch_instance *instance = data;
  static gchar o2j_latency_s[OW_LABEL_MAX_LEN];
  static gchar j2o_latency_s[OW_LABEL_MAX_LEN];
  ow_resampler_status_t status;
  const char *status_string;

  GListModel *model = G_LIST_MODEL (status_list_store);

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      OverwitchDevice *dev = g_list_model_get_item (model, i);

      if (instance->jclient.bus == dev->bus
	  && instance->jclient.address == dev->address)
	{
	  if (instance->latency.o2h >= 0)
	    {
	      g_snprintf (o2j_latency_s, OW_LABEL_MAX_LEN,
			  "%.1f [%.1f, %.1f] ms", instance->latency.o2h,
			  instance->latency.o2h_min,
			  instance->latency.o2h_max);
	    }
	  else
	    {
	      o2j_latency_s[0] = '\0';
	    }

	  if (instance->latency.h2o >= 0)
	    {
	      g_snprintf (j2o_latency_s, OW_LABEL_MAX_LEN,
			  "%.1f [%.1f, %.1f] ms", instance->latency.h2o,
			  instance->latency.h2o_min,
			  instance->latency.h2o_max);
	    }
	  else
	    {
	      j2o_latency_s[0] = '\0';
	    }

	  status = ow_resampler_get_status (instance->jclient.resampler);
	  status_string = get_status_string (status);
	  overwitch_device_set_status (dev, status_string, o2j_latency_s,
				       j2o_latency_s, instance->o2j_ratio,
				       instance->j2o_ratio);

	  break;
	}
    }

  return FALSE;
}

static void
set_report_data (struct overwitch_instance *instance,
		 struct ow_resampler_latency *latency, gdouble o2j_ratio,
		 gdouble j2o_ratio)
{
  instance->latency.o2h = latency->o2h;
  instance->latency.o2h_min = latency->o2h_min;
  instance->latency.o2h_max = latency->o2h_max;
  instance->latency.h2o = latency->h2o;
  instance->latency.h2o_min = latency->h2o_min;
  instance->latency.h2o_max = latency->h2o_max;
  instance->o2j_ratio = o2j_ratio;
  instance->j2o_ratio = j2o_ratio;
  g_idle_add (set_overwitch_instance_status, instance);
}

static void
update_all_metrics (gboolean active)
{
  gtk_column_view_column_set_visible (device_column, active);
  gtk_column_view_column_set_visible (bus_column, active);
  gtk_column_view_column_set_visible (address_column, active);
  gtk_column_view_column_set_visible (o2j_ratio_column, active);
  gtk_column_view_column_set_visible (j2o_ratio_column, active);
}

static void
overwitch_show_all_columns (GSimpleAction *action,
			    GVariant *value, gpointer data)
{
  gboolean show_all_columns = g_variant_get_boolean (value);
  g_simple_action_set_state (action, value);
  update_all_metrics (show_all_columns);
}

static void
save_preferences ()
{
  struct ow_config config;
  GVariant *v;
  GAction *a;

  a = g_action_map_lookup_action (G_ACTION_MAP (app), "refresh_at_startup");
  v = g_action_get_state (a);
  g_variant_get (v, "b", &config.refresh_at_startup);

  a = g_action_map_lookup_action (G_ACTION_MAP (app), "show_all_columns");
  v = g_action_get_state (a);
  g_variant_get (v, "b", &config.show_all_columns);

  config.blocks = gtk_spin_button_get_value_as_int (blocks_spin_button);
  config.timeout = gtk_spin_button_get_value_as_int (timeout_spin_button);
  config.quality = gtk_drop_down_get_selected (quality_drop_down);
  config.pipewire_props = pipewire_props;

  ow_save_config (&config);
}

static void
load_preferences ()
{
  struct ow_config config;
  GVariant *v;
  GAction *a;

  ow_load_config (&config);

  pipewire_props = config.pipewire_props;

  a = g_action_map_lookup_action (G_ACTION_MAP (app), "refresh_at_startup");
  v = g_variant_new_boolean (config.refresh_at_startup);
  g_action_change_state (a, v);

  a = g_action_map_lookup_action (G_ACTION_MAP (app), "show_all_columns");
  v = g_variant_new_boolean (config.show_all_columns);
  g_action_change_state (a, v);

  gtk_spin_button_set_value (blocks_spin_button, config.blocks);
  gtk_spin_button_set_value (timeout_spin_button, config.timeout);
  gtk_drop_down_set_selected (quality_drop_down, config.quality);
  update_all_metrics (config.show_all_columns);
}

static gboolean
is_device_at_bus_address (uint8_t bus, uint8_t address, GtkTreeIter *iter)
{
  GListModel *model = G_LIST_MODEL (status_list_store);

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      OverwitchDevice *dev = g_list_model_get_item (model, i);
      if (dev->bus == bus && dev->address == address)
	{
	  return TRUE;
	}
    }

  return FALSE;
}

static void
set_widgets_to_running_state (gboolean running)
{
  gtk_widget_set_sensitive (stop_button, running);
  gtk_widget_set_sensitive (GTK_WIDGET (blocks_spin_button), !running);
  gtk_widget_set_sensitive (GTK_WIDGET (timeout_spin_button), !running);
  gtk_widget_set_sensitive (GTK_WIDGET (quality_drop_down), !running);
}

static gboolean
set_status (gpointer data)
{
  static char msg[OW_LABEL_MAX_LEN];
  if (control_client)
    {
      snprintf (msg, OW_LABEL_MAX_LEN, _("JACK at %.5g kHz, %d period"),
		jack_sample_rate / 1000.f, jack_buffer_size);
    }
  else
    {
      snprintf (msg, OW_LABEL_MAX_LEN, _("JACK not found"));
    }
  gtk_label_set_text (jack_status_label, msg);

  return G_SOURCE_REMOVE;
}

static int
set_sample_rate_cb (jack_nframes_t nframes, void *cb_data)
{
  debug_print (1, "JACK sample rate: %d", nframes);
  jack_sample_rate = nframes;
  g_idle_add (set_status, NULL);
  return 0;
}

static int
set_buffer_size_cb (jack_nframes_t nframes, void *cb_data)
{
  debug_print (1, "JACK buffer size: %d", nframes);
  jack_buffer_size = nframes;
  g_idle_add (set_status, NULL);
  return 0;
}

static void
stop_control_client ()
{
  if (control_client)
    {
      jack_deactivate (control_client);
      jack_client_close (control_client);
      control_client = NULL;
    }
}

static void
start_control_client ()
{
  if (control_client)
    {
      return;
    }

  control_client = jack_client_open ("Overwitch control client",
				     JackNoStartServer, NULL, NULL);
  if (control_client)
    {
      jack_sample_rate = jack_get_sample_rate (control_client);
      jack_buffer_size = jack_get_buffer_size (control_client);
      if (jack_set_sample_rate_callback (control_client, set_sample_rate_cb,
					 NULL))
	{
	  error_print ("Cannot set JACK control client sample rate callback");
	}
      if (jack_set_buffer_size_callback (control_client, set_buffer_size_cb,
					 NULL))
	{
	  error_print
	    ("Cannot set JACK control client set buffer size callback");
	}

      if (jack_activate (control_client))
	{
	  error_print ("Cannot activate control client");
	  jack_client_close (control_client);
	}
    }

  g_idle_add (set_status, NULL);
}

static void
remove_stopped_instances ()
{
  GListModel *model = G_LIST_MODEL (status_list_store);
  GSList *e, *to_delete = NULL;

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      OverwitchDevice *dev = g_list_model_get_item (model, i);
      struct overwitch_instance *instance = dev->instance;
      struct ow_resampler *resampler = instance->jclient.resampler;
      ow_resampler_status_t status = ow_resampler_get_status (resampler);
      if (status <= OW_RESAMPLER_STATUS_STOP)
	{
	  jclient_wait (&instance->jclient);
	  jclient_destroy (&instance->jclient);
	  g_free (instance);
	  to_delete = g_slist_prepend (to_delete, GUINT_TO_POINTER (i));
	}
    }

  e = to_delete;
  while (e)
    {
      g_list_store_remove (status_list_store, GPOINTER_TO_UINT (e->data));
      e = e->next;
    }

  g_slist_free (to_delete);
}

static void
refresh_all (GtkWidget *object, gpointer data)
{
  struct ow_usb_device *devices, *device;
  struct ow_resampler_reporter *reporter;
  struct overwitch_instance *instance;
  size_t devices_count;
  ow_err_t err;

  remove_stopped_instances ();

  set_dll_target_delay ();

  err = ow_get_usb_device_list (&devices, &devices_count);
  if (err || !devices_count)
    {
      set_widgets_to_running_state (FALSE);
      return;
    }

  device = devices;

  for (gint i = 0; i < devices_count; i++, device++)
    {
      GtkTreeIter iter;
      if (is_device_at_bus_address (device->bus, device->address, &iter))
	{
	  debug_print (2, "Device at %03d:%03d already running. Skipping...",
		       device->bus, device->address);
	  continue;
	}

      instance = g_malloc (sizeof (struct overwitch_instance));

      instance->latency.o2h = 0.0;
      instance->latency.h2o = 0.0;
      instance->o2j_ratio = 1.0;
      instance->j2o_ratio = 1.0;

      //Needed because jclient_init will momentarily block the GUI.
      while (g_main_context_pending (NULL))
	{
	  g_main_context_iteration (NULL, FALSE);
	}

      if (jclient_init (&instance->jclient, device->bus, device->address,
			gtk_spin_button_get_value_as_int (blocks_spin_button),
			gtk_spin_button_get_value_as_int
			(timeout_spin_button),
			gtk_drop_down_get_selected (quality_drop_down), -1))
	{
	  g_free (instance);
	  continue;
	}

      reporter = ow_resampler_get_reporter (instance->jclient.resampler);
      reporter->callback = (ow_resampler_report_t) set_report_data;
      reporter->data = instance;

      debug_print (1, "Adding %s...", instance->jclient.name);

      g_list_store_append (status_list_store,
			   overwitch_device_new (instance->jclient.name,
						 device->desc.name,
						 instance->jclient.bus,
						 instance->jclient.address,
						 instance));

      start_instance (instance);
      set_widgets_to_running_state (TRUE);
    }

  free (devices);
}

static gboolean
refresh_all_sourcefunc (gpointer data)
{
  refresh_all (NULL, NULL);
  return G_SOURCE_REMOVE;
}

static void
stop_all (GtkWidget *object, gpointer data)
{
  GListModel *model = G_LIST_MODEL (status_list_store);

  for (guint i = 0; i < g_list_model_get_n_items (model); i++)
    {
      OverwitchDevice *dev = g_list_model_get_item (model, i);
      struct overwitch_instance *instance = dev->instance;
      struct ow_resampler *resampler = instance->jclient.resampler;
      ow_resampler_status_t status = ow_resampler_get_status (resampler);
      if (status != OW_RESAMPLER_STATUS_ERROR)
	{
	  debug_print (1, "Stopping %s...", instance->jclient.name);
	  stop_instance (instance);
	}
      jclient_wait (&instance->jclient);
      jclient_destroy (&instance->jclient);
      g_free (instance);
    }

  g_list_store_remove_all (status_list_store);

  set_widgets_to_running_state (FALSE);

  set_dll_target_delay ();
}

// When under PipeWire, it is desirable to run Overwitch as a follower of the hardware driver.
// This could be achieved by setting the node.group property to the same value the hardware has but there are other ways.
// See https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/3612 for a thorough explanation of the need of this.
// As setting an environment variable does not need additional libraries, this is backwards compatible.

static void
set_pipewire_props ()
{
  if (pipewire_env_var_set)
    {
      debug_print (1, "%s was '%s' at launch. Ignoring user value '%s'...",
		   PIPEWIRE_PROPS_ENV_VAR, getenv (PIPEWIRE_PROPS_ENV_VAR),
		   pipewire_props);
      return;
    }

  if (pipewire_props)
    {
      debug_print (1, "Setting %s to '%s'...", PIPEWIRE_PROPS_ENV_VAR,
		   pipewire_props);
      setenv (PIPEWIRE_PROPS_ENV_VAR, pipewire_props, TRUE);
    }
  else
    {
      unsetenv (PIPEWIRE_PROPS_ENV_VAR);
    }
}

static void
overwitch_open_preferences (GSimpleAction *simple_action, GVariant *parameter,
			    gpointer data)
{
  GtkEntryBuffer *buf;

  buf = gtk_entry_get_buffer (GTK_ENTRY (pipewire_props_dialog_entry));
  gtk_entry_buffer_set_text (buf, pipewire_props ? pipewire_props : "", -1);
  gtk_widget_set_visible (preferences_window, TRUE);
}

static void
overwitch_close_preferences (GtkButton *self, gpointer data)
{
  gtk_widget_set_visible (preferences_window, FALSE);
}

static void
overwitch_save_preferences (GtkButton *self, gpointer data)
{
  const gchar *props;
  GtkEntryBuffer *buf;

  gtk_widget_set_visible (preferences_window, FALSE);

  buf = gtk_entry_get_buffer (GTK_ENTRY (pipewire_props_dialog_entry));
  props = gtk_entry_buffer_get_text (buf);

  g_free (pipewire_props);
  pipewire_props = strdup (props ? props : "");

  set_pipewire_props ();

  stop_all (NULL, NULL);
  usleep (PAUSE_TO_BE_NOTIFIED_USECS);	//Time to let the devices notify us.
  stop_control_client ();
  start_control_client ();
  g_idle_add (refresh_all_sourcefunc, NULL);
}

static void
overwitch_open_about (GSimpleAction *simple_action, GVariant *parameter,
		      gpointer data)
{
  gtk_widget_set_visible (GTK_WIDGET (about_dialog), TRUE);
}

static void
overwitch_exit ()
{
  debug_print (1, "Exiting Overwitch...");

  pthread_spin_lock (&lock);
  hotplug_running = 0;
  pthread_spin_unlock (&lock);

  stop_all (NULL, NULL);
  usleep (PAUSE_TO_BE_NOTIFIED_USECS);	//Time to let the devices notify us.
  while (g_main_context_pending (NULL))
    {
      g_main_context_iteration (NULL, FALSE);
    }
  stop_control_client ();
  save_preferences ();
  gtk_window_destroy (GTK_WINDOW (main_window));
}

static gboolean
overwitch_delete_window (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  overwitch_exit ();
  return FALSE;
}

const GActionEntry APP_ENTRIES[] = {
  {"refresh_at_startup", NULL, NULL, "false", NULL},
  {"show_all_columns", NULL, NULL, "false", overwitch_show_all_columns},
  {"open_preferences", overwitch_open_preferences, NULL, NULL, NULL},
  {"open_about", overwitch_open_about, NULL, NULL, NULL}
};

static void
overwitch_device_name_changed (GtkEditableLabel *label, GParamSpec *pspec,
			       GtkColumnViewCell *cell)
{
  if (!gtk_editable_label_get_editing (label))
    {
      OverwitchDevice *d =
	OVERWITCH_DEVICE (gtk_column_view_cell_get_item (cell));
      struct overwitch_instance *instance = d->instance;
      struct ow_engine *engine =
	ow_resampler_get_engine (instance->jclient.resampler);
      const char *new_name = gtk_editable_get_text (GTK_EDITABLE (label));
      const char *old_name = d->name;
      if (g_strcmp0 (new_name, old_name))
	{
	  debug_print (1, "Renaming device to '%s'...", new_name);
	  ow_engine_set_overbridge_name (engine, new_name);
	  stop_instance (instance);
	  usleep (PAUSE_TO_BE_NOTIFIED_USECS);
	  remove_stopped_instances ();
	  refresh_all (NULL, NULL);
	}
    }
}

static void
overwitch_build_ui ()
{
  gchar *thanks;
  static GtkBuilder *builder;
  GtkBuilderScope *scope = gtk_builder_cscope_new ();
  gtk_builder_cscope_add_callback (scope, overwitch_device_name_changed);

  builder = gtk_builder_new ();
  gtk_builder_set_scope (builder, scope);
  gtk_builder_add_from_file (builder, DATADIR "/overwitch.ui", NULL);
  g_object_unref (scope);

  main_window = GTK_WIDGET (gtk_builder_get_object (builder, "main_window"));

  preferences_window =
    GTK_WIDGET (gtk_builder_get_object (builder, "preferences_window"));
  pipewire_props_dialog_entry =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "pipewire_props_dialog_entry"));
  preferences_window_cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "preferences_window_cancel_button"));
  preferences_window_save_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "preferences_window_save_button"));

  about_dialog =
    GTK_ABOUT_DIALOG (gtk_builder_get_object (builder, "about_dialog"));
  gtk_about_dialog_set_version (about_dialog, PACKAGE_VERSION);

  if (g_file_get_contents (DATADIR "/THANKS", &thanks, NULL, NULL))
    {
      gchar *last_new_line = strrchr (thanks, '\n');
      *last_new_line = 0;
      gchar **lines = g_strsplit (thanks, "\n", 0);
      gtk_about_dialog_add_credit_section (about_dialog,
					   _("Acknowledgements"),
					   (const gchar **) lines);
      g_free (thanks);
      g_strfreev (lines);
    }

  blocks_spin_button =
    GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "blocks_spin_button"));
  timeout_spin_button =
    GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "timeout_spin_button"));
  quality_drop_down =
    GTK_DROP_DOWN (gtk_builder_get_object (builder, "quality_drop_down"));

  refresh_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "refresh_button"));
  stop_button = GTK_WIDGET (gtk_builder_get_object (builder, "stop_button"));

  status_list_store =
    G_LIST_STORE (gtk_builder_get_object (builder, "status_list_store"));

  name_cell_renderer =
    GTK_CELL_RENDERER_TEXT (gtk_builder_get_object
			    (builder, "name_cell_renderer"));
  device_column =
    GTK_COLUMN_VIEW_COLUMN (gtk_builder_get_object
			    (builder, "device_column"));
  bus_column =
    GTK_COLUMN_VIEW_COLUMN (gtk_builder_get_object (builder, "bus_column"));
  address_column =
    GTK_COLUMN_VIEW_COLUMN (gtk_builder_get_object
			    (builder, "address_column"));
  o2j_ratio_column =
    GTK_COLUMN_VIEW_COLUMN (gtk_builder_get_object
			    (builder, "o2j_ratio_column"));
  j2o_ratio_column =
    GTK_COLUMN_VIEW_COLUMN (gtk_builder_get_object
			    (builder, "j2o_ratio_column"));

  jack_status_label =
    GTK_LABEL (gtk_builder_get_object (builder, "jack_status_label"));
  target_delay_label =
    GTK_LABEL (gtk_builder_get_object (builder, "target_delay_label"));

  g_signal_connect (main_window, "close-request",
		    G_CALLBACK (overwitch_delete_window), NULL);

  g_signal_connect (preferences_window_cancel_button, "clicked",
		    G_CALLBACK (overwitch_close_preferences), NULL);
  g_signal_connect (preferences_window_save_button, "clicked",
		    G_CALLBACK (overwitch_save_preferences), NULL);

  g_signal_connect (refresh_button, "clicked", G_CALLBACK (refresh_all),
		    NULL);
  g_signal_connect (stop_button, "clicked", G_CALLBACK (stop_all), NULL);

  g_action_map_add_action_entries (G_ACTION_MAP (app), APP_ENTRIES,
				   G_N_ELEMENTS (APP_ENTRIES), app);

  g_object_unref (builder);
}

static void
hotplug_callback (uint8_t bus, uint8_t address)
{
  g_idle_add (refresh_all_sourcefunc, NULL);
}

static void *
hotplug_runner (void *data)
{
  hotplug_running = 1;
  ow_hotplug_loop (&hotplug_running, &lock, hotplug_callback);
  return NULL;
}

static void
overwitch_startup (GApplication *gapp, gpointer *user_data)
{
  gboolean refresh_at_startup;
  GVariant *v;
  GAction *a;

  overwitch_build_ui ();

  gtk_application_add_window (app, GTK_WINDOW (main_window));

  load_preferences ();

  pipewire_env_var_set = getenv (PIPEWIRE_PROPS_ENV_VAR) != NULL;
  set_pipewire_props ();

  a = g_action_map_lookup_action (G_ACTION_MAP (app), "refresh_at_startup");
  v = g_action_get_state (a);

  g_variant_get (v, "b", &refresh_at_startup);

  start_control_client ();
  if (refresh_at_startup)
    {
      refresh_all (NULL, NULL);
    }

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
overwitch_activate (GApplication *gapp, gpointer *user_data)
{
  gtk_window_present (GTK_WINDOW (main_window));
}

static void
signal_handler (int signum)
{
  overwitch_exit ();
}

gint
main (gint argc, gchar *argv[])
{
  gint status;
  struct sigaction action;

  action.sa_handler = signal_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGTSTP, &action, NULL);

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  pthread_spin_init (&lock, PTHREAD_PROCESS_PRIVATE);

  app = gtk_application_new (PACKAGE_SERVICE_NAME,
			     G_APPLICATION_DEFAULT_FLAGS);

  g_signal_connect (app, "startup", G_CALLBACK (overwitch_startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (overwitch_activate), NULL);

  g_application_add_main_option_entries (G_APPLICATION (app), CMD_PARAMS);

  status = g_application_run (G_APPLICATION (app), argc, argv);

  g_object_unref (app);

  if (hotplug_thread)
    {
      pthread_join (hotplug_thread, NULL);
    }

  pthread_spin_destroy (&lock);

  return status;
}
