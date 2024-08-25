/*
 *   utils.h
 *   Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
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

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include "../config.h"
#if defined(JSON_DEVS_FILE) && !defined(OW_TESTING)
#include <json-glib/json-glib.h>
#else
//This is used for PATH_MAX
#include <limits.h>
#endif

#define CONF_DIR "~/.config/" PACKAGE

#define debug_print(level, format, ...) { \
  if (level <= debug_level) \
    { \
      fprintf(stderr, "DEBUG:" __FILE__ ":%d:%s: " format "\n", __LINE__, __FUNCTION__, ## __VA_ARGS__); \
    } \
}

#define error_print(format, ...) { \
  gboolean tty = isatty(fileno(stderr)); \
  const gchar * color_start = tty ? "\x1b[31m" : ""; \
  const gchar * color_end = tty ? "\x1b[m" : ""; \
  fprintf(stderr, "%sERROR:" __FILE__ ":%d:(%s): " format "%s\n", color_start, __LINE__, __FUNCTION__, ## __VA_ARGS__, color_end); \
}

extern int debug_level;

char *get_expanded_dir (const char *);

#endif
