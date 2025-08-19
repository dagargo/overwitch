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
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <glib/gi18n.h>
#include "common.h"
#include "utils.h"
#include "preferences.h"
#include "message.h"

#define REFRESH_TIMEOUT_MS 2000

#define PLAY_IMAGE_NAME "media-playback-start-symbolic"
#define STOP_IMAGE_NAME "media-playback-stop-symbolic"

static GDBusConnection *connection;

static gint devices;
static guint source_id;

static GtkApplication *app;

static GtkWidget *main_window;
static GtkAboutDialog *about_dialog;
static GtkWidget *preferences_window;
static GtkWidget *preferences_window_cancel_button;
static GtkWidget *preferences_window_save_button;
static GtkWidget *pipewire_props_dialog_entry;
static GtkWidget *start_stop_button;
static GtkSpinButton *blocks_spin_button;
static GtkSpinButton *timeout_spin_button;
static GtkDropDown *quality_drop_down;
static GtkColumnViewColumn *device_column;
static GtkColumnViewColumn *bus_column;
static GtkColumnViewColumn *address_column;
static GtkColumnViewColumn *o2j_ratio_column;
static GtkColumnViewColumn *j2o_ratio_column;
static GListStore *status_list_store;
static GtkLabel *jack_status_label;
static GtkLabel *target_delay_label;

static void control_service (const gchar * method);

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
  struct ow_preferences prefs;
  GtkEntryBuffer *buf;
  const gchar *props;
  GVariant *v;
  GAction *a;

  a = g_action_map_lookup_action (G_ACTION_MAP (app), "show_all_columns");
  v = g_action_get_state (a);
  g_variant_get (v, "b", &prefs.show_all_columns);

  prefs.blocks = gtk_spin_button_get_value_as_int (blocks_spin_button);
  prefs.timeout = gtk_spin_button_get_value_as_int (timeout_spin_button);
  prefs.quality = gtk_drop_down_get_selected (quality_drop_down);

  buf = gtk_entry_get_buffer (GTK_ENTRY (pipewire_props_dialog_entry));
  props = gtk_entry_buffer_get_text (buf);
  prefs.pipewire_props = strdup (props);

  ow_save_preferences (&prefs);
  g_free (prefs.pipewire_props);
}

static void
load_preferences ()
{
  struct ow_preferences prefs;
  GtkEntryBuffer *buf;
  GVariant *v;
  GAction *a;

  ow_load_preferences (&prefs);

  a = g_action_map_lookup_action (G_ACTION_MAP (app), "show_all_columns");
  v = g_variant_new_boolean (prefs.show_all_columns);
  g_action_change_state (a, v);

  gtk_spin_button_set_value (blocks_spin_button, prefs.blocks);
  gtk_spin_button_set_value (timeout_spin_button, prefs.timeout);
  gtk_drop_down_set_selected (quality_drop_down, prefs.quality);

  buf = gtk_entry_get_buffer (GTK_ENTRY (pipewire_props_dialog_entry));
  gtk_entry_buffer_set_text (buf, prefs.pipewire_props, -1);
  g_free (prefs.pipewire_props);

  update_all_metrics (prefs.show_all_columns);
}

static void
set_widgets_to_running_state ()
{
  gtk_button_set_icon_name (GTK_BUTTON (start_stop_button),
			    devices > 0 ? STOP_IMAGE_NAME : PLAY_IMAGE_NAME);
  gtk_widget_set_tooltip_text (start_stop_button,
			       devices > 0 ? _("Stop All Devices") :
			       _("Start All Devices"));
  gtk_widget_set_sensitive (start_stop_button, devices >= 0);
}

static void
set_service_state (guint32 samplerate, guint32 buffer_size,
		   gdouble target_delay_ms)
{
  static char msg[OW_LABEL_MAX_LEN];
  if (devices > 0)
    {
      snprintf (msg, OW_LABEL_MAX_LEN, _("JACK at %.5g kHz, %u period"),
		samplerate / 1000.f, buffer_size);
      gtk_label_set_text (jack_status_label, msg);

      snprintf (msg, OW_LABEL_MAX_LEN, _("Target latency: %.1f ms"),
		target_delay_ms);
      gtk_label_set_text (target_delay_label, msg);
    }
  else
    {
      gtk_label_set_text (jack_status_label, "");
      gtk_label_set_text (target_delay_label, "");
    }
}

static void
open_preferences (GSimpleAction *simple_action, GVariant *parameter,
		  gpointer data)
{
  load_preferences ();

  gtk_widget_set_visible (preferences_window, TRUE);
}

static void
close_preferences (GtkButton *self, gpointer data)
{
  gtk_widget_set_visible (preferences_window, FALSE);
}

static void
click_save_preferences (GtkButton *self, gpointer data)
{
  gtk_widget_set_visible (preferences_window, FALSE);

  save_preferences ();

  control_service ("Start");	//Stop, reload and start
}

static void
open_about (GSimpleAction *simple_action, GVariant *parameter, gpointer data)
{
  gtk_widget_set_visible (GTK_WIDGET (about_dialog), TRUE);
}

static void
app_exit ()
{
  debug_print (1, "Exiting Overwitch...");

  save_preferences ();		//Needed for preference show_all_columns.

  if (source_id)
    {
      g_source_remove (source_id);
    }

  gtk_window_destroy (GTK_WINDOW (main_window));
}

static gboolean
delete_main_window (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  app_exit ();
  return FALSE;
}

static void
set_state (const gchar *state)
{
  OverwitchDevice *device;
  guint udevices;
  guint32 samplerate, buffer_size;
  double target_delay_ms;

  JsonReader *reader = message_state_reader_start (state, &udevices);
  if (reader == NULL)
    {
      devices = -1;
      return;
    }

  devices = udevices;

  g_list_store_remove_all (status_list_store);

  for (guint i = 0; i < devices; i++)
    {
      device = message_state_reader_get_device (reader, i);
      if (device)
	{
	  g_list_store_append (status_list_store, device);
	}
    }

  message_state_reader_end (reader, &samplerate, &buffer_size,
			    &target_delay_ms);

  set_service_state (samplerate, buffer_size, target_delay_ms);
}

static gboolean
refresh_state (gpointer data)
{
  GVariant *result;
  const gchar *state;
  GError *error = NULL;

  result = g_dbus_connection_call_sync (connection,
					PACKAGE_SERVICE_DBUS_NAME,
					"/io/github/dagargo/OverwitchService",
					PACKAGE_SERVICE_DBUS_NAME,
					"GetState", NULL,
					G_VARIANT_TYPE ("(s)"),
					G_DBUS_CALL_FLAGS_NONE, -1, NULL,
					&error);

  if (error == NULL)
    {
      g_variant_get (result, "(&s)", &state);
      debug_print (1, "State: %s", state);
      set_state (state);
      g_variant_unref (result);
    }
  else
    {
      devices = -1;
      error_print ("Error calling method 'GetState': %s", error->message);
      g_error_free (error);
    }

  set_widgets_to_running_state ();

  return G_SOURCE_CONTINUE;
}

static void
device_name_changed (GtkEditableLabel *label, GParamSpec *pspec,
		     GtkColumnViewCell *cell)
{
  if (gtk_editable_label_get_editing (label))
    {
      g_source_remove (source_id);
    }
  else
    {
      OverwitchDevice *d =
	OVERWITCH_DEVICE (gtk_column_view_cell_get_item (cell));
      GVariant *result;
      GError *error = NULL;
      const char *new_name = gtk_editable_get_text (GTK_EDITABLE (label));
      const char *old_name = d->name;

      if (g_strcmp0 (new_name, old_name))
	{
	  result = g_dbus_connection_call_sync (connection,
						PACKAGE_SERVICE_DBUS_NAME,
						"/io/github/dagargo/OverwitchService",
						PACKAGE_SERVICE_DBUS_NAME,
						"SetDeviceName",
						g_variant_new ("(us)", d->id,
							       new_name),
						G_VARIANT_TYPE ("(i)"),
						G_DBUS_CALL_FLAGS_NONE, -1,
						NULL, &error);

	  if (error == NULL)
	    {
	      gint err;
	      g_variant_get (result, "(i)", &err);
	      debug_print (1, "Err: %d", err);
	      g_variant_unref (result);
	    }
	  else
	    {
	      error_print ("Error calling method 'SetDeviceName': %s",
			   error->message);
	      g_error_free (error);
	    }
	}

      source_id = g_timeout_add (REFRESH_TIMEOUT_MS, refresh_state, NULL);
    }
}

static void
control_service (const gchar *method)
{
  GError *error = NULL;
  GVariant *result;
  result = g_dbus_connection_call_sync (connection,
					PACKAGE_SERVICE_DBUS_NAME,
					"/io/github/dagargo/OverwitchService",
					PACKAGE_SERVICE_DBUS_NAME,
					method, NULL, NULL,
					G_DBUS_CALL_FLAGS_NONE, -1, NULL,
					&error);

  if (error == NULL)
    {
      g_variant_unref (result);
    }
  else
    {
      error_print ("Error calling method '%s': %s", method, error->message);
      g_error_free (error);
    }
}

static void
click_start_stop (GtkWidget *object, gpointer data)
{
  control_service (devices > 0 ? "Stop" : "Start");
}

static void
exit_service_and_exit (GSimpleAction *simple_action, GVariant *parameter,
		       gpointer data)
{
  control_service ("Exit");
  app_exit ();
}

const GActionEntry APP_ENTRIES[] = {
  {"show_all_columns", NULL, NULL, "false", overwitch_show_all_columns},
  {"open_preferences", open_preferences, NULL, NULL, NULL},
  {"open_about", open_about, NULL, NULL, NULL},
  {"exit", exit_service_and_exit, NULL, NULL, NULL}
};

static void
build_ui ()
{
  gchar *thanks;
  GtkBuilder *builder;
  GtkBuilderScope *scope = gtk_builder_cscope_new ();
  gtk_builder_cscope_add_callback (scope, device_name_changed);

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

  start_stop_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "start_stop_button"));

  status_list_store =
    G_LIST_STORE (gtk_builder_get_object (builder, "status_list_store"));

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
		    G_CALLBACK (delete_main_window), NULL);

  g_signal_connect (preferences_window_cancel_button, "clicked",
		    G_CALLBACK (close_preferences), NULL);
  g_signal_connect (preferences_window_save_button, "clicked",
		    G_CALLBACK (click_save_preferences), NULL);

  g_signal_connect (start_stop_button, "clicked",
		    G_CALLBACK (click_start_stop), NULL);

  g_action_map_add_action_entries (G_ACTION_MAP (app), APP_ENTRIES,
				   G_N_ELEMENTS (APP_ENTRIES), app);

  g_object_unref (builder);
}

static void
app_startup (GApplication *gapp, gpointer *user_data)
{
  GError *error = NULL;

  build_ui ();

  gtk_application_add_window (app, GTK_WINDOW (main_window));

  load_preferences ();

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (error == NULL)
    {
      refresh_state (NULL);	// If the D-Bus service is not started, it will be started. In this case, it will fail to get the state.
      source_id = g_timeout_add (REFRESH_TIMEOUT_MS, refresh_state, NULL);
    }
  else
    {
      error_print ("Error connecting to the bus: %s", error->message);
      g_error_free (error);
    }
}

static void
app_activate (GApplication *gapp, gpointer *user_data)
{
  gtk_window_present (GTK_WINDOW (main_window));
}

static void
app_shutdown (GApplication *gapp, gpointer *user_data)
{
  g_object_unref (connection);
}

static void
signal_handler (int signum)
{
  app_exit ();
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

  app = gtk_application_new (PACKAGE_APP_DBUS_NAME,
			     G_APPLICATION_DEFAULT_FLAGS);

  g_signal_connect (app, "startup", G_CALLBACK (app_startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (app_activate), NULL);
  g_signal_connect (app, "shutdown", G_CALLBACK (app_shutdown), NULL);

  g_application_add_main_option_entries (G_APPLICATION (app), CMD_PARAMS);

  status = g_application_run (G_APPLICATION (app), argc, argv);

  g_object_unref (app);

  return status;
}
