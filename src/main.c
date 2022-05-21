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
#include <wordexp.h>
#define _GNU_SOURCE
#include "../config.h"
#include "common.h"
#include "overwitch.h"
#include "jclient.h"
#include "utils.h"

#define MSG_JACK_SERVER_FOUND "JACK server found"
#define MSG_NO_JACK_SERVER_FOUND "No JACK server found"

#define CONF_DIR "~/.config/overwitch"
#define CONF_FILE "/preferences.json"

#define CONF_REFRESH_AT_STARTUP "refreshAtStartup"
#define CONF_SHOW_ALL_COLUMNS "showAllColumns"
#define CONF_BLOCKS "blocks"
#define CONF_QUALITY "quality"

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
  pthread_t thread;
  gdouble o2j_latency;
  gdouble j2o_latency;
  gdouble o2j_latency_max;
  gdouble j2o_latency_max;
  gdouble o2j_ratio;
  gdouble j2o_ratio;
  struct jclient jclient;
  const struct ow_device_desc *device_desc;
};

typedef void (*check_jack_server_callback_t) ();

static GtkWidget *main_window;
static GtkAboutDialog *about_dialog;
static GtkWidget *about_button;
static GtkWidget *refresh_at_startup_button;
static GtkWidget *show_all_columns_button;
static GtkWidget *refresh_button;
static GtkWidget *stop_button;
static GtkSpinButton *blocks_spin_button;
static GtkComboBox *quality_combo_box;
static GtkTreeViewColumn *device_column;
static GtkTreeViewColumn *bus_column;
static GtkTreeViewColumn *address_column;
static GtkTreeViewColumn *o2j_ratio_column;
static GtkTreeViewColumn *j2o_ratio_column;
static GtkListStore *status_list_store;
static GtkStatusbar *status_bar;
static GtkPopover *main_popover;

static GThread *jack_control_client_thread;

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
      return "Error";
    case OW_RESAMPLER_STATUS_STOP:
      return "Stopped";
    case OW_RESAMPLER_STATUS_READY:
      return "Ready";
    case OW_RESAMPLER_STATUS_BOOT:
      return "Booting";
    case OW_RESAMPLER_STATUS_TUNE:
      return "Tunning";
    case OW_RESAMPLER_STATUS_RUN:
      return "Running";
    }
  return NULL;
}

static void
start_instance (struct overwitch_instance *instance)
{
  debug_print (1, "Starting %s...\n",
	       ow_resampler_get_overbridge_name (instance->
						 jclient.resampler));
  pthread_create (&instance->thread, NULL, jclient_run_thread,
		  &instance->jclient);
}

static void
stop_instance (struct overwitch_instance *instance)
{
  debug_print (1, "Stopping %s...\n",
	       ow_resampler_get_overbridge_name (instance->
						 jclient.resampler));
  jclient_exit (&instance->jclient);
  pthread_join (instance->thread, NULL);
}

static gboolean
set_overwitch_instance_status (struct overwitch_instance *instance)
{
  static char o2j_latency_s[OW_LABEL_MAX_LEN];
  static char j2o_latency_s[OW_LABEL_MAX_LEN];
  const char *status;
  GtkTreeIter iter;
  gint bus, address;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (status_list_store), &iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (status_list_store), &iter,
			  STATUS_LIST_STORE_BUS, &bus,
			  STATUS_LIST_STORE_ADDRESS, &address, -1);

      if (instance->jclient.bus == bus
	  && instance->jclient.address == address)
	{
	  if (instance->o2j_latency >= 0)
	    {
	      g_snprintf (o2j_latency_s, OW_LABEL_MAX_LEN,
			  "%.1f ms (max. %.1f ms)", instance->o2j_latency,
			  instance->o2j_latency_max);
	    }
	  else
	    {
	      o2j_latency_s[0] = '\0';
	    }

	  if (instance->j2o_latency >= 0)
	    {
	      g_snprintf (j2o_latency_s, OW_LABEL_MAX_LEN,
			  "%.1f ms (max. %.1f ms)", instance->j2o_latency,
			  instance->j2o_latency_max);
	    }
	  else
	    {
	      j2o_latency_s[0] = '\0';
	    }

	  status =
	    get_status_string (ow_resampler_get_status
			       (instance->jclient.resampler));

	  gtk_list_store_set (status_list_store, &iter,
			      STATUS_LIST_STORE_STATUS,
			      status,
			      STATUS_LIST_STORE_NAME,
			      instance->jclient.name,
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

      valid =
	gtk_tree_model_iter_next (GTK_TREE_MODEL (status_list_store), &iter);
    }

  return FALSE;
}

static void
set_report_data (struct overwitch_instance *instance,
		 double o2j_latency, double j2o_latency,
		 double o2j_latency_max, double j2o_latency_max,
		 double o2j_ratio, double j2o_ratio)
{
  instance->o2j_latency = o2j_latency;
  instance->j2o_latency = j2o_latency;
  instance->o2j_latency_max = o2j_latency_max;
  instance->j2o_latency_max = j2o_latency_max;
  instance->o2j_ratio = o2j_ratio;
  instance->j2o_ratio = j2o_ratio;
  g_idle_add ((GSourceFunc) set_overwitch_instance_status, instance);
}

static void
overwitch_cleanup_jack (GtkWidget * object, gpointer data)
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
refresh_at_startup (GtkWidget * object, gpointer data)
{
  gboolean active;

  g_object_get (G_OBJECT (refresh_at_startup_button), "active", &active,
		NULL);
  active = !active;
  g_object_set (G_OBJECT (refresh_at_startup_button), "active", active, NULL);
  gtk_widget_hide (GTK_WIDGET (main_popover));
}

static void
show_all_columns (GtkWidget * object, gpointer data)
{
  gboolean active;

  g_object_get (G_OBJECT (show_all_columns_button), "active", &active, NULL);
  active = !active;
  g_object_set (G_OBJECT (show_all_columns_button), "active", active, NULL);
  update_all_metrics (active);

  gtk_widget_hide (GTK_WIDGET (main_popover));
}

gchar *
get_expanded_dir (const char *exp)
{
  wordexp_t exp_result;
  size_t n;
  gchar *exp_dir = malloc (PATH_MAX);

  wordexp (exp, &exp_result, 0);
  n = PATH_MAX - 1;
  strncpy (exp_dir, exp_result.we_wordv[0], n);
  exp_dir[PATH_MAX - 1] = 0;
  wordfree (&exp_result);

  return exp_dir;
}

gint
save_file_char (const gchar * path, const guint8 * data, ssize_t len)
{
  gint res;
  long bytes;
  FILE *file;

  file = fopen (path, "w");

  if (!file)
    {
      return -errno;
    }

  debug_print (1, "Saving file %s...\n", path);

  res = 0;
  bytes = fwrite (data, 1, len, file);
  if (bytes == len)
    {
      debug_print (1, "%zu bytes written\n", bytes);
    }
  else
    {
      error_print ("Error while writing to file %s\n", path);
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
      error_print ("Error wile creating dir `%s'\n", CONF_DIR);
      return;
    }

  n = PATH_MAX - strlen (preferences_path) - 1;
  strncat (preferences_path, CONF_FILE, n);
  preferences_path[PATH_MAX - 1] = 0;

  debug_print (1, "Saving preferences to '%s'...\n", preferences_path);

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

  json_builder_set_member_name (builder, CONF_QUALITY);
  json_builder_add_int_value (builder,
			      gtk_combo_box_get_active (quality_combo_box));


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

  error = NULL;
  json_parser_load_from_file (parser, preferences_file, &error);
  if (error)
    {
      error_print ("Error wile loading preferences from `%s': %s\n",
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

  if (json_reader_read_member (reader, CONF_QUALITY))
    {
      quality = json_reader_get_int_value (reader);
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
  gtk_combo_box_set_active (quality_combo_box, quality);
  update_all_metrics (show_all_columns);
}

static gboolean
is_device_at_bus_address_running (uint8_t bus, uint8_t address,
				  const char **name)
{
  guint dev_bus, dev_address;
  GtkTreeIter iter;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (status_list_store), &iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (status_list_store), &iter,
			  STATUS_LIST_STORE_NAME, name,
			  STATUS_LIST_STORE_BUS, &dev_bus,
			  STATUS_LIST_STORE_ADDRESS, &dev_address, -1);

      if (dev_bus == bus && dev_address == address)
	{
	  return TRUE;
	}

      valid =
	gtk_tree_model_iter_next (GTK_TREE_MODEL (status_list_store), &iter);
    }

  return FALSE;
}

static gboolean
check_jack_server_free (gpointer data)
{
  const char *msg;
  gboolean *status;
  check_jack_server_callback_t callback = data;

  status = g_thread_join (jack_control_client_thread);
  jack_control_client_thread = NULL;

  if (*status)
    {
      msg = MSG_JACK_SERVER_FOUND;
      if (callback)
	{
	  callback ();
	}
    }
  else
    {
      msg = MSG_NO_JACK_SERVER_FOUND;
      gtk_widget_set_sensitive (stop_button, FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (blocks_spin_button), TRUE);
      gtk_widget_set_sensitive (GTK_WIDGET (quality_combo_box), TRUE);
    }
  gtk_statusbar_push (status_bar, 0, msg);

  g_free (status);

  return G_SOURCE_REMOVE;
}

static gpointer
check_jack_server (gpointer callback)
{
  jack_status_t foo;
  jack_client_t *client;
  gboolean *status = g_malloc (sizeof (gboolean));

  gtk_statusbar_pop (status_bar, 0);

  client = jack_client_open ("Overwitch control client", JackNoStartServer,
			     &foo, NULL);
  if (client)
    {
      *status = TRUE;
      jack_client_close (client);
    }
  else
    {
      *status = FALSE;
    }

  g_idle_add (check_jack_server_free, callback);

  return status;
}

static void
check_jack_server_bg (check_jack_server_callback_t callback)
{
  if (!jack_control_client_thread)
    {
      jack_control_client_thread = g_thread_new ("Overwitch control client",
						 check_jack_server, callback);
    }
}

static gboolean
remove_jclient_bg (guint * id)
{
  struct overwitch_instance *instance;
  GtkTreeIter iter;
  uint8_t bus = *id >> 8;
  uint8_t address = 0xff & *id;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (status_list_store), &iter);

  g_free (id);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (status_list_store), &iter,
			  STATUS_LIST_STORE_INSTANCE, &instance, -1);

      if (instance->jclient.bus == bus
	  && instance->jclient.address == address)
	{
	  pthread_join (instance->thread, NULL);
	  gtk_list_store_remove (status_list_store, &iter);
	  break;
	}

      valid =
	gtk_tree_model_iter_next (GTK_TREE_MODEL (status_list_store), &iter);
    }

  check_jack_server_bg (NULL);

  return FALSE;
}

static void
remove_jclient (uint8_t bus, uint8_t address)
{
  guint *id = g_malloc (sizeof (guint));
  *id = (bus << 8) + address;
  g_idle_add ((GSourceFunc) remove_jclient_bg, id);
}

static void
refresh_devices ()
{
  struct ow_usb_device *devices, *device;
  struct overwitch_instance *instance;
  size_t devices_count;
  const char *name, *status;
  ow_err_t err;

  err = ow_get_devices (&devices, &devices_count);
  if (err)
    {
      return;
    }

  if (!devices_count)
    {
      return;
    }

  device = devices;

  for (int i = 0; i < devices_count; i++, device++)
    {
      if (is_device_at_bus_address_running
	  (device->bus, device->address, &name))
	{
	  debug_print (2, "%s already running. Skipping...\n", name);
	  continue;
	}

      instance = g_malloc (sizeof (struct overwitch_instance));
      instance->jclient.bus = device->bus;
      instance->jclient.address = device->address;
      instance->jclient.priority = -1;
      instance->jclient.reporter.callback =
	(ow_resampler_report_t) set_report_data;
      instance->jclient.reporter.data = instance;
      instance->jclient.reporter.period = -1;
      instance->jclient.end_notifier = remove_jclient;
      instance->jclient.blocks_per_transfer =
	gtk_spin_button_get_value_as_int (blocks_spin_button);
      instance->jclient.quality =
	gtk_combo_box_get_active (quality_combo_box);
      instance->o2j_latency = 0.0;
      instance->j2o_latency = 0.0;
      instance->o2j_ratio = 1.0;
      instance->j2o_ratio = 1.0;
      instance->device_desc = device->desc;

      if (jclient_init (&instance->jclient))
	{
	  g_free (instance);
	  continue;
	}

      debug_print (1, "Adding %s...\n",
		   ow_resampler_get_overbridge_name (instance->
						     jclient.resampler));

      status =
	get_status_string (ow_resampler_get_status
			   (instance->jclient.resampler));

      gtk_list_store_insert_with_values (status_list_store, NULL, -1,
					 STATUS_LIST_STORE_STATUS,
					 status,
					 STATUS_LIST_STORE_NAME,
					 instance->jclient.name,
					 STATUS_LIST_STORE_DEVICE,
					 device->desc->name,
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

      set_report_data (instance, -1.0, -1.0, -1.0, -1.0, 1.0, 1.0);

      start_instance (instance);
      gtk_widget_set_sensitive (stop_button, TRUE);
      gtk_widget_set_sensitive (GTK_WIDGET (blocks_spin_button), FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (quality_combo_box), FALSE);
    }

  ow_free_usb_device_list (devices, devices_count);
}

static void
refresh_devices_click (GtkWidget * object, gpointer data)
{
  check_jack_server_bg (refresh_devices);
}

static void
stop_all (GtkWidget * object, gpointer data)
{
  struct overwitch_instance *instance;
  GtkTreeIter iter;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (status_list_store), &iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (status_list_store), &iter,
			  STATUS_LIST_STORE_INSTANCE, &instance, -1);

      stop_instance (instance);
      g_free (instance);
      gtk_list_store_remove (status_list_store, &iter);

      valid =
	gtk_tree_model_get_iter_first (GTK_TREE_MODEL (status_list_store),
				       &iter);
    }

  gtk_widget_set_sensitive (stop_button, FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (blocks_spin_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (quality_combo_box), TRUE);
}

static void
quit (int signo)
{
  stop_all (NULL, NULL);
  save_preferences ();
  debug_print (1, "Quitting GTK+...\n");
  gtk_main_quit ();
}

static gboolean
overwitch_delete_window (GtkWidget * widget, GdkEvent * event, gpointer data)
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
  char *glade_file = malloc (PATH_MAX);

  while ((opt = getopt_long (argc, argv, "vh",
			     options, &long_index)) != -1)
    {
      switch (opt)
	{
	case 'v':
	  vflg++;
	  break;
	case 'h':
	  print_help (argv[0], PACKAGE_STRING, options);
	  exit (EXIT_SUCCESS);
	case '?':
	  errflg++;
	}
    }

  if (errflg > 0)
    {
      print_help (argv[0], PACKAGE_STRING, options);
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

  if (snprintf
      (glade_file, PATH_MAX, "%s/%s/res/gui.glade", DATADIR,
       PACKAGE) >= PATH_MAX)
    {
      error_print ("Path too long\n");
      return -1;
    }

  gtk_init (&argc, &argv);
  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, glade_file, NULL);
  free (glade_file);

  main_window = GTK_WIDGET (gtk_builder_get_object (builder, "main_window"));
  gtk_window_resize (GTK_WINDOW (main_window), 1, 1);	//Compact window

  about_dialog =
    GTK_ABOUT_DIALOG (gtk_builder_get_object (builder, "about_dialog"));
  gtk_about_dialog_set_version (about_dialog, PACKAGE_VERSION);

  refresh_at_startup_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "refresh_at_startup_button"));
  show_all_columns_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "show_all_columns_button"));
  about_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "about_button"));

  blocks_spin_button =
    GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "blocks_spin_button"));
  quality_combo_box =
    GTK_COMBO_BOX (gtk_builder_get_object (builder, "quality_combo_box"));

  refresh_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "refresh_button"));
  stop_button = GTK_WIDGET (gtk_builder_get_object (builder, "stop_button"));

  status_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "status_list_store"));

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

  status_bar = GTK_STATUSBAR (gtk_builder_get_object (builder, "status_bar"));
  gtk_statusbar_push (status_bar, 0, MSG_NO_JACK_SERVER_FOUND);

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

  g_signal_connect (refresh_button, "clicked",
		    G_CALLBACK (refresh_devices_click), NULL);

  g_signal_connect (stop_button, "clicked", G_CALLBACK (stop_all), NULL);

  g_signal_connect (main_window, "delete-event",
		    G_CALLBACK (overwitch_delete_window), NULL);

  load_preferences ();

  g_object_get (G_OBJECT (refresh_at_startup_button), "active", &refresh,
		NULL);
  check_jack_server_bg (refresh ? refresh_devices : NULL);

  gtk_widget_show (main_window);
  gtk_main ();

  return 0;
}
