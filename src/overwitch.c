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

#define DEVICES_FILE "/devices.json"

#define ELEKTRON_VID 0x1935

#define ANALOG_FOUR_PID 0x0004
#define ANALOG_KEYS_PID 0x0006
#define ANALOG_RYTM_PID 0x0008
#define ANALOG_HEAT_PID 0x000a
#define DIGITAKT_PID 0x0b2c
#define ANALOG_FOUR_MK2_PID 0x0b47
#define ANALOG_RYTM_MK2_PID 0x0b48
#define DIGITONE_PID 0x0b36
#define ANALOG_HEAT_MK2_PID 0x0b52
#define DIGITONE_KEYS_PID 0x0b35
#define SYNTAKT_PID 0x0b4a
#define ANALOG_HEAT_FX_PID 0x0b53
#define DIGITAKT_II_PID 0x0b2b

#define DEV_TAG_PID "pid"
#define DEV_TAG_NAME "name"
#define DEV_TAG_PROTOCOL "protocol"
#define DEV_TAG_INPUT_TRACK_NAMES "input_track_names"
#define DEV_TAG_OUTPUT_TRACK_NAMES "output_track_names"
#define DEV_TAG_CUSTOM_INPUT_TRACK_SIZES "custom_input_track_sizes"
#define DEV_TAG_CUSTOM_OUTPUT_TRACK_SIZES "custom_output_track_sizes"

#define OB_DEFAULT_SAMPLE_SIZE 4

#if !defined(JSON_DEVS_FILE) || defined(OW_TESTING)
static const struct ow_device_desc DIGITAKT_DESC = {
  .pid = DIGITAKT_PID,
  .name = "Digitakt",
  .protocol = 1,
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Main L Input", "Main R Input"},
  .output_track_names =
    {"Main L", "Main R", "Track 1", "Track 2", "Track 3", "Track 4",
     "Track 5", "Track 6", "Track 7", "Track 8", "Input L",
     "Input R"}
};

static const struct ow_device_desc DIGITONE_DESC = {
  .pid = DIGITONE_PID,
  .name = "Digitone",
  .protocol = 1,
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Main L Input", "Main R Input"},
  .output_track_names =
    {"Main L", "Main R", "Track 1 L", "Track 1 R", "Track 2 L",
     "Track 2 R", "Track 3 L", "Track 3 R", "Track 4 L", "Track 4 R",
     "Input L", "Input R"}
};

static const struct ow_device_desc ANALOG_FOUR_MK2_DESC = {
  .pid = ANALOG_FOUR_MK2_PID,
  .name = "Analog Four MKII",
  .protocol = 1,
  .inputs = 6,
  .outputs = 8,
  .input_track_names = {"Main L Input", "Main R Input", "Synth Track 1 Input",
			"Synth Track 2 Input", "Synth Track 3 Input",
			"Synth Track 4 Input"},
  .output_track_names =
    {"Main L", "Main R", "Synth Track 1", "Synth Track 2", "Synth Track 3",
     "Synth Track 4", "Input L", "Input R"}
};

static const struct ow_device_desc ANALOG_RYTM_MK2_DESC = {
  .pid = ANALOG_RYTM_MK2_PID,
  .name = "Analog Rytm MKII",
  .protocol = 1,
  .inputs = 12,
  .outputs = 12,
  .input_track_names =
    {"Main L Input", "Main R Input", "Main FX L Input", "Main FX R Input",
     "BD Input", "SD Input", "RS/CP Input", "BT Input",
     "LT Input", "MT/HT Input", "CH/OH Input", "CY/CB Input"},
  .output_track_names = {"Main L", "Main R", "BD", "SD", "RS/CP",
			 "BT", "LT", "MT/HT", "CH/OH", "CY/CB", "Input L",
			 "Input R"}
};

static const struct ow_device_desc DIGITONE_KEYS_DESC = {
  .pid = DIGITONE_KEYS_PID,
  .name = "Digitone Keys",
  .protocol = 1,
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Main L Input", "Main R Input"},
  .output_track_names =
    {"Main L", "Main R", "Track 1 L", "Track 1 R", "Track 2 L",
     "Track 2 R", "Track 3 L", "Track 3 R", "Track 4 L", "Track 4 R",
     "Input L", "Input R"}
};

static const struct ow_device_desc ANALOG_HEAT_DESC = {
  .pid = ANALOG_HEAT_PID,
  .name = "Analog Heat",
  .protocol = 1,
  .inputs = 4,
  .outputs = 4,
  .input_track_names =
    {"Main L Input", "Main R Input", "FX Send L", "FX Send R"},
  .output_track_names = {"Main L", "Main R", "FX Return L", "FX Return R"}
};

static const struct ow_device_desc ANALOG_HEAT_MK2_DESC = {
  .pid = ANALOG_HEAT_MK2_PID,
  .name = "Analog Heat MKII",
  .protocol = 1,
  .inputs = 4,
  .outputs = 4,
  .input_track_names =
    {"Main L Input", "Main R Input", "FX Send L", "FX Send R"},
  .output_track_names = {"Main L", "Main R", "FX Return L", "FX Return R"}
};

static const struct ow_device_desc ANALOG_HEAT_FX_DESC = {
  .pid = ANALOG_HEAT_FX_PID,
  .name = "Analog Heat +FX",
  .protocol = 1,
  .inputs = 4,
  .outputs = 4,
  .input_track_names =
    {"Main L Input", "Main R Input", "FX Send L", "FX Send R"},
  .output_track_names = {"Main L", "Main R", "FX Return L", "FX Return R"}
};

static const struct ow_device_desc SYNTAKT_DESC = {
  .pid = SYNTAKT_PID,
  .name = "Syntakt",
  .protocol = 1,
  .inputs = 8,
  .outputs = 20,
  .input_track_names =
    {"Main L Input", "Main R Input", "Analog FX L Input", "Analog FX R Input",
     "Track 9 Input", "Track 10 Input", "Track 11 Input", "Track 12 Input"},
  .output_track_names =
    {"Main L", "Main R", "Track  1", "Track  2", "Track  3", "Track  4",
     "Track  5", "Track  6", "Track  7", "Track  8", "Track  9", "Track 10",
     "Track 11", "Track 12", "Analog FX L", "Analog FX R",
     "Delay/Reverb L", "Delay/Reverb R", "Input L", "Input R"}
};

static const struct ow_device_desc DIGITAKT_II_DESC = {
  .pid = DIGITAKT_II_PID,
  .name = "Digitakt II",
  .protocol = 2,
  .inputs = 8,
  .outputs = 42,
  .input_track_names =
    {"Main L Input", "Main R Input", "Delay L Input", "Delay R Input",
     "Reverb L Input", "Reverb R Input", "Chorus L Input", "Chorus R Input"},
  .custom_input_track_sizes = {4, 4, 4, 4, 4, 4, 4, 4},
  .output_track_names =
    {"Main L", "Main R", "Track  1 L", "Track  1 R", "Track  2 L",
     "Track  2 R", "Track  3 L", "Track  3 R", "Track  4 L", "Track  4 R",
     "Track  5 L", "Track  5 R", "Track  6 L", "Track  6 R", "Track  7 L",
     "Track  7 R", "Track  8 L", "Track  8 R", "Track  9 L", "Track  9 R",
     "Track 10 L", "Track 10 R", "Track 11 L", "Track 11 R", "Track 12 L",
     "Track 12 R", "Track 13 L", "Track 13 R", "Track 14 L", "Track 14 R",
     "Track 15 L", "Track 15 R", "Track 16 L", "Track 16 R", "Delay L",
     "Delay R", "Reverb L", "Reverb R", "Chorus L", "Chorus R", "Input L",
     "Input R"},
  .custom_output_track_sizes =
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3}
};

static const struct ow_device_desc *OB_DEVICE_DESCS[] = {
  &DIGITAKT_DESC, &DIGITONE_DESC, &ANALOG_FOUR_MK2_DESC,
  &ANALOG_RYTM_MK2_DESC, &DIGITONE_KEYS_DESC,
  &ANALOG_HEAT_DESC, &ANALOG_HEAT_MK2_DESC, &ANALOG_HEAT_FX_DESC,
  &SYNTAKT_DESC, &DIGITAKT_II_DESC, NULL
};
#endif

void
ow_free_device_desc (struct ow_device_desc *desc)
{
  char **names;

  free (desc->name);

  names = desc->input_track_names;
  for (int i = 0; i < desc->inputs; i++, names++)
    {
      free (*names);
    }

  names = desc->output_track_names;
  for (int i = 0; i < desc->outputs; i++, names++)
    {
      free (*names);
    }
}

void
ow_free_usb_device_list (struct ow_usb_device *devices, size_t size)
{
  struct ow_usb_device *d = devices;
  for (int i = 0; i < size; i++, d++)
    {
      ow_free_device_desc (&d->desc);
    }
  free (devices);
}

int
ow_get_usb_device_list (struct ow_usb_device **devices, size_t *size)
{
  int err;
  uint8_t bus, address;
  libusb_device **usb_device;
  struct libusb_device_descriptor desc;
  struct ow_usb_device *device;
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
      *devices = NULL;
      return 0;
    }

  *devices = malloc (sizeof (struct ow_usb_device) * total);
  device = *devices;
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

      if (!ow_get_device_desc_from_vid_pid (desc.idVendor, desc.idProduct,
					    &device->desc))
	{
	  bus = libusb_get_bus_number (*usb_device);
	  address = libusb_get_device_address (*usb_device);
	  debug_print (1, "Found %s (bus %03d, address %03d, ID %04x:%04x)",
		       device->desc.name, bus, address, desc.idVendor,
		       desc.idProduct);
	  device->vid = desc.idVendor;
	  device->pid = desc.idProduct;
	  device->bus = bus;
	  device->address = address;
	  device++;
	  (*size)++;
	}
    }

  if (!*size)
    {
      free (*devices);
      *devices = NULL;
    }

  libusb_free_device_list (usb_devices, total);
  libusb_exit (context);
  return 0;
}

void
ow_copy_device_desc_static (struct ow_device_desc *device_desc,
			    const struct ow_device_desc *d)
{
  device_desc->pid = d->pid;
  device_desc->name = strdup (d->name);
  device_desc->protocol = d->protocol;
  device_desc->inputs = d->inputs;
  device_desc->outputs = d->outputs;

  for (int i = 0; i < device_desc->inputs; i++)
    {
      device_desc->input_track_names[i] = strdup (d->input_track_names[i]);
      device_desc->custom_input_track_sizes[i] =
	d->custom_input_track_sizes[i];
    }

  for (int i = 0; i < device_desc->outputs; i++)
    {
      device_desc->output_track_names[i] = strdup (d->output_track_names[i]);
      device_desc->custom_output_track_sizes[i] =
	d->custom_output_track_sizes[i];
    }
}

int
ow_get_device_desc_from_vid_pid (uint16_t vid, uint16_t pid,
				 struct ow_device_desc *device_desc)
{
  if (vid != ELEKTRON_VID)
    {
      return 1;
    }

#if defined(JSON_DEVS_FILE) && !defined(OW_TESTING)
  gint dpid, err, devices;
  JsonParser *parser;
  JsonReader *reader;
  gchar *devices_filename;
  GError *error = NULL;

  device_desc->inputs = 0;
  device_desc->outputs = 0;

  parser = json_parser_new ();

  devices_filename = get_expanded_dir (CONF_DIR DEVICES_FILE);

  if (!json_parser_load_from_file (parser, devices_filename, &error))
    {
      debug_print (1, "%s", error->message);
      g_clear_error (&error);

      g_free (devices_filename);
      devices_filename = strdup (DATADIR DEVICES_FILE);

      debug_print (1, "Falling back to %s...", devices_filename);

      if (!json_parser_load_from_file (parser, devices_filename, &error))
	{
	  error_print ("%s", error->message);
	  g_clear_error (&error);
	  err = -ENODEV;
	  goto cleanup_parser;
	}
    }

  reader = json_reader_new (json_parser_get_root (parser));
  if (!reader)
    {
      error_print ("Unable to read from parser");
      err = -ENODEV;
      goto cleanup_parser;
    }

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

      if (!json_reader_read_member (reader, DEV_TAG_PID))
	{
	  error_print ("Cannot read member '%s'. Continuing...", DEV_TAG_PID);
	  continue;
	}
      dpid = json_reader_get_int_value (reader);
      json_reader_end_member (reader);
      if (dpid != pid)
	{
	  json_reader_end_element (reader);
	  continue;
	}
      device_desc->pid = dpid;

      err = 0;
      debug_print (1, "Device with PID %d found", dpid);

      if (!json_reader_read_member (reader, DEV_TAG_NAME))
	{
	  error_print ("Cannot read member '%s'. Stopping...", DEV_TAG_NAME);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      device_desc->name = strdup (json_reader_get_string_value (reader));
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_PROTOCOL))
	{
	  error_print ("Cannot read member '%s'. Stopping...",
		       DEV_TAG_PROTOCOL);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      device_desc->protocol = json_reader_get_int_value (reader);
      json_reader_end_member (reader);

      if (device_desc->protocol < OW_ENGINE_PROTOCOL_V1 ||
	  device_desc->protocol > OW_ENGINE_PROTOCOL_V2)
	{
	  error_print ("Invalid protocol version '%d'. Stopping...",
		       device_desc->protocol);
	  err = -ENODEV;
	  break;
	}

      if (!json_reader_read_member (reader, DEV_TAG_INPUT_TRACK_NAMES))
	{
	  error_print ("Cannot read member '%s'. Stopping...",
		       DEV_TAG_INPUT_TRACK_NAMES);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      if (!json_reader_is_array (reader))
	{
	  error_print ("Not an array");
	  err = -ENODEV;
	  goto cleanup_reader;
	}
      device_desc->inputs = json_reader_count_elements (reader);
      if (!device_desc->inputs)
	{
	  debug_print (1, "No tracks found");
	  err = -ENODEV;
	  goto cleanup_reader;
	}

      for (int j = 0; j < device_desc->inputs; j++)
	{
	  device_desc->input_track_names[j] = NULL;
	}
      for (int j = 0; j < device_desc->inputs; j++)
	{
	  if (json_reader_read_element (reader, j))
	    {
	      device_desc->input_track_names[j] =
		strdup (json_reader_get_string_value (reader));
	      json_reader_end_element (reader);
	    }
	  else
	    {
	      error_print ("Cannot read input track name %d", j);
	      ow_free_device_desc (device_desc);
	      err = -ENODEV;
	      goto cleanup_reader;
	    }
	}
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_OUTPUT_TRACK_NAMES))
	{
	  error_print ("Cannot read member '%s'. Stopping...",
		       DEV_TAG_OUTPUT_TRACK_NAMES);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      if (!json_reader_is_array (reader))
	{
	  error_print ("Not an array");
	  err = -ENODEV;
	  goto cleanup_reader;
	}
      device_desc->outputs = json_reader_count_elements (reader);
      if (!device_desc->outputs)
	{
	  debug_print (1, "No tracks found");
	  err = -ENODEV;
	  goto cleanup_reader;
	}

      for (int j = 0; j < device_desc->outputs; j++)
	{
	  device_desc->output_track_names[j] = NULL;
	}
      for (int j = 0; j < device_desc->outputs; j++)
	{
	  if (json_reader_read_element (reader, j))
	    {
	      device_desc->output_track_names[j] =
		strdup (json_reader_get_string_value (reader));
	      json_reader_end_element (reader);
	    }
	  else
	    {
	      error_print ("Cannot read output track name %d", j);
	      ow_free_device_desc (device_desc);
	      err = -ENODEV;
	      goto cleanup_reader;
	    }
	}
      json_reader_end_member (reader);

      if (json_reader_read_member (reader, DEV_TAG_CUSTOM_INPUT_TRACK_SIZES))
	{
	  if (json_reader_is_array (reader))
	    {
	      for (int j = 0; j < device_desc->inputs; j++)
		{
		  if (json_reader_read_element (reader, j))
		    {
		      device_desc->custom_input_track_sizes[j] =
			json_reader_get_int_value (reader);
		      json_reader_end_element (reader);
		    }
		  else
		    {
		      error_print ("Cannot read custom input track size %d",
				   j);
		      ow_free_device_desc (device_desc);
		      err = -ENODEV;
		      goto cleanup_reader;
		    }
		}
	      json_reader_end_member (reader);
	    }
	  else
	    {
	      error_print ("Not an array");
	      err = -ENODEV;
	      goto cleanup_reader;
	    }
	}
      else
	{
	  debug_print (1, "Cannot read member '%s'. Using default value...",
		       DEV_TAG_CUSTOM_INPUT_TRACK_SIZES);
	  json_reader_end_element (reader);

	  for (int j = 0; j < device_desc->inputs; j++)
	    {
	      device_desc->custom_input_track_sizes[j] =
		OB_DEFAULT_SAMPLE_SIZE;
	    }
	}

      if (json_reader_read_member (reader, DEV_TAG_CUSTOM_OUTPUT_TRACK_SIZES))
	{
	  if (json_reader_is_array (reader))
	    {
	      for (int j = 0; j < device_desc->outputs; j++)
		{
		  if (json_reader_read_element (reader, j))
		    {
		      device_desc->custom_output_track_sizes[j] =
			json_reader_get_int_value (reader);
		      json_reader_end_element (reader);
		    }
		  else
		    {
		      error_print ("Cannot read custom output track size %d",
				   j);
		      ow_free_device_desc (device_desc);
		      err = -ENODEV;
		      goto cleanup_reader;
		    }
		}
	      json_reader_end_member (reader);
	    }
	  else
	    {
	      error_print ("Not an array");
	      err = -ENODEV;
	      goto cleanup_reader;
	    }
	}
      else
	{
	  debug_print (1, "Cannot read member '%s'. Using default value...",
		       DEV_TAG_CUSTOM_OUTPUT_TRACK_SIZES);
	  json_reader_end_element (reader);

	  for (int j = 0; j < device_desc->outputs; j++)
	    {
	      device_desc->custom_output_track_sizes[j] =
		OB_DEFAULT_SAMPLE_SIZE;
	    }
	}

      break;
    }

cleanup_reader:
  g_object_unref (reader);
cleanup_parser:
  g_object_unref (parser);
  g_free (devices_filename);
  return err;
#else
  for (const struct ow_device_desc ** d = OB_DEVICE_DESCS; *d != NULL; d++)
    {
      if ((*d)->pid == pid)
	{
	  ow_copy_device_desc_static (device_desc, *d);
	  return 0;
	}
    }
  return 1;
#endif
}

int
ow_get_usb_device_from_device_attrs (int device_num, const char *device_name,
				     struct ow_usb_device **device)
{
  int i;
  size_t total;
  struct ow_usb_device *usb_devices;
  struct ow_usb_device *usb_device;
  ow_err_t err = ow_get_usb_device_list (&usb_devices, &total);

  if (err)
    {
      return 1;
    }

  usb_device = usb_devices;
  for (i = 0; i < total; i++, usb_device++)
    {
      if (device_num >= 0)
	{
	  if (i == device_num)
	    {
	      break;
	    }
	}
      else
	{
	  if (strcmp (usb_device->desc.name, device_name) == 0)
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
      *device = malloc (sizeof (struct ow_usb_device));
      memcpy (*device, usb_device, sizeof (struct ow_usb_device));
    }

  ow_free_usb_device_list (usb_devices, total);

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
