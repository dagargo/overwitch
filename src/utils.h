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
#include <time.h>

#define debug_print(level, format, ...) if (level <= debug_level) fprintf(stderr, "DEBUG:" __FILE__ ":%d:(%s): " format, __LINE__, __FUNCTION__, ## __VA_ARGS__)
#define error_print(format, ...) fprintf(stderr, "\x1b[31mERROR:" __FILE__ ":%d:(%s): " format "\x1b[m", __LINE__, __FUNCTION__, ## __VA_ARGS__)

extern int debug_level;

//JACK time callback counter
struct jt_cb_counter
{
  int counter;
  int iters;
  size_t frames_per_iter;
  jack_time_t start_t;
  jack_time_t last_t;
  double estimated_sr;
  const jack_client_t *jclient;
  void (*jack_get_time) (const jack_client_t *, jack_time_t *);
};

int jt_cb_counter_inc (struct jt_cb_counter *);

double jt_cb_counter_restart (struct jt_cb_counter *);

void jt_cb_counter_init_jack (struct jt_cb_counter *, const jack_client_t *,
			      size_t, size_t);

void jt_cb_counter_init_ext (struct jt_cb_counter *, const jack_client_t *,
			     size_t, size_t);
