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
#define _GNU_SOURCE
#include "../config.h"
#include "common.h"
#include "overwitch.h"
#include "jclient.h"
#include "utils.h"

enum status_list_store_columns
{
  STATUS_LIST_STORE_DEVICE,
  STATUS_LIST_STORE_BUS,
  STATUS_LIST_STORE_ADDRESS,
  STATUS_LIST_STORE_NAME,
  STATUS_LIST_STORE_O2J_LATENCY,
  STATUS_LIST_STORE_J2O_LATENCY,
  STATUS_LIST_STORE_O2J_RATIO,
  STATUS_LIST_STORE_J2O_RATIO
};

struct overwitch_instance
{
  int device;
  pthread_t thread;
  double o2j_latency;
  double j2o_latency;
  double o2j_ratio;
  double j2o_ratio;
  struct jclient jclient;
};

static struct overwitch_instance **instances;
static ssize_t instance_count;

static GtkWidget *main_window;
static GtkAboutDialog *about_dialog;

static GtkWidget *about_button;

static GtkListStore *status_list_store;

static gboolean
set_overwith_instance_metrics (struct overwitch_instance *instance)
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
  debug_print (1, "Updating data for device %d...\n", instance->device);
  instance->o2j_latency = o2j_latency;
  instance->j2o_latency = j2o_latency;
  instance->o2j_ratio = o2j_ratio;
  instance->j2o_ratio = j2o_ratio;
  g_idle_add ((GSourceFunc) set_overwith_instance_metrics, instance);
}

static void
overwitch_show_about (GtkWidget * object, gpointer data)
{
  gtk_dialog_run (GTK_DIALOG (about_dialog));
  gtk_widget_hide (GTK_WIDGET (about_dialog));
}

static int
overwitch_run ()
{
  struct ow_usb_device *devices;
  struct ow_usb_device *device;
  ow_err_t err = ow_get_devices (&devices, &instance_count);

  if (err)
    {
      return err;
    }

  if (!instance_count)
    {
      return EXIT_SUCCESS;
    }

  device = devices;

  instances = malloc (sizeof (struct overwitch_instance *) * instance_count);
  for (int i = 0; i < instance_count; i++)
    {
      debug_print (1,
		   "Creating client for device %d (bus %03d, address %03d)...\n",
		   i, device->bus, device->address);

      instances[i] = malloc (sizeof (struct overwitch_instance));
      instances[i]->jclient.bus = device->bus;
      instances[i]->jclient.address = device->address;
      instances[i]->jclient.blocks_per_transfer = 4;
      instances[i]->jclient.quality = 2;
      instances[i]->jclient.priority = -1;
      instances[i]->jclient.reporter.callback =
	(ow_resampler_report_t) set_report_data;
      instances[i]->jclient.reporter.data = instances[i];
      instances[i]->jclient.reporter.period = -1;
      instances[i]->device = i;
      instances[i]->o2j_latency = 0.0;
      instances[i]->j2o_latency = 0.0;
      instances[i]->o2j_ratio = 1.0;
      instances[i]->j2o_ratio = 1.0;

      gtk_list_store_insert_with_values (status_list_store, NULL, -1,
					 STATUS_LIST_STORE_DEVICE, i,
					 STATUS_LIST_STORE_BUS, device->bus,
					 STATUS_LIST_STORE_ADDRESS,
					 device->address,
					 STATUS_LIST_STORE_NAME,
					 device->desc->name,
					 STATUS_LIST_STORE_O2J_LATENCY,
					 instances[i]->o2j_latency,
					 STATUS_LIST_STORE_J2O_LATENCY,
					 instances[i]->j2o_latency,
					 STATUS_LIST_STORE_O2J_RATIO,
					 instances[i]->o2j_ratio,
					 STATUS_LIST_STORE_J2O_RATIO,
					 instances[i]->j2o_ratio, -1);
    }

  ow_free_usb_device_list (devices, instance_count);

  pthread_create (&instances[0]->thread, NULL, jclient_run_thread,
		  &instances[0]->jclient);

  return EXIT_SUCCESS;
}

static void
overwitch_stop ()
{
  for (int i = 0; i < instance_count; i++)
    {
      jclient_exit (&instances[i]->jclient);
      pthread_join (instances[0]->thread, NULL);
      free (instances[0]);
    }
  free (instances);
  instances = NULL;
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

  status_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "status_list_store"));

  g_signal_connect (about_button, "clicked",
		    G_CALLBACK (overwitch_show_about), NULL);

  g_signal_connect (main_window, "delete-event",
		    G_CALLBACK (overwitch_delete_window), NULL);

  if (overwitch_run ())
    {
      goto end;
    }

  gtk_widget_show (main_window);
  gtk_main ();

end:
  return 0;
}
