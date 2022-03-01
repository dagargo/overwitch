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

#include <stdint.h>

struct instant
{
  double time;
  uint32_t frames;
};

struct dll_counter
{
  struct instant i0;
  struct instant i1;
  double e2;
  double b;
  double c;
};

struct dll
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
  struct dll_counter counter;
};

void dll_counter_init (struct dll_counter *, double, int, uint64_t);

void dll_counter_inc (struct dll_counter *, int, uint64_t);

void dll_init (struct dll *, double, double, int, int);

void dll_set_loop_filter (struct dll *, double, int, double);

void dll_update_err (struct dll *, uint64_t);

void dll_update (struct dll *);

void dll_calc_avg (struct dll *, int);

void dll_first_time_run (struct dll *);
