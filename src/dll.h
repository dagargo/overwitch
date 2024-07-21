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
  uint64_t time;		//us
  uint32_t frames;
};

struct ow_dll_overbridge
{
  struct instant i0;
  struct instant i1;
  uint64_t e2;
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
  int ratio_avg_cycles;
  double last_ratio_avg;
  double err;
  struct instant i0;
  struct instant i1;
  struct ow_dll_overbridge dll_overbridge;
  int set;
};

void ow_dll_overbridge_init (void *, double, uint32_t, uint64_t);

void ow_dll_overbridge_inc (void *, uint32_t, uint64_t);

void ow_dll_primary_init (struct ow_dll *);

void ow_dll_primary_reset (struct ow_dll *, double, double, int, int);

void ow_dll_primary_set_loop_filter (struct ow_dll *, double, int, double);

void ow_dll_primary_update_err (struct ow_dll *, uint64_t);

void ow_dll_primary_update_err_first_time (struct ow_dll *, uint64_t);

void ow_dll_primary_update (struct ow_dll *);

void ow_dll_primary_calc_avg (struct ow_dll *);

void ow_dll_primary_load_dll_overbridge (struct ow_dll *);

int ow_dll_tuned (struct ow_dll *);

#endif
