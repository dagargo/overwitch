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

#include <errno.h>
#include <libusb.h>
#include <string.h>
#include <sched.h>
#include <gtk/gtk.h>
#include <jack/jack.h>
#include <sys/stat.h>
#include <json-glib/json-glib.h>
#include <glib/gi18n.h>
#include "common.h"
#include "jclient.h"
#include "utils.h"
#include "overwitch_device.h"

#define CONF_FILE "/preferences.json"

#define CONF_REFRESH_AT_STARTUP "refreshAtStartup"
#define CONF_SHOW_ALL_COLUMNS "showAllColumns"
#define CONF_BLOCKS "blocks"
#define CONF_QUALITY "quality"
#define CONF_TIMEOUT "timeout"
#define CONF_PIPEWIRE_PROPS "pipewireProps"

#define PIPEWIRE_PROPS_ENV_VAR "PIPEWIRE_PROPS"

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
static GtkBuilder *builder;

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

static struct option options[] = {
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
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

gint
save_file_char (const gchar *path, const guint8 *data, ssize_t len)
{
  gint res;
  long bytes;
  FILE *file;

  file = fopen (path, "w");

  if (!file)
    {
      return -errno;
    }

  debug_print (1, "Saving file %s...", path);

  res = 0;
  bytes = fwrite (data, 1, len, file);
  if (bytes == len)
    {
      debug_print (1, "%zu bytes written", bytes);
    }
  else
    {
      error_print ("Error while writing to file %s", path);
      res = -EIO;
    }

  fclose (file);

  return res;
}

static void
save_preferences ()
{
  size_t n;
  gchar *preferences_path;
  JsonBuilder *builder;
  JsonGenerator *gen;
  JsonNode *root;
  gchar *json;
  gboolean show_all_columns, refresh_at_startup;
  GVariant *v;
  GAction *a;

  preferences_path = get_expanded_dir (CONF_DIR);
  if (g_mkdir_with_parents (preferences_path,
			    S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH |
			    S_IXOTH))
    {
      error_print ("Error wile creating dir `%s'", CONF_DIR);
      return;
    }

  n = PATH_MAX - strlen (preferences_path) - 1;
  strncat (preferences_path, CONF_FILE, n);
  preferences_path[PATH_MAX - 1] = 0;

  debug_print (1, "Saving preferences to '%s'...", preferences_path);

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, CONF_REFRESH_AT_STARTUP);
  a = g_action_map_lookup_action (G_ACTION_MAP (app), "refresh_at_startup");
  v = g_action_get_state (a);
  g_variant_get (v, "b", &refresh_at_startup);
  json_builder_add_boolean_value (builder, refresh_at_startup);

  json_builder_set_member_name (builder, CONF_SHOW_ALL_COLUMNS);
  a = g_action_map_lookup_action (G_ACTION_MAP (app), "show_all_columns");
  v = g_action_get_state (a);
  g_variant_get (v, "b", &show_all_columns);
  json_builder_add_boolean_value (builder, show_all_columns);

  json_builder_set_member_name (builder, CONF_BLOCKS);
  json_builder_add_int_value (builder,
			      gtk_spin_button_get_value_as_int
			      (blocks_spin_button));

  json_builder_set_member_name (builder, CONF_TIMEOUT);
  json_builder_add_int_value (builder,
			      gtk_spin_button_get_value_as_int
			      (timeout_spin_button));

  json_builder_set_member_name (builder, CONF_QUALITY);
  json_builder_add_int_value (builder,
			      gtk_drop_down_get_selected (quality_drop_down));

  json_builder_set_member_name (builder, CONF_PIPEWIRE_PROPS);
  json_builder_add_string_value (builder, pipewire_props);

  json_builder_end_object (builder);

  gen = json_generator_new ();
  root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
  json_generator_set_pretty (gen, TRUE);
  json = json_generator_to_data (gen, NULL);

  save_file_char (preferences_path, (guint8 *) json, strlen (json));

  g_free (json);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);
  g_free (preferences_path);
}

static void
load_preferences ()
{
  GError *error;
  JsonReader *reader;
  JsonParser *parser = json_parser_new ();
  gchar *preferences_file = get_expanded_dir (CONF_DIR CONF_FILE);
  gboolean show_all_columns = FALSE;
  gboolean refresh_at_startup = FALSE;
  gint64 blocks = 24;
  gint64 quality = 2;
  gint64 timeout = 10;
  GVariant *v;
  GAction *a;

  error = NULL;
  json_parser_load_from_file (parser, preferences_file, &error);
  if (error)
    {
      error_print ("Error wile loading preferences from `%s': %s",
		   CONF_DIR CONF_FILE, error->message);
      g_error_free (error);
      g_object_unref (parser);
      goto end;
    }

  reader = json_reader_new (json_parser_get_root (parser));

  if (json_reader_read_member (reader, CONF_REFRESH_AT_STARTUP))
    {
      refresh_at_startup = json_reader_get_boolean_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_SHOW_ALL_COLUMNS))
    {
      show_all_columns = json_reader_get_boolean_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_BLOCKS))
    {
      blocks = json_reader_get_int_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_TIMEOUT))
    {
      timeout = json_reader_get_int_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_QUALITY))
    {
      quality = json_reader_get_int_value (reader);
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, CONF_PIPEWIRE_PROPS))
    {
      const gchar *v = json_reader_get_string_value (reader);
      if (v && strlen (v))
	{
	  pipewire_props = strdup (v);
	}
    }
  json_reader_end_member (reader);

  g_object_unref (reader);
  g_object_unref (parser);

  g_free (preferences_file);

end:
  a = g_action_map_lookup_action (G_ACTION_MAP (app), "refresh_at_startup");
  v = g_variant_new_boolean (refresh_at_startup);
  g_action_change_state (a, v);

  a = g_action_map_lookup_action (G_ACTION_MAP (app), "show_all_columns");
  v = g_variant_new_boolean (show_all_columns);
  g_action_change_state (a, v);

  gtk_spin_button_set_value (blocks_spin_button, blocks);
  gtk_spin_button_set_value (timeout_spin_button, timeout);
  gtk_drop_down_set_selected (quality_drop_down, quality);
  update_all_metrics (show_all_columns);
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
      instance->jclient.bus = device->bus;
      instance->jclient.address = device->address;
      instance->jclient.blocks_per_transfer =
	gtk_spin_button_get_value_as_int (blocks_spin_button);
      instance->jclient.xfr_timeout =
	gtk_spin_button_get_value_as_int (timeout_spin_button);
      instance->jclient.quality =
	gtk_drop_down_get_selected (quality_drop_down);
      instance->jclient.priority = -1;

      instance->latency.o2h = 0.0;
      instance->latency.h2o = 0.0;
      instance->o2j_ratio = 1.0;
      instance->j2o_ratio = 1.0;

      //Needed because jclient_init will momentarily block the GUI.
      while (g_main_context_pending (NULL))
	{
	  g_main_context_iteration (NULL, FALSE);
	}

      if (jclient_init (&instance->jclient))
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
	}
    }
}

static void
overwitch_build_ui ()
{
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
}

static void
overwitch_activate (GApplication *gapp, gpointer *user_data)
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

  gtk_window_present (GTK_WINDOW (main_window));
}

static void
overwitch_free_ui ()
{
  g_object_unref (app);
  g_object_unref (builder);
}

static void
signal_handler (int signum)
{
  if (signum == SIGUSR1)
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
  else
    {
      overwitch_exit ();
    }
}

int
main (int argc, char *argv[])
{
  int status, opt, long_index = 0;
  int vflg = 0, errflg = 0;
  struct sigaction action;

  while ((opt = getopt_long (argc, argv, "vh", options, &long_index)) != -1)
    {
      switch (opt)
	{
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

  action.sa_handler = signal_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGTSTP, &action, NULL);
  sigaction (SIGUSR1, &action, NULL);
  sigaction (SIGUSR2, &action, NULL);

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  gtk_init ();

  app = gtk_application_new ("io.github.dagargo.Overwitch",
			     G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect (app, "activate", G_CALLBACK (overwitch_activate), NULL);

  status = g_application_run (G_APPLICATION (app), 0, NULL);
  overwitch_free_ui ();

  return status;
}
