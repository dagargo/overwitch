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
#include <math.h>
#include <string.h>
#include "utils.h"

int debug_level = 0;

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/alsathread.cc.
inline void
dll_counter_init (struct dll_counter *dll_counter, double samplerate,
		  int frames_per_transfer)
{
  double dtime = frames_per_transfer / samplerate;
  double w = 2 * M_PI * 0.1 * dtime;
  dll_counter->b = 1.6 * w;
  dll_counter->c = w * w;

  //TODO: add this to xrun handler and perhaps clear the buffers. See paper.
  dll_counter->e2 = dtime;
  dll_counter->i0.time = jack_get_time () * 1.0e-6;
  dll_counter->i1.time = dll_counter->i0.time + dll_counter->e2;

  dll_counter->i0.frames = 0;
  dll_counter->i1.frames = frames_per_transfer;
}

inline void
dll_counter_inc (struct dll_counter *dll_counter, int frames_per_transfer)
{
  double e = jack_get_time () * 1.0e-6 - dll_counter->i1.time;
  dll_counter->i0.time = dll_counter->i1.time;
  dll_counter->i1.time += dll_counter->b * e + dll_counter->e2;
  dll_counter->e2 += dll_counter->c * e;
  dll_counter->i0.frames = dll_counter->i1.frames;
  dll_counter->i1.frames += frames_per_transfer;
}

//The whole calculation of the delay and the loop filter is taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
inline void
dll_update_err (struct dll *dll, jack_time_t current_usecs)
{
  double tj = current_usecs * 1.0e-6;
  jack_nframes_t frames = dll->ko1 - dll->ko0;
  double dob = frames * (tj - dll->to0) / (dll->to1 - dll->to0);
  frames = dll->ko0 - dll->kj;
  dll->err = frames + dob - dll->kdel;
}

inline void
dll_update (struct dll *dll)
{
  dll->_z1 += dll->_w0 * (dll->_w1 * dll->err - dll->_z1);
  dll->_z2 += dll->_w0 * (dll->_z1 - dll->_z2);
  dll->_z3 += dll->_w2 * dll->_z2;
  dll->ratio = 1.0 - dll->_z2 - dll->_z3;
  if (dll->ratio > dll->ratio_max)
    {
      dll->ratio = dll->ratio_max;
    }
  if (dll->ratio < dll->ratio_min)
    {
      dll->ratio = dll->ratio_min;
    }
}

inline void
dll_init (struct dll *dll, double output_samplerate, double input_samplerate,
	  int output_frames_per_transfer, int input_frames_per_transfer)
{
  dll->_z1 = 0.0;
  dll->_z2 = 0.0;
  dll->_z3 = 0.0;
  dll->ratio_sum = 0.0;
  dll->ratio_avg = 0.0;
  dll->last_ratio_avg = 0.0;

  dll->ratio = output_samplerate / input_samplerate;
  dll->ratio_max = 1.05 * dll->ratio;
  dll->ratio_min = 0.95 * dll->ratio;

  dll->kj = -input_frames_per_transfer / dll->ratio;

  dll->kdel = input_frames_per_transfer + 1.5 * output_frames_per_transfer;
}

inline void
dll_set_loop_filter (struct dll *dll, double bw,
		     int output_frames_per_transfer, double output_samplerate)
{
  //Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
  double w =
    2.0 * M_PI * 20 * bw * output_frames_per_transfer / output_samplerate;
  dll->_w0 = 1.0 - exp (-w);
  w = 2.0 * M_PI * bw * dll->ratio / output_samplerate;
  dll->_w1 = w * 1.6;
  dll->_w2 = w * output_frames_per_transfer / 1.6;
}
