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

#define CONF_FILE "/preferences.json"

#define CONF_REFRESH_AT_STARTUP "refreshAtStartup"
#define CONF_SHOW_ALL_COLUMNS "showAllColumns"
#define CONF_BLOCKS "blocks"
#define CONF_QUALITY "quality"
#define CONF_TIMEOUT "timeout"
#define CONF_PIPEWIRE_PROPS "pipewireProps"

#define PIPEWIRE_PROPS_ENVV "PIPEWIRE_PROPS"

#define PAUSE_TO_BE_NOTIFIED_USECS (jack_sample_rate ? jack_buffer_size * 10 * 1000000 / (gdouble) jack_sample_rate : 0)

enum list_store_columns
{
  STATUS_LIST_STORE_STATUS,
  STATUS_LIST_STORE_NAME,
  STATUS_LIST_STORE_DEVICE,
  STATUS_LIST_STORE_BUS,
  STATUS_LIST_STORE_ADDRESS,
  STATUS_LIST_STORE_O2J_LATENCY,
  STATUS_LIST_STORE_J2O_LATENCY,
  STATUS_LIST_STORE_O2J_RATIO,
  STATUS_LIST_STORE_J2O_RATIO,
  STATUS_LIST_STORE_INSTANCE
};

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
static gboolean pipewire_venv_set;

static GtkWidget *main_window;
static GtkAboutDialog *about_dialog;
static GtkDialog *preferences_dialog;
static GtkWidget *pipewire_props_dialog_entry;
static GtkWidget *about_button;
static GtkWidget *refresh_at_startup_button;
static GtkWidget *show_all_columns_button;
static GtkWidget *preferences_button;
static GtkWidget *refresh_button;
static GtkWidget *stop_button;
static GtkSpinButton *blocks_spin_button;
static GtkSpinButton *timeout_spin_button;
static GtkComboBox *quality_combo_box;
static GtkCellRendererText *name_cell_renderer;
static GtkTreeViewColumn *device_column;
static GtkTreeViewColumn *bus_column;
static GtkTreeViewColumn *address_column;
static GtkTreeViewColumn *o2j_ratio_column;
static GtkTreeViewColumn *j2o_ratio_column;
static GtkListStore *status_list_store;
static GtkLabel *jack_status_label;
static GtkPopover *main_popover;

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
start_instance (struct overwitch_instance *instance)
{
  struct ow_resampler *resampler = instance->jclient.resampler;
  struct ow_engine *engine = ow_resampler_get_engine (resampler);
  debug_print (1, "Starting %s...", ow_engine_get_overbridge_name (engine));
  jclient_start (&instance->jclient);
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
  const char *status_name;
  GtkTreeIter iter;
  gint bus, address;
  gboolean valid, editing;
  GtkTreeModel *model = GTK_TREE_MODEL (status_list_store);

  g_object_get (G_OBJECT (name_cell_renderer), "editing", &editing, NULL);

  if (editing)
    {
      return FALSE;
    }

  valid = gtk_tree_model_get_iter_first (model, &iter);

  while (valid)
    {
      gtk_tree_model_get (model, &iter, STATUS_LIST_STORE_BUS, &bus,
			  STATUS_LIST_STORE_ADDRESS, &address, -1);

      if (instance->jclient.bus == bus
	  && instance->jclient.address == address)
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
	  status_name = get_status_string (status);

	  gtk_list_store_set (status_list_store, &iter,
			      STATUS_LIST_STORE_STATUS,
			      status_name,
			      STATUS_LIST_STORE_O2J_LATENCY,
			      o2j_latency_s,
			      STATUS_LIST_STORE_J2O_LATENCY,
			      j2o_latency_s,
			      STATUS_LIST_STORE_O2J_RATIO,
			      instance->o2j_ratio,
			      STATUS_LIST_STORE_J2O_RATIO,
			      instance->j2o_ratio, -1);

	  break;
	}

      valid = gtk_tree_model_iter_next (model, &iter);
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
overwitch_cleanup_jack (GtkWidget *object, gpointer data)
{
  gtk_dialog_run (GTK_DIALOG (about_dialog));
  gtk_widget_hide (GTK_WIDGET (about_dialog));
}

static void
update_all_metrics (gboolean active)
{
  gtk_tree_view_column_set_visible (device_column, active);
  gtk_tree_view_column_set_visible (bus_column, active);
  gtk_tree_view_column_set_visible (address_column, active);
  gtk_tree_view_column_set_visible (o2j_ratio_column, active);
  gtk_tree_view_column_set_visible (j2o_ratio_column, active);
}

static void
refresh_at_startup (GtkWidget *object, gpointer data)
{
  gboolean active;

  g_object_get (G_OBJECT (refresh_at_startup_button), "active", &active,
		NULL);
  active = !active;
  g_object_set (G_OBJECT (refresh_at_startup_button), "active", active, NULL);
  gtk_widget_hide (GTK_WIDGET (main_popover));
}

static void
show_all_columns (GtkWidget *object, gpointer data)
{
  gboolean active;

  g_object_get (G_OBJECT (show_all_columns_button), "active", &active, NULL);
  active = !active;
  g_object_set (G_OBJECT (show_all_columns_button), "active", active, NULL);
  update_all_metrics (active);

  gtk_widget_hide (GTK_WIDGET (main_popover));
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
  g_object_get (G_OBJECT (refresh_at_startup_button), "active",
		&refresh_at_startup, NULL);
  json_builder_add_boolean_value (builder, refresh_at_startup);

  json_builder_set_member_name (builder, CONF_SHOW_ALL_COLUMNS);
  g_object_get (G_OBJECT (show_all_columns_button), "active",
		&show_all_columns, NULL);
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
			      gtk_combo_box_get_active (quality_combo_box));

  json_builder_set_member_name (builder, CONF_QUALITY);
  json_builder_add_int_value (builder,
			      gtk_combo_box_get_active (quality_combo_box));

  json_builder_set_member_name (builder, CONF_PIPEWIRE_PROPS);
  json_builder_add_string_value (builder, pipewire_props);

  json_builder_end_object (builder);

  gen = json_generator_new ();
  root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
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
  g_object_set (G_OBJECT (refresh_at_startup_button), "active",
		refresh_at_startup, NULL);
  g_object_set (G_OBJECT (show_all_columns_button), "active",
		show_all_columns, NULL);
  gtk_spin_button_set_value (blocks_spin_button, blocks);
  gtk_spin_button_set_value (timeout_spin_button, timeout);
  gtk_combo_box_set_active (quality_combo_box, quality);
  update_all_metrics (show_all_columns);
}

static gboolean
is_device_at_bus_address (uint8_t bus, uint8_t address, GtkTreeIter *iter,
			  struct overwitch_instance **instance)
{
  guint dev_bus, dev_address;
  GtkTreeModel *model = GTK_TREE_MODEL (status_list_store);
  gboolean valid = gtk_tree_model_get_iter_first (model, iter);

  while (valid)
    {
      gtk_tree_model_get (model, iter, STATUS_LIST_STORE_INSTANCE, instance,
			  STATUS_LIST_STORE_BUS, &dev_bus,
			  STATUS_LIST_STORE_ADDRESS, &dev_address, -1);

      if (dev_bus == bus && dev_address == address)
	{
	  return TRUE;
	}

      valid = gtk_tree_model_iter_next (model, iter);
    }

  return FALSE;
}

static void
set_widgets_to_running_state (gboolean running)
{
  gtk_widget_set_sensitive (stop_button, running);
  gtk_widget_set_sensitive (GTK_WIDGET (blocks_spin_button), !running);
  gtk_widget_set_sensitive (GTK_WIDGET (timeout_spin_button), !running);
  gtk_widget_set_sensitive (GTK_WIDGET (quality_combo_box), !running);
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
  GtkTreeIter iter;
  ow_resampler_status_t status;
  struct overwitch_instance *instance;
  GtkTreeModel *model = GTK_TREE_MODEL (status_list_store);
  gboolean valid = gtk_tree_model_get_iter_first (model, &iter);

  while (valid)
    {
      gtk_tree_model_get (model, &iter, STATUS_LIST_STORE_INSTANCE, &instance,
			  -1);
      status = ow_resampler_get_status (instance->jclient.resampler);
      if (status <= OW_RESAMPLER_STATUS_STOP)
	{
	  jclient_wait (&instance->jclient);
	  jclient_destroy (&instance->jclient);
	  g_free (instance);
	  valid = gtk_list_store_remove (status_list_store, &iter);
	}
      else
	{
	  valid = gtk_tree_model_iter_next (model, &iter);
	}
    }
}

static void
refresh_all (GtkWidget *object, gpointer data)
{
  struct ow_usb_device *devices, *device;
  struct ow_resampler_reporter *reporter;
  struct overwitch_instance *instance;
  size_t devices_count;
  const char *status_string;
  ow_resampler_status_t status;
  ow_err_t err;

  remove_stopped_instances ();

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
      if (is_device_at_bus_address (device->bus, device->address, &iter,
				    &instance))
	{
	  debug_print (2, "%s already running. Skipping...",
		       instance->jclient.name);
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
	gtk_combo_box_get_active (quality_combo_box);
      instance->jclient.priority = -1;

      instance->latency.o2h = 0.0;
      instance->latency.h2o = 0.0;
      instance->o2j_ratio = 1.0;
      instance->j2o_ratio = 1.0;

      //Needed because jclient_init will momentarily block the GUI.
      while (gtk_events_pending ())
	{
	  gtk_main_iteration ();
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

      status = ow_resampler_get_status (instance->jclient.resampler);
      status_string = get_status_string (status);

      gtk_list_store_insert_with_values (status_list_store, NULL, -1,
					 STATUS_LIST_STORE_STATUS,
					 status_string,
					 STATUS_LIST_STORE_NAME,
					 instance->jclient.name,
					 STATUS_LIST_STORE_DEVICE,
					 device->desc.name,
					 STATUS_LIST_STORE_BUS,
					 instance->jclient.bus,
					 STATUS_LIST_STORE_ADDRESS,
					 instance->jclient.address,
					 STATUS_LIST_STORE_O2J_LATENCY, "",
					 STATUS_LIST_STORE_J2O_LATENCY, "",
					 STATUS_LIST_STORE_O2J_RATIO,
					 instance->o2j_ratio,
					 STATUS_LIST_STORE_J2O_RATIO,
					 instance->j2o_ratio,
					 STATUS_LIST_STORE_INSTANCE, instance,
					 -1);

      start_instance (instance);
      set_widgets_to_running_state (TRUE);
    }

  ow_free_usb_device_list (devices, devices_count);
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
  GtkTreeIter iter;
  ow_resampler_status_t status;
  struct overwitch_instance *instance;
  GtkTreeModel *model = GTK_TREE_MODEL (status_list_store);
  gboolean valid = gtk_tree_model_get_iter_first (model, &iter);

  while (valid)
    {
      gtk_tree_model_get (model, &iter, STATUS_LIST_STORE_INSTANCE, &instance,
			  -1);
      status = ow_resampler_get_status (instance->jclient.resampler);
      if (status != OW_RESAMPLER_STATUS_ERROR)
	{
	  debug_print (1, "Stopping %s...", instance->jclient.name);
	  stop_instance (instance);
	}
      jclient_wait (&instance->jclient);
      jclient_destroy (&instance->jclient);
      g_free (instance);
      valid = gtk_list_store_remove (status_list_store, &iter);
    }

  set_widgets_to_running_state (FALSE);
}

static void
set_overbridge_name (GtkCellRendererText *self,
		     gchar *path, gchar *name, gpointer user_data)
{
  struct overwitch_instance *instance;
  struct ow_engine *engine;
  GtkTreeIter iter;
  gint row = atoi (path);
  GtkTreeModel *model = GTK_TREE_MODEL (status_list_store);
  gboolean valid = gtk_tree_model_get_iter_first (model, &iter);
  gint i = 0;

  while (valid && i < row)
    {
      valid = gtk_tree_model_iter_next (model, &iter);
      i++;
    }

  if (valid)
    {
      gtk_tree_model_get (model, &iter, STATUS_LIST_STORE_INSTANCE, &instance,
			  -1);

      engine = ow_resampler_get_engine (instance->jclient.resampler);
      ow_engine_set_overbridge_name (engine, name);

      stop_instance (instance);

      if (!gtk_tree_model_iter_n_children (model, NULL))
	{
	  set_widgets_to_running_state (FALSE);
	}

      usleep (PAUSE_TO_BE_NOTIFIED_USECS);	//Time to let the device notify us.
      g_idle_add (refresh_all_sourcefunc, NULL);
    }
}

// When under PipeWire, it is desirable to run Overwitch as a follower of the hardware driver.
// This could be achieved by setting the node.group property to the same value the hardware has but there are other ways.
// See https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/3612 for a thorough explanation of the need of this.
// As setting an environment variable does not need additional libraries, this is backwards compatible.

static void
set_pipewire_props ()
{
  if (pipewire_venv_set)
    {
      debug_print (1, "%s was '%s' at launch. Ignoring user value '%s'...",
		   PIPEWIRE_PROPS_ENVV, getenv (PIPEWIRE_PROPS_ENVV),
		   pipewire_props);
      return;
    }

  if (pipewire_props)
    {
      debug_print (1, "Setting %s to '%s'...", PIPEWIRE_PROPS_ENVV,
		   pipewire_props);
      setenv (PIPEWIRE_PROPS_ENVV, pipewire_props, TRUE);
    }
  else
    {
      unsetenv (PIPEWIRE_PROPS_ENVV);
    }
}

static void
open_preferences (GtkWidget *object, gpointer data)
{
  const gchar *props;
  gint res;
  gtk_widget_hide (GTK_WIDGET (main_popover));

  gtk_entry_set_text (GTK_ENTRY (pipewire_props_dialog_entry),
		      pipewire_props ? pipewire_props : "");
  res = gtk_dialog_run (GTK_DIALOG (preferences_dialog));
  if (res == GTK_RESPONSE_ACCEPT)
    {
      props = gtk_entry_get_text (GTK_ENTRY (pipewire_props_dialog_entry));
      g_free (pipewire_props);
      pipewire_props = strlen (props) ? strdup (props) : NULL;
      set_pipewire_props ();
      stop_all (NULL, NULL);
      usleep (PAUSE_TO_BE_NOTIFIED_USECS);	//Time to let the devices notify us.
      stop_control_client ();
      start_control_client ();
      g_idle_add (refresh_all_sourcefunc, NULL);
    }
  gtk_widget_hide (GTK_WIDGET (preferences_dialog));
}

static void
quit (int signo)
{
  stop_all (NULL, NULL);
  usleep (PAUSE_TO_BE_NOTIFIED_USECS);	//Time to let the devices notify us.
  while (gtk_events_pending ())
    {
      gtk_main_iteration ();
    }
  stop_control_client ();
  save_preferences ();
  debug_print (1, "Quitting GTK+...");
  gtk_main_quit ();
}

static gboolean
overwitch_delete_window (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  quit (0);
  return FALSE;
}

int
main (int argc, char *argv[])
{
  int opt, long_index = 0;
  int vflg = 0, errflg = 0;
  GtkBuilder *builder;
  struct sigaction action;
  gboolean refresh;

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

  action.sa_handler = quit;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);
  sigaction (SIGTSTP, &action, NULL);

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  gtk_init (&argc, &argv);
  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, DATADIR "/gui.glade", NULL);

  main_window = GTK_WIDGET (gtk_builder_get_object (builder, "main_window"));
  gtk_window_resize (GTK_WINDOW (main_window), 1, 1);	//Compact window

  preferences_dialog =
    GTK_DIALOG (gtk_builder_get_object (builder, "preferences_dialog"));
  pipewire_props_dialog_entry =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "pipewire_props_dialog_entry"));

  about_dialog =
    GTK_ABOUT_DIALOG (gtk_builder_get_object (builder, "about_dialog"));
  gtk_about_dialog_set_version (about_dialog, PACKAGE_VERSION);

  refresh_at_startup_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "refresh_at_startup_button"));
  show_all_columns_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "show_all_columns_button"));
  preferences_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "preferences_button"));
  about_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "about_button"));

  blocks_spin_button =
    GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "blocks_spin_button"));
  timeout_spin_button =
    GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "timeout_spin_button"));
  quality_combo_box =
    GTK_COMBO_BOX (gtk_builder_get_object (builder, "quality_combo_box"));

  refresh_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "refresh_button"));
  stop_button = GTK_WIDGET (gtk_builder_get_object (builder, "stop_button"));

  status_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "status_list_store"));

  name_cell_renderer =
    GTK_CELL_RENDERER_TEXT (gtk_builder_get_object
			    (builder, "name_cell_renderer"));
  device_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object (builder, "device_column"));
  bus_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object (builder, "bus_column"));
  address_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object (builder, "address_column"));
  o2j_ratio_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "o2j_ratio_column"));
  j2o_ratio_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "j2o_ratio_column"));

  jack_status_label =
    GTK_LABEL (gtk_builder_get_object (builder, "jack_status_label"));

  main_popover =
    GTK_POPOVER (gtk_builder_get_object (builder, "main_popover"));
  gtk_popover_set_constrain_to (main_popover, GTK_POPOVER_CONSTRAINT_NONE);

  g_object_set (G_OBJECT (refresh_at_startup_button), "role",
		GTK_BUTTON_ROLE_CHECK, NULL);
  g_object_set (G_OBJECT (show_all_columns_button), "role",
		GTK_BUTTON_ROLE_CHECK, NULL);

  g_signal_connect (about_button, "clicked",
		    G_CALLBACK (overwitch_cleanup_jack), NULL);

  g_signal_connect (refresh_at_startup_button, "clicked",
		    G_CALLBACK (refresh_at_startup), NULL);

  g_signal_connect (show_all_columns_button, "clicked",
		    G_CALLBACK (show_all_columns), NULL);

  g_signal_connect (preferences_button, "clicked",
		    G_CALLBACK (open_preferences), NULL);

  g_signal_connect (refresh_button, "clicked", G_CALLBACK (refresh_all),
		    NULL);

  g_signal_connect (stop_button, "clicked", G_CALLBACK (stop_all), NULL);

  g_signal_connect (name_cell_renderer, "edited",
		    G_CALLBACK (set_overbridge_name), NULL);

  g_signal_connect (main_window, "delete-event",
		    G_CALLBACK (overwitch_delete_window), NULL);

  load_preferences ();

  g_object_get (G_OBJECT (refresh_at_startup_button), "active", &refresh,
		NULL);

  pipewire_venv_set = getenv (PIPEWIRE_PROPS_ENVV) != NULL;
  set_pipewire_props ();

  start_control_client ();
  if (refresh)
    {
      refresh_all (NULL, NULL);
    }

  gtk_widget_show (main_window);
  gtk_main ();

  return 0;
}
