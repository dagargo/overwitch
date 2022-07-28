/*
 *   utils.c
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

#include <stdlib.h>
#include <string.h>
#include <wordexp.h>
#define _GNU_SOURCE
#include "utils.h"

int debug_level = 0;

char *
get_expanded_dir (const char *exp)
{
  wordexp_t exp_result;
  size_t n;
  char *exp_dir = malloc (PATH_MAX);

  wordexp (exp, &exp_result, 0);
  n = PATH_MAX - 1;
  strncpy (exp_dir, exp_result.we_wordv[0], n);
  exp_dir[PATH_MAX - 1] = 0;
  wordfree (&exp_result);

  return exp_dir;
}
