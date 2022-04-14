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

#include <libusb.h>
#include <string.h>
#include <sched.h>
#include <gtk/gtk.h>
#include <jack/jack.h>
#define _GNU_SOURCE
#include "../config.h"
#include "common.h"
#include "overwitch.h"
#include "jclient.h"
#include "utils.h"

#define MSG_JACK_SERVER_FOUND "JACK server found"
#define MSG_NO_JACK_SERVER_FOUND "No JACK server found"

enum list_store_columns
{
  STATUS_LIST_STORE_NAME,
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
  gdouble o2j_ratio;
  gdouble j2o_ratio;
  struct jclient jclient;
  const struct ow_device_desc *device_desc;
};

static GtkWidget *main_window;
static GtkAboutDialog *about_dialog;
static GtkWidget *about_button;
static GtkWidget *refresh_button;
static GtkListStore *status_list_store;
static GtkStatusbar *status_bar;

static void
start_instance (struct overwitch_instance *instance)
{
  debug_print (1, "Starting %s...\n",
	       ow_resampler_get_name (instance->jclient.resampler));
  pthread_create (&instance->thread, NULL, jclient_run_thread,
		  &instance->jclient);
}

static void
stop_instance (struct overwitch_instance *instance)
{
  debug_print (1, "Stopping %s...\n",
	       ow_resampler_get_name (instance->jclient.resampler));
  jclient_exit (&instance->jclient);
}

static gboolean
set_overwitch_instance_metrics (struct overwitch_instance *instance)
{
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
	  gtk_list_store_set (status_list_store, &iter,
			      STATUS_LIST_STORE_O2J_LATENCY,
			      instance->o2j_latency,
			      STATUS_LIST_STORE_J2O_LATENCY,
			      instance->j2o_latency,
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
set_report_data (struct overwitch_instance *instance, double o2j_latency,
		 double j2o_latency, double o2j_ratio, double j2o_ratio)
{
  instance->o2j_latency = o2j_latency;
  instance->j2o_latency = j2o_latency;
  instance->o2j_ratio = o2j_ratio;
  instance->j2o_ratio = j2o_ratio;
  g_idle_add ((GSourceFunc) set_overwitch_instance_metrics, instance);
}

static void
overwitch_show_about (GtkWidget * object, gpointer data)
{
  gtk_dialog_run (GTK_DIALOG (about_dialog));
  gtk_widget_hide (GTK_WIDGET (about_dialog));
}

static gboolean
overwitch_instance_running (uint8_t bus, uint8_t address, const char **name)
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
overwitch_check_jack_server ()
{
  jack_client_t *client;
  jack_status_t status;

  gtk_statusbar_pop (status_bar, 0);

  client = jack_client_open ("Overwitch control client", JackNoStartServer,
			     &status, NULL);
  if (client)
    {
      gtk_statusbar_push (status_bar, 0, MSG_JACK_SERVER_FOUND);
      jack_client_close (client);
      return TRUE;
    }
  else
    {
      gtk_statusbar_push (status_bar, 0, MSG_NO_JACK_SERVER_FOUND);
      return FALSE;
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

  overwitch_check_jack_server ();

  return FALSE;
}

static void
remove_jclient (uint8_t bus, uint8_t address)
{
  guint * id = g_malloc (sizeof(guint));
  *id = (bus << 8) + address;
  g_idle_add ((GSourceFunc) remove_jclient_bg, id);
}

static void
overwitch_refresh_devices (GtkWidget * object, gpointer data)
{
  struct ow_usb_device *devices, *device;
  struct overwitch_instance *instance;
  size_t devices_count;
  const char *name;
  ow_err_t err;

  if (!overwitch_check_jack_server ())
    {
      return;
    }

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
      if (overwitch_instance_running (device->bus, device->address, &name))
	{
	  debug_print (2, "%s already running. Skipping...\n", name);
	  continue;
	}

      instance = malloc (sizeof (struct overwitch_instance));
      instance->jclient.bus = device->bus;
      instance->jclient.address = device->address;
      instance->jclient.blocks_per_transfer = 4;
      instance->jclient.quality = 2;
      instance->jclient.priority = -1;
      instance->jclient.reporter.callback =
	(ow_resampler_report_t) set_report_data;
      instance->jclient.reporter.data = instance;
      instance->jclient.reporter.period = -1;
      instance->jclient.end_notifier = remove_jclient;
      instance->o2j_latency = 0.0;
      instance->j2o_latency = 0.0;
      instance->o2j_ratio = 1.0;
      instance->j2o_ratio = 1.0;
      instance->device_desc = device->desc;

      if (jclient_init (&instance->jclient))
	{
	  free (instance);
	  continue;
	}

      debug_print (1, "Adding %s...\n",
		   ow_resampler_get_name (instance->jclient.resampler));

      gtk_list_store_insert_with_values (status_list_store, NULL, -1,
					 STATUS_LIST_STORE_NAME,
					 device->desc->name,
					 STATUS_LIST_STORE_BUS,
					 instance->jclient.bus,
					 STATUS_LIST_STORE_ADDRESS,
					 instance->jclient.address,
					 STATUS_LIST_STORE_O2J_LATENCY,
					 instance->o2j_latency,
					 STATUS_LIST_STORE_J2O_LATENCY,
					 instance->j2o_latency,
					 STATUS_LIST_STORE_O2J_RATIO,
					 instance->o2j_ratio,
					 STATUS_LIST_STORE_J2O_RATIO,
					 instance->j2o_ratio,
					 STATUS_LIST_STORE_INSTANCE, instance,
					 -1);

      start_instance (instance);
    }

  ow_free_usb_device_list (devices, devices_count);
}

static void
overwitch_run ()
{
  overwitch_refresh_devices (NULL, NULL);
}

static void
overwitch_stop ()
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
      gtk_list_store_remove (status_list_store, &iter);

      valid =
	gtk_tree_model_get_iter_first (GTK_TREE_MODEL (status_list_store),
				       &iter);
    }
}

static void
overwitch_quit ()
{
  overwitch_stop ();
  debug_print (1, "Quitting GTK+...\n");
  gtk_main_quit ();
}

static gboolean
overwitch_delete_window (GtkWidget * widget, GdkEvent * event, gpointer data)
{
  overwitch_quit ();
  return FALSE;
}

int
main (int argc, char *argv[])
{
  GtkBuilder *builder;
  char *glade_file = malloc (PATH_MAX);

  debug_level = 1;

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

  about_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "about_button"));

  refresh_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "refresh_button"));

  status_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "status_list_store"));

  status_bar = GTK_STATUSBAR (gtk_builder_get_object (builder, "status_bar"));
  gtk_statusbar_push (status_bar, 0, MSG_NO_JACK_SERVER_FOUND);

  g_signal_connect (about_button, "clicked",
		    G_CALLBACK (overwitch_show_about), NULL);

  g_signal_connect (refresh_button, "clicked",
		    G_CALLBACK (overwitch_refresh_devices), NULL);

  g_signal_connect (main_window, "delete-event",
		    G_CALLBACK (overwitch_delete_window), NULL);

  overwitch_run ();

  gtk_widget_show (main_window);
  gtk_main ();

  return 0;
}
