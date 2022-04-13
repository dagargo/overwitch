/*
 *   dll.h
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

#ifndef DLL_H
#define DLL_H

#include <stdint.h>

struct instant
{
  double time;
  uint32_t frames;
};

struct ow_dll_overwitch
{
  struct instant i0;
  struct instant i1;
  double e2;
  double b;
  double c;
};

struct ow_dll
{
  double ratio;
  uint32_t kj;
  double _w0;
  double _w1;
  double _w2;
  int kdel;
  double _z1;
  double _z2;
  double _z3;
  double ratio_sum;
  double ratio_avg;
  double last_ratio_avg;
  double err;
  uint32_t ko0;
  uint32_t ko1;
  double to0;
  double to1;
  struct ow_dll_overwitch dll_ow;
  int init;
};

void ow_dll_overwitch_init (struct ow_dll_overwitch *, double, int, double);

void ow_dll_overwitch_inc (struct ow_dll_overwitch *, int, double);

void ow_dll_primary_init (struct ow_dll *);

void ow_dll_primary_reset (struct ow_dll *, double, double, int, int);

void ow_dll_primary_set_loop_filter (struct ow_dll *, double, int, double);

void ow_dll_primary_update_err (struct ow_dll *, double);

void ow_dll_primary_update (struct ow_dll *);

void ow_dll_primary_calc_avg (struct ow_dll *, int);

void ow_dll_primary_first_time_run (struct ow_dll *);

void ow_dll_primary_load_dll_overwitch (struct ow_dll *);

#endif
