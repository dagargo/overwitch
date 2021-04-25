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

#include <stdio.h>
#include "utils.h"

int debug_level = 0;

static void
jt_cb_counter_time_jack (const jack_client_t * jclient,
			 jack_time_t * current_usecs)
{
  jack_nframes_t current_frames;
  jack_time_t next_usecs;
  float period_usecs;

  if (jack_get_cycle_times (jclient,
			    &current_frames,
			    current_usecs, &next_usecs, &period_usecs))
    {
      error_print ("Error while getting JACK time\n");
    }
}

static void
jt_cb_counter_time_ext (const jack_client_t * jclient, jack_time_t * time)
{
  *time = jack_get_time ();
}

static void
jt_cb_counter_init_int (struct jt_cb_counter *counter,
			const jack_client_t * jclient,
			void (*jack_get_time) (const jack_client_t *,
					       jack_time_t *),
			size_t frames, size_t frames_per_iter)
{
  counter->iters = frames / frames_per_iter;
  counter->frames_per_iter = frames_per_iter;
  counter->counter = -1;
  counter->jclient = jclient;
  counter->estimated_sr = 0.0;
  counter->jack_get_time = jack_get_time;
}

double
jt_cb_counter_restart_int (struct jt_cb_counter *counter, int iters)
{
  jack_time_t diff_t = counter->last_t - counter->start_t;
  counter->start_t = counter->last_t;
  counter->estimated_sr =
    iters * counter->frames_per_iter * 1000000.0 / diff_t;
  return counter->estimated_sr;
}

double
jt_cb_counter_restart (struct jt_cb_counter *counter)
{
  double ratio;

  ratio = jt_cb_counter_restart_int (counter, counter->counter - 1);
  counter->counter = 1;
  return ratio;
}

int
jt_cb_counter_inc (struct jt_cb_counter *counter)
{
  int restart = 0;

  if (counter->counter == -1)
    {
      counter->jack_get_time (counter->jclient, &counter->last_t);
      counter->start_t = counter->last_t;
      counter->counter = 1;
      return 0;
    }

  counter->jack_get_time (counter->jclient, &counter->last_t);

  if (counter->counter == 0)
    {
      jt_cb_counter_restart_int (counter, counter->iters);
      restart = 1;
    }

  counter->counter++;

  if (counter->counter == counter->iters)
    {
      counter->counter = 0;
    }

  return restart;
}

void
jt_cb_counter_init_jack (struct jt_cb_counter *counter,
			 const jack_client_t * jclient, size_t frames,
			 size_t frames_per_iter)
{
  jt_cb_counter_init_int (counter, jclient, jt_cb_counter_time_jack, frames,
			  frames_per_iter);
}

void
jt_cb_counter_init_ext (struct jt_cb_counter *counter,
			const jack_client_t * jclient, size_t frames,
			size_t frames_per_iter)
{
  jt_cb_counter_init_int (counter, jclient, jt_cb_counter_time_ext, frames,
			  frames_per_iter);
}
