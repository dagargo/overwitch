/*
 *   overwitch.c
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
#include <errno.h>
#include "overwitch.h"
#include "utils.h"

#define DEVICES_DIR "/devices.d"
#define DEVICES_FILE "/devices.json"

#define DEV_TAG_PID "pid"
#define DEV_TAG_NAME "name"
#define DEV_TAG_TYPE "type"
#define DEV_TAG_INPUT_TRACKS "input_tracks"
#define DEV_TAG_OUTPUT_TRACKS "output_tracks"
#define DEV_TAG_TRACK_NAME "name"
#define DEV_TAG_TRACK_SIZE "size"

static int ow_get_device_desc (uint16_t, struct ow_device_desc *);

int
ow_get_device_list (struct ow_device **ow_devices, size_t *size)
{
  int err;
  uint8_t bus, address;
  libusb_device **usb_device;
  struct libusb_device_descriptor desc;
  struct ow_device *ow_device;
  ssize_t total = 0;
  libusb_context *context = NULL;
  libusb_device **usb_devices = NULL;

  if (libusb_init (&context) != LIBUSB_SUCCESS)
    {
      return 1;
    }

  *size = 0;

  total = libusb_get_device_list (context, &usb_devices);
  if (!total)
    {
      *ow_devices = NULL;
      return 0;
    }

  *ow_devices = malloc (sizeof (struct ow_device) * total);
  ow_device = *ow_devices;
  usb_device = usb_devices;
  for (int i = 0; i < total; i++, usb_device++)
    {
      err = libusb_get_device_descriptor (*usb_device, &desc);
      if (err)
	{
	  error_print ("Error while getting USB device description: %s",
		       libusb_error_name (err));
	  continue;
	}

      if (desc.idVendor != ELEKTRON_VID)
	{
	  debug_print (3, "Non Elektron USB device found. Skipping...");
	  continue;
	}

      if (!ow_get_device_desc (desc.idProduct, &ow_device->desc))
	{
	  bus = libusb_get_bus_number (*usb_device);
	  address = libusb_get_device_address (*usb_device);
	  debug_print (1, "Found %s (bus %03d, address %03d, ID %04x:%04x)",
		       ow_device->desc.name, bus, address, desc.idVendor,
		       desc.idProduct);
	  ow_device->vid = desc.idVendor;
	  ow_device->pid = desc.idProduct;
	  ow_device->bus = bus;
	  ow_device->address = address;
	  ow_device++;
	  (*size)++;
	}
    }

  if (!*size)
    {
      free (*ow_devices);
      *ow_devices = NULL;
    }

  libusb_free_device_list (usb_devices, total);
  libusb_exit (context);
  return 0;
}

void
ow_copy_device_desc (struct ow_device_desc *device_desc,
		     const struct ow_device_desc *d)
{
  device_desc->pid = d->pid;
  strncpy (device_desc->name, d->name, OW_LABEL_MAX_LEN);
  device_desc->type = d->type;
  device_desc->inputs = d->inputs;
  device_desc->outputs = d->outputs;

  for (int i = 0; i < device_desc->inputs; i++)
    {
      strncpy (device_desc->input_tracks[i].name, d->input_tracks[i].name,
	       OW_LABEL_MAX_LEN);
      device_desc->input_tracks[i].size = d->input_tracks[i].size;
    }

  for (int i = 0; i < device_desc->outputs; i++)
    {
      strncpy (device_desc->output_tracks[i].name, d->output_tracks[i].name,
	       OW_LABEL_MAX_LEN);
      device_desc->output_tracks[i].size = d->output_tracks[i].size;
    }
}

static int
ow_get_device_desc_reader (uint16_t pid, struct ow_device_desc *device_desc,
			   JsonReader *reader)
{
  gint dpid;

  device_desc->inputs = 0;
  device_desc->outputs = 0;

  if (!json_reader_read_member (reader, DEV_TAG_PID))
    {
      error_print ("Cannot read member '%s'", DEV_TAG_PID);
      return -ENODEV;
    }

  dpid = json_reader_get_int_value (reader);
  json_reader_end_member (reader);
  if (dpid != pid)
    {
      return -ENODEV;
    }
  device_desc->pid = dpid;

  debug_print (1, "Device with PID %d found", dpid);

  if (!json_reader_read_member (reader, DEV_TAG_NAME))
    {
      error_print ("Cannot read member '%s'", DEV_TAG_NAME);
      json_reader_end_element (reader);
      return -EINVAL;
    }
  snprintf (device_desc->name, OW_LABEL_MAX_LEN, "%s",
	    json_reader_get_string_value (reader));
  json_reader_end_member (reader);

  if (!json_reader_read_member (reader, DEV_TAG_TYPE))
    {
      error_print ("Cannot read member '%s'", DEV_TAG_TYPE);
      return -EINVAL;
    }
  device_desc->type = json_reader_get_int_value (reader);
  json_reader_end_member (reader);

  if (device_desc->type < OW_DEVICE_TYPE_1 ||
      device_desc->type > OW_DEVICE_TYPE_3)
    {
      error_print ("Invalid type version '%d'", device_desc->type);
      return -EINVAL;
    }

  if (!json_reader_read_member (reader, DEV_TAG_INPUT_TRACKS))
    {
      error_print ("Cannot read member '%s'", DEV_TAG_INPUT_TRACKS);
      json_reader_end_element (reader);
      return -EINVAL;
    }
  if (!json_reader_is_array (reader))
    {
      error_print ("Not an array");
      json_reader_end_element (reader);
      return -EINVAL;
    }
  device_desc->inputs = json_reader_count_elements (reader);
  if (!device_desc->inputs)
    {
      debug_print (1, "No tracks found");
      return -EINVAL;
    }

  for (int j = 0; j < device_desc->inputs; j++)
    {
      if (json_reader_read_element (reader, j))
	{
	  if (!json_reader_read_member (reader, DEV_TAG_TRACK_NAME))
	    {
	      debug_print (1, "No name found");
	      return -EINVAL;
	    }
	  snprintf (device_desc->input_tracks[j].name, OW_LABEL_MAX_LEN,
		    "%s", json_reader_get_string_value (reader));

	  json_reader_end_member (reader);

	  if (!json_reader_read_member (reader, DEV_TAG_TRACK_SIZE))
	    {
	      debug_print (1, "No size found");
	      return -EINVAL;
	    }
	  device_desc->input_tracks[j].size =
	    json_reader_get_int_value (reader);

	  json_reader_end_member (reader);

	  json_reader_end_element (reader);
	}
      else
	{
	  error_print ("Cannot read input track name %d", j);
	  return -EINVAL;
	}
    }
  json_reader_end_member (reader);

  if (!json_reader_read_member (reader, DEV_TAG_OUTPUT_TRACKS))
    {
      error_print ("Cannot read member '%s'", DEV_TAG_OUTPUT_TRACKS);
      json_reader_end_element (reader);
      return -EINVAL;
    }
  if (!json_reader_is_array (reader))
    {
      error_print ("Not an array");
      return -EINVAL;
    }
  device_desc->outputs = json_reader_count_elements (reader);
  if (!device_desc->outputs)
    {
      debug_print (1, "No tracks found");
      return -EINVAL;
    }

  for (int j = 0; j < device_desc->outputs; j++)
    {
      if (json_reader_read_element (reader, j))
	{
	  if (!json_reader_read_member (reader, DEV_TAG_TRACK_NAME))
	    {
	      debug_print (1, "No name found");
	      return -EINVAL;
	    }
	  snprintf (device_desc->output_tracks[j].name, OW_LABEL_MAX_LEN,
		    "%s", json_reader_get_string_value (reader));

	  json_reader_end_member (reader);

	  if (!json_reader_read_member (reader, DEV_TAG_TRACK_SIZE))
	    {
	      debug_print (1, "No size found");
	      return -EINVAL;
	    }
	  device_desc->output_tracks[j].size =
	    json_reader_get_int_value (reader);

	  json_reader_end_member (reader);

	  json_reader_end_element (reader);
	}
      else
	{
	  error_print ("Cannot read output track name %d", j);
	  return -EINVAL;
	}
    }
  json_reader_end_member (reader);

  return 0;
}

static int
ow_get_device_desc_file (uint16_t pid, struct ow_device_desc *device_desc,
			 const char *file, int array)
{
  gint err, devices;
  JsonParser *parser;
  JsonReader *reader;
  GError *error = NULL;

  device_desc->inputs = 0;
  device_desc->outputs = 0;

  debug_print (1, "Searching device in %s", file);

  parser = json_parser_new_immutable ();

  if (!json_parser_load_from_file (parser, file, &error))
    {
      debug_print (1, "%s", error->message);
      g_clear_error (&error);
      err = -ENODEV;
      goto cleanup_parser;
    }

  reader = json_reader_new (json_parser_get_root (parser));
  if (!reader)
    {
      error_print ("Unable to read from parser");
      err = -ENODEV;
      goto cleanup_parser;
    }

  if (array)
    {
      if (!json_reader_is_array (reader))
	{
	  error_print ("Not an array");
	  err = -ENODEV;
	  goto cleanup_reader;
	}

      devices = json_reader_count_elements (reader);
      if (!devices)
	{
	  debug_print (1, "No devices found");
	  err = -ENODEV;
	  goto cleanup_reader;
	}

      err = -ENODEV;
      for (int i = 0; i < devices; i++)
	{
	  if (!json_reader_read_element (reader, i))
	    {
	      error_print ("Cannot read element %d. Continuing...", i);
	      continue;
	    }

	  err = ow_get_device_desc_reader (pid, device_desc, reader);
	  if (err == -ENODEV)
	    {
	      json_reader_end_element (reader);
	      continue;
	    }
	  else
	    {
	      break;
	    }
	}
    }
  else
    {
      err = ow_get_device_desc_reader (pid, device_desc, reader);
    }

cleanup_reader:
  g_object_unref (reader);
cleanup_parser:
  g_object_unref (parser);
  return err;
}

static int
ow_get_device_desc (uint16_t pid, struct ow_device_desc *device_desc)
{
  char *file, *dir;
  int err;

  dir = get_expanded_dir (CONF_DIR DEVICES_DIR);

  err = -ENODEV;
  if (g_file_test (dir, G_FILE_TEST_IS_DIR))
    {
      GDir *gdir;
      const gchar *name;

      if ((gdir = g_dir_open (dir, 0, NULL)) != NULL)
	{
	  while ((name = g_dir_read_name (gdir)) != NULL)
	    {
	      if (name[0] == '.')
		{
		  continue;
		}

	      if (!g_str_has_suffix (name, ".json"))
		{
		  continue;
		}

	      file = g_build_path (G_DIR_SEPARATOR_S, dir, name, NULL);
	      if (g_file_test (file, G_FILE_TEST_IS_REGULAR))
		{
		  err = ow_get_device_desc_file (pid, device_desc, file, 0);
		}
	      g_free (file);
	    }
	}
    }
  g_free (dir);

  if (err)
    {
      file = get_expanded_dir (CONF_DIR DEVICES_FILE);
      err = ow_get_device_desc_file (pid, device_desc, file, 1);
      g_free (file);
    }

  if (err)
    {
      file = strdup (DATADIR DEVICES_FILE);
      err = ow_get_device_desc_file (pid, device_desc, file, 1);
      g_free (file);
    }

  return err;
}

int
ow_get_device_from_device_attrs (int device_num, const char *device_name,
				 uint8_t bus, uint8_t address,
				 struct ow_device **dev)
{
  int i;
  size_t total;
  struct ow_device *devices;
  struct ow_device *device;
  ow_err_t err = ow_get_device_list (&devices, &total);

  if (err)
    {
      return 1;
    }

  device = devices;
  for (i = 0; i < total; i++, device++)
    {
      if (device_num >= 0)
	{
	  if (i == device_num)
	    {
	      break;
	    }
	}
      else if (device_name)
	{
	  if (strcmp (device->desc.name, device_name) == 0)
	    {
	      break;
	    }
	}
      else
	{
	  if (device->bus == bus && device->address == address)
	    {
	      break;
	    }
	}
    }

  if (i == total)
    {
      err = 1;
    }
  else
    {
      *dev = malloc (sizeof (struct ow_device));
      memcpy (*dev, device, sizeof (struct ow_device));
    }

  free (devices);

  return err;
}

void
ow_set_thread_rt_priority (pthread_t thread, int p)
{
  struct sched_param default_rt_param = {
    .sched_priority = p
  };
  pthread_setschedparam (thread, SCHED_FIFO, &default_rt_param);
}
