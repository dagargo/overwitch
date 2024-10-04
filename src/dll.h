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

#pragma once

#include <stdint.h>

struct instant
{
  double time;
  uint32_t frames;
};

struct ow_dll_overbridge
{
  struct instant i0;
  struct instant i1;
  double dt;
  double w1;
  double w2;
  int boot;
};

struct ow_dll
{
  double ratio;
  uint32_t frames;
  double w0;
  double w1;
  double w2;
  int delay;
  double z1;
  double z2;
  double z3;
  double t_quantum;
  double ratio_sum;
  double ratio_avg;
  int ratio_avg_cycles;
  double last_ratio_avg;
  double err;
  struct instant i0;
  struct instant i1;
  struct ow_dll_overbridge dll_overbridge;
  int set;
  int boot;
};

void ow_dll_overbridge_init (void *, double, uint32_t);

void ow_dll_overbridge_update (void *, uint32_t, uint64_t);

void ow_dll_host_init (struct ow_dll *);

void ow_dll_host_reset (struct ow_dll *, double, double, uint32_t, uint32_t);

void ow_dll_host_set_loop_filter (struct ow_dll *, double, uint32_t, double);

void ow_dll_host_update_error (struct ow_dll *, uint64_t);

void ow_dll_host_update (struct ow_dll *);

void ow_dll_host_calc_avg (struct ow_dll *);

void ow_dll_host_load_dll_overbridge (struct ow_dll *);

int ow_dll_tuned (struct ow_dll *);
