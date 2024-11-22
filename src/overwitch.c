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
#define DEV_TAG_FORMAT "format"
#define DEV_TAG_INPUT_TRACKS "input_tracks"
#define DEV_TAG_OUTPUT_TRACKS "output_tracks"
#define DEV_TAG_TRACK_NAME "name"
#define DEV_TAG_TRACK_SIZE "size"

#if !defined(JSON_DEVS_FILE) || defined(OW_TESTING)
static const struct ow_device_desc DIGITAKT_DESC = {
  .pid = DIGITAKT_PID,
  .name = "Digitakt",
  .format = 1,
  .inputs = 2,
  .outputs = 12,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "Track 1",.size = 4},
		    {.name = "Track 2",.size = 4},
		    {.name = "Track 3",.size = 4},
		    {.name = "Track 4",.size = 4},
		    {.name = "Track 5",.size = 4},
		    {.name = "Track 6",.size = 4},
		    {.name = "Track 7",.size = 4},
		    {.name = "Track 8",.size = 4},
		    {.name = "Input L",.size = 4},
		    {.name = "Input R",.size = 4}}
};

static const struct ow_device_desc DIGITONE_DESC = {
  .pid = DIGITONE_PID,
  .name = "Digitone",
  .format = 1,
  .inputs = 2,
  .outputs = 12,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "Track 1 L",.size = 4},
		    {.name = "Track 1 R",.size = 4},
		    {.name = "Track 2 L",.size = 4},
		    {.name = "Track 2 R",.size = 4},
		    {.name = "Track 3 L",.size = 4},
		    {.name = "Track 3 R",.size = 4},
		    {.name = "Track 4 L",.size = 4},
		    {.name = "Track 4 R",.size = 4},
		    {.name = "Input L",.size = 4},
		    {.name = "Input R",.size = 4}}
};

static const struct ow_device_desc ANALOG_FOUR_MK2_DESC = {
  .pid = ANALOG_FOUR_MK2_PID,
  .name = "Analog Four MKII",
  .format = 1,
  .inputs = 6,
  .outputs = 8,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4},
		   {.name = "Synth Track 1 Input",.size = 4},
		   {.name = "Synth Track 2 Input",.size = 4},
		   {.name = "Synth Track 3 Input",.size = 4},
		   {.name = "Synth Track 4 Input",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "Synth Track 1",.size = 4},
		    {.name = "Synth Track 2",.size = 4},
		    {.name = "Synth Track 3",.size = 4},
		    {.name = "Synth Track 4",.size = 4},
		    {.name = "Input L",.size = 4},
		    {.name = "Input R",.size = 4}}
};

static const struct ow_device_desc ANALOG_RYTM_MK2_DESC = {
  .pid = ANALOG_RYTM_MK2_PID,
  .name = "Analog Rytm MKII",
  .format = 1,
  .inputs = 12,
  .outputs = 12,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4},
		   {.name = "Main FX L Input",.size = 4},
		   {.name = "Main FX R Input",.size = 4},
		   {.name = "BD Input",.size = 4},
		   {.name = "SD Input",.size = 4},
		   {.name = "RS/CP Input",.size = 4},
		   {.name = "BT Input",.size = 4},
		   {.name = "LT Input",.size = 4},
		   {.name = "MT/HT Input",.size = 4},
		   {.name = "CH/OH Input",.size = 4},
		   {.name = "CY/CB Input",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "BD",.size = 4},
		    {.name = "SD",.size = 4},
		    {.name = "RS/CP",.size = 4},
		    {.name = "BT",.size = 4},
		    {.name = "LT",.size = 4},
		    {.name = "MT/HT",.size = 4},
		    {.name = "CH/OH",.size = 4},
		    {.name = "CY/CB",.size = 4},
		    {.name = "Input L",.size = 4},
		    {.name = "Input R",.size = 4}}
};

static const struct ow_device_desc DIGITONE_KEYS_DESC = {
  .pid = DIGITONE_KEYS_PID,
  .name = "Digitone Keys",
  .format = 1,
  .inputs = 2,
  .outputs = 12,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "Track 1 L",.size = 4},
		    {.name = "Track 1 R",.size = 4},
		    {.name = "Track 2 L",.size = 4},
		    {.name = "Track 2 R",.size = 4},
		    {.name = "Track 3 L",.size = 4},
		    {.name = "Track 3 R",.size = 4},
		    {.name = "Track 4 L",.size = 4},
		    {.name = "Track 4 R",.size = 4},
		    {.name = "Input L",.size = 4},
		    {.name = "Input R",.size = 4}}
};

static const struct ow_device_desc ANALOG_HEAT_DESC = {
  .pid = ANALOG_HEAT_PID,
  .name = "Analog Heat",
  .format = 1,
  .inputs = 4,
  .outputs = 4,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4},
		   {.name = "FX Send L",.size = 4},
		   {.name = "FX Send R",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "FX Return L",.size = 4},
		    {.name = "FX Return R",.size = 4}}
};

static const struct ow_device_desc ANALOG_HEAT_MK2_DESC = {
  .pid = ANALOG_HEAT_MK2_PID,
  .name = "Analog Heat MKII",
  .format = 1,
  .inputs = 4,
  .outputs = 4,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4},
		   {.name = "FX Send L",.size = 4},
		   {.name = "FX Send R",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "FX Return L",.size = 4},
		    {.name = "FX Return R",.size = 4}}
};

static const struct ow_device_desc ANALOG_HEAT_FX_DESC = {
  .pid = ANALOG_HEAT_FX_PID,
  .name = "Analog Heat +FX",
  .format = 1,
  .inputs = 4,
  .outputs = 4,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4},
		   {.name = "FX Send L",.size = 4},
		   {.name = "FX Send R",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "FX Return L",.size = 4},
		    {.name = "FX Return R",.size = 4}}
};

static const struct ow_device_desc SYNTAKT_DESC = {
  .pid = SYNTAKT_PID,
  .name = "Syntakt",
  .format = 1,
  .inputs = 8,
  .outputs = 20,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4},
		   {.name = "Analog FX L Input",.size = 4},
		   {.name = "Analog FX R Input",.size = 4},
		   {.name = "Track 9 Input",.size = 4},
		   {.name = "Track 10 Input",.size = 4},
		   {.name = "Track 11 Input",.size = 4},
		   {.name = "Track 12 Input",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "Track  1",.size = 4},
		    {.name = "Track  2",.size = 4},
		    {.name = "Track  3",.size = 4},
		    {.name = "Track  4",.size = 4},
		    {.name = "Track  5",.size = 4},
		    {.name = "Track  6",.size = 4},
		    {.name = "Track  7",.size = 4},
		    {.name = "Track  8",.size = 4},
		    {.name = "Track  9",.size = 4},
		    {.name = "Track 10",.size = 4},
		    {.name = "Track 11",.size = 4},
		    {.name = "Track 12",.size = 4},
		    {.name = "Analog FX L",.size = 4},
		    {.name = "Analog FX R",.size = 4},
		    {.name = "Delay/Reverb L",.size = 4},
		    {.name = "Delay/Reverb R",.size = 4},
		    {.name = "Input L",.size = 4},
		    {.name = "Input R",.size = 4}}
};

static const struct ow_device_desc DIGITAKT_II_DESC = {
  .pid = DIGITAKT_II_PID,
  .name = "Digitakt II",
  .format = 2,
  .inputs = 8,
  .outputs = 42,
  .input_tracks = {{.name = "Main L Input",.size = 4},
		   {.name = "Main R Input",.size = 4},
		   {.name = "Delay L Input",.size = 4},
		   {.name = "Delay R Input",.size = 4},
		   {.name = "Reverb L Input",.size = 4},
		   {.name = "Reverb R Input",.size = 4},
		   {.name = "Chorus L Input",.size = 4},
		   {.name = "Chorus R Input",.size = 4}},
  .output_tracks = {{.name = "Main L",.size = 4},
		    {.name = "Main R",.size = 4},
		    {.name = "Track  1 L",.size = 4},
		    {.name = "Track  1 R",.size = 4},
		    {.name = "Track  2 L",.size = 4},
		    {.name = "Track  2 R",.size = 4},
		    {.name = "Track  3 L",.size = 4},
		    {.name = "Track  3 R",.size = 4},
		    {.name = "Track  4 L",.size = 4},
		    {.name = "Track  4 R",.size = 4},
		    {.name = "Track  5 L",.size = 4},
		    {.name = "Track  5 R",.size = 4},
		    {.name = "Track  6 L",.size = 4},
		    {.name = "Track  6 R",.size = 4},
		    {.name = "Track  7 L",.size = 3},
		    {.name = "Track  7 R",.size = 3},
		    {.name = "Track  8 L",.size = 3},
		    {.name = "Track  8 R",.size = 3},
		    {.name = "Track  9 L",.size = 3},
		    {.name = "Track  9 R",.size = 3},
		    {.name = "Track 10 L",.size = 3},
		    {.name = "Track 10 R",.size = 3},
		    {.name = "Track 11 L",.size = 3},
		    {.name = "Track 11 R",.size = 3},
		    {.name = "Track 12 L",.size = 3},
		    {.name = "Track 12 R",.size = 3},
		    {.name = "Track 13 L",.size = 3},
		    {.name = "Track 13 R",.size = 3},
		    {.name = "Track 14 L",.size = 3},
		    {.name = "Track 14 R",.size = 3},
		    {.name = "Track 15 L",.size = 3},
		    {.name = "Track 15 R",.size = 3},
		    {.name = "Track 16 L",.size = 3},
		    {.name = "Track 16 R",.size = 3},
		    {.name = "Delay L",.size = 3},
		    {.name = "Delay R",.size = 3},
		    {.name = "Reverb L",.size = 3},
		    {.name = "Reverb R",.size = 3},
		    {.name = "Chorus L",.size = 3},
		    {.name = "Chorus R",.size = 3},
		    {.name = "Input L",.size = 3},
		    {.name = "Input R",.size = 3}}
};

static const struct ow_device_desc *OB_DEVICE_DESCS[] = {
  &DIGITAKT_DESC, &DIGITONE_DESC, &ANALOG_FOUR_MK2_DESC,
  &ANALOG_RYTM_MK2_DESC, &DIGITONE_KEYS_DESC, &ANALOG_HEAT_DESC,
  &ANALOG_HEAT_MK2_DESC, &ANALOG_HEAT_FX_DESC, &SYNTAKT_DESC,
  &DIGITAKT_II_DESC, NULL
};
#endif

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
ow_copy_device_desc (struct ow_device_desc *device_desc,
		     const struct ow_device_desc *d)
{
  device_desc->pid = d->pid;
  strncpy (device_desc->name, d->name, OW_LABEL_MAX_LEN);
  device_desc->format = d->format;
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
      snprintf (device_desc->name, OW_LABEL_MAX_LEN, "%s",
		json_reader_get_string_value (reader));
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_FORMAT))
	{
	  error_print ("Cannot read member '%s'. Stopping...",
		       DEV_TAG_FORMAT);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      device_desc->format = json_reader_get_int_value (reader);
      json_reader_end_member (reader);

      if (device_desc->format < OW_ENGINE_FORMAT_V1 ||
	  device_desc->format > OW_ENGINE_FORMAT_V2)
	{
	  error_print ("Invalid format version '%d'. Stopping...",
		       device_desc->format);
	  err = -ENODEV;
	  break;
	}

      if (!json_reader_read_member (reader, DEV_TAG_INPUT_TRACKS))
	{
	  error_print ("Cannot read member '%s'. Stopping...",
		       DEV_TAG_INPUT_TRACKS);
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
	  if (json_reader_read_element (reader, j))
	    {
	      if (!json_reader_read_member (reader, DEV_TAG_TRACK_NAME))
		{
		  debug_print (1, "No name found");
		  err = -ENODEV;
		  goto cleanup_reader;
		}
	      snprintf (device_desc->input_tracks[j].name, OW_LABEL_MAX_LEN,
			"%s", json_reader_get_string_value (reader));

	      json_reader_end_member (reader);

	      if (!json_reader_read_member (reader, DEV_TAG_TRACK_SIZE))
		{
		  debug_print (1, "No size found");
		  err = -ENODEV;
		  goto cleanup_reader;
		}
	      device_desc->input_tracks[j].size =
		json_reader_get_int_value (reader);

	      json_reader_end_member (reader);

	      json_reader_end_element (reader);
	    }
	  else
	    {
	      error_print ("Cannot read input track name %d", j);
	      err = -ENODEV;
	      goto cleanup_reader;
	    }
	}
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_OUTPUT_TRACKS))
	{
	  error_print ("Cannot read member '%s'. Stopping...",
		       DEV_TAG_OUTPUT_TRACKS);
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
	  if (json_reader_read_element (reader, j))
	    {
	      if (!json_reader_read_member (reader, DEV_TAG_TRACK_NAME))
		{
		  debug_print (1, "No name found");
		  err = -ENODEV;
		  goto cleanup_reader;
		}
	      snprintf (device_desc->output_tracks[j].name, OW_LABEL_MAX_LEN,
			"%s", json_reader_get_string_value (reader));

	      json_reader_end_member (reader);

	      if (!json_reader_read_member (reader, DEV_TAG_TRACK_SIZE))
		{
		  debug_print (1, "No size found");
		  err = -ENODEV;
		  goto cleanup_reader;
		}
	      device_desc->output_tracks[j].size =
		json_reader_get_int_value (reader);

	      json_reader_end_member (reader);

	      json_reader_end_element (reader);
	    }
	  else
	    {
	      error_print ("Cannot read output track name %d", j);
	      err = -ENODEV;
	      goto cleanup_reader;
	    }
	}
      json_reader_end_member (reader);

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
	  ow_copy_device_desc (device_desc, *d);
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

  free (usb_devices);

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
