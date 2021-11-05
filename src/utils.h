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

#include <jack/jack.h>
#include <jack/thread.h>

#define debug_print(level, format, ...) if (level <= debug_level) fprintf(stderr, "DEBUG:" __FILE__ ":%d:(%s): " format, __LINE__, __FUNCTION__, ## __VA_ARGS__)
#define error_print(format, ...) fprintf(stderr, "\x1b[31mERROR:" __FILE__ ":%d:(%s): " format "\x1b[m", __LINE__, __FUNCTION__, ## __VA_ARGS__)

extern int debug_level;

struct instant
{
  double time;
  jack_nframes_t frames;
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
  jack_nframes_t kj;
  double _w0;
  double _w1;
  double _w2;
  int kdel;
  double _z1;
  double _z2;
  double _z3;
  double ratio_max;
  double ratio_min;
  double ratio_sum;
  double ratio_avg;
  double last_ratio_avg;
  double err;
  jack_nframes_t ko0;
  jack_nframes_t ko1;
  double to0;
  double to1;
};

void dll_counter_init (struct dll_counter *, double, int);

void dll_counter_inc (struct dll_counter *, int);

void dll_init (struct dll *, double, double, int, int);

void dll_set_loop_filter (struct dll *, double, int, double);

void dll_update_err (struct dll *, jack_time_t);

void dll_update (struct dll *);

void set_rt_priority (int);
