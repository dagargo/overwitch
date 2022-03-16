/*
 *   common.c
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

#include "common.h"
#include "utils.h"

void
print_help (const char *executable_path, const char *package_string,
	    struct option *option)
{
  char *exec_name;
  char *executable_path_copy = strdup (executable_path);

  fprintf (stderr, "%s\n", package_string);
  exec_name = basename (executable_path_copy);
  fprintf (stderr, "Usage: %s [options]\n", exec_name);
  fprintf (stderr, "Options:\n");
  while (option->name)
    {
      fprintf (stderr, "  --%s, -%c", option->name, option->val);
      if (option->has_arg)
	{
	  fprintf (stderr, " value");
	}
      fprintf (stderr, "\n");
      option++;
    }
  free (executable_path_copy);
}

ow_err_t
print_devices ()
{
  ssize_t total;
  struct ow_usb_device *devices;
  struct ow_usb_device *device;
  ow_err_t err = ow_get_devices (&devices, &total);

  if (err)
    {
      return err;
    }

  device = devices;
  for (int i = 0; i < total; i++, device++)
    {
      fprintf (stderr, "%d: Bus %03d Device %03d: ID %04x:%04x %s\n", i,
	       device->bus, device->address, device->vid, device->pid,
	       device->name);
    }

  ow_free_usb_device_list (devices, total);
  return OW_OK;
}
