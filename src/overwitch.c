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

#include <stdlib.h>
#include <libusb.h>
#include <string.h>
#include "overwitch.h"
#include "utils.h"

#define ELEKTRON_VID 0x1935

#define AFMK1_PID 0x0004
#define AKEYS_PID 0x0006
#define ARMK1_PID 0x0008
#define AHMK1_PID 0x000a
#define DTAKT_PID 0x000c
#define AFMK2_PID 0x000e
#define ARMK2_PID 0x0010
#define DTONE_PID 0x0014
#define AHMK2_PID 0x0016
#define DKEYS_PID 0x001c

static const struct ow_device_desc DIGITAKT_DESC = {
  .pid = DTAKT_PID,
  .name = "Digitakt",
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Main L Input", "Main R Input"},
  .output_track_names =
    {"Main L", "Main R", "Track 1", "Track 2", "Track 3", "Track 4",
     "Track 5", "Track 6", "Track 7", "Track 8", "Input L",
     "Input R"}
};

static const struct ow_device_desc DIGITONE_DESC = {
  .pid = DTONE_PID,
  .name = "Digitone",
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Main L Input", "Main R Input"},
  .output_track_names =
    {"Main L", "Main R", "Track 1 L", "Track 1 R", "Track 2 L",
     "Track 2 R", "Track 3 L", "Track 3 R", "Track 4 L", "Track 4 R",
     "Input L", "Input R"}
};

static const struct ow_device_desc AFMK2_DESC = {
  .pid = AFMK2_PID,
  .name = "Analog Four MKII",
  .inputs = 6,
  .outputs = 8,
  .input_track_names = {"Main L Input", "Main R Input", "Synth Track 1 Input",
			"Synth Track 2 Input", "Synth Track 3 Input",
			"Synth Track 4 Input"},
  .output_track_names =
    {"Main L", "Main R", "Synth Track 1", "Synth Track 2", "Synth Track 3",
     "Synth Track 4", "Input L", "Input R"}
};

static const struct ow_device_desc ARMK2_DESC = {
  .pid = ARMK2_PID,
  .name = "Analog Rytm MKII",
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

static const struct ow_device_desc DKEYS_DESC = {
  .pid = DKEYS_PID,
  .name = "Digitone Keys",
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Main L Input", "Main R Input"},
  .output_track_names =
    {"Main L", "Main R", "Track 1 L", "Track 1 R", "Track 2 L",
     "Track 2 R", "Track 3 L", "Track 3 R", "Track 4 L", "Track 4 R",
     "Input L", "Input R"}
};

static const struct ow_device_desc AHMK1_DESC = {
  .pid = AHMK1_PID,
  .name = "Analog Heat",
  .inputs = 4,
  .outputs = 4,
  .input_track_names =
    {"Main L Input", "Main R Input", "FX Send L", "FX Send R"},
  .output_track_names = {"Main L", "Main R", "FX Return L", "FX Return R"}
};

static const struct ow_device_desc AHMK2_DESC = {
  .pid = AHMK2_PID,
  .name = "Analog Heat MKII",
  .inputs = 4,
  .outputs = 4,
  .input_track_names =
    {"Main L Input", "Main R Input", "FX Send L", "FX Send R"},
  .output_track_names = {"Main L", "Main R", "FX Return L", "FX Return R"}
};

const struct ow_device_desc *OB_DEVICE_DESCS[] = {
  &DIGITAKT_DESC, &DIGITONE_DESC, &AFMK2_DESC, &ARMK2_DESC, &DKEYS_DESC,
  &AHMK1_DESC, &AHMK2_DESC, NULL
};

int
ow_bytes_to_frame_bytes (int bytes, int bytes_per_frame)
{
  int frames = bytes / bytes_per_frame;
  return frames * bytes_per_frame;
}

int
ow_print_devices ()
{
  libusb_context *context = NULL;
  libusb_device **list = NULL;
  ssize_t count = 0;
  libusb_device *device;
  struct libusb_device_descriptor desc;
  int i, j, err;
  char *name;
  uint8_t bus, address;

  if (libusb_init (&context) != LIBUSB_SUCCESS)
    {
      return 1;
    }

  count = libusb_get_device_list (context, &list);
  j = 0;
  for (i = 0; i < count; i++)
    {
      device = list[i];
      err = libusb_get_device_descriptor (device, &desc);
      if (err)
	{
	  error_print ("Error while getting device description: %s",
		       libusb_error_name (err));
	  continue;
	}

      if (ow_is_valid_device (desc.idVendor, desc.idProduct, &name))
	{
	  bus = libusb_get_bus_number (device);
	  address = libusb_get_device_address (device);
	  printf ("%d: Bus %03d Device %03d: ID %04x:%04x %s\n", j,
		  bus, address, desc.idVendor, desc.idProduct, name);
	  j++;
	}
    }

  libusb_free_device_list (list, count);
  libusb_exit (context);
  return 0;
}

int
ow_get_bus_address (int index, char *name, uint8_t * bus, uint8_t * address)
{
  libusb_context *context = NULL;
  libusb_device **list = NULL;
  int err, i, j;
  ssize_t count = 0;
  libusb_device *device = NULL;
  struct libusb_device_descriptor desc;
  char *dev_name;

  err = libusb_init (&context);
  if (err != LIBUSB_SUCCESS)
    {
      return err;
    }

  j = 0;
  count = libusb_get_device_list (context, &list);
  for (i = 0; i < count; i++)
    {
      device = list[i];
      err = libusb_get_device_descriptor (device, &desc);
      if (err)
	{
	  error_print ("Error while getting device description: %s",
		       libusb_error_name (err));
	  continue;
	}

      err = 1;
      if (ow_is_valid_device (desc.idVendor, desc.idProduct, &dev_name))
	{
	  if (index >= 0)
	    {
	      if (j == index)
		{
		  err = 0;
		  break;
		}
	    }
	  else
	    {
	      if (strcmp (name, dev_name) == 0)
		{
		  err = 0;
		  break;
		}
	    }
	  j++;
	}
    }

  if (err)
    {
      error_print ("No device found\n");
    }
  else
    {
      *bus = libusb_get_bus_number (device);
      *address = libusb_get_device_address (device);
    }

  libusb_free_device_list (list, count);
  libusb_exit (context);
  return err;
}

int
ow_is_valid_device (uint16_t vid, uint16_t pid, char **name)
{
  if (vid != ELEKTRON_VID)
    {
      return 0;
    }
  for (const struct ow_device_desc ** d = OB_DEVICE_DESCS; *d != NULL; d++)
    {
      if ((*d)->pid == pid)
	{
	  *name = (*d)->name;
	  return 1;
	}
    }
  return 0;
}
