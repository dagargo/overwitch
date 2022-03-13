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
