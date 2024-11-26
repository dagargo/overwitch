/*
 *   common.h
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#define _GNU_SOURCE
#include <getopt.h>
#include <overwitch.h>

void print_help (const char *, const char *, struct option *, const char *);

ow_err_t print_devices ();

int get_ow_xfr_timeout_argument (const char *);

int get_ow_blocks_per_transfer_argument (const char *);

int get_bus_address_from_str (char *str, uint8_t *, uint8_t *);
