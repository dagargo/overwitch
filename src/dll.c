/*
 *   dll.c
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

#include <math.h>
#include "utils.h"
#include "dll.h"

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/alsathread.cc.
inline void
ow_dll_overwitch_init (struct ow_dll_overwitch *dll_ow, double samplerate,
		       int frames_per_transfer, double time)
{
  double dtime = frames_per_transfer / samplerate;
  double w = 2 * M_PI * 0.1 * dtime;
  dll_ow->b = 1.6 * w;
  dll_ow->c = w * w;

  dll_ow->e2 = dtime;
  dll_ow->i0.time = time;
  dll_ow->i1.time = dll_ow->i0.time + dll_ow->e2;

  dll_ow->i0.frames = 0;
  dll_ow->i1.frames = frames_per_transfer;
}

inline void
ow_dll_overwitch_inc (struct ow_dll_overwitch *dll_ow,
		      int frames_per_transfer, double time)
{
  double e = time - dll_ow->i1.time;
  dll_ow->i0.time = dll_ow->i1.time;
  dll_ow->i1.time += dll_ow->b * e + dll_ow->e2;
  dll_ow->e2 += dll_ow->c * e;
  dll_ow->i0.frames = dll_ow->i1.frames;
  dll_ow->i1.frames += frames_per_transfer;
}

//The whole calculation of the delay and the loop filter is taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
inline void
ow_dll_primary_update_err (struct ow_dll *dll, double time)
{
  double tj = time;
  uint32_t frames = dll->ko1 - dll->ko0;
  double dob = frames * (tj - dll->to0) / (dll->to1 - dll->to0);
  int n =
    dll->ko0 > dll->kj ? dll->ko0 - dll->kj : -(int) (dll->kj - dll->ko0);
  dll->err = n + dob - dll->kdel;
}

inline void
ow_dll_primary_update (struct ow_dll *dll)
{
  dll->_z1 += dll->_w0 * (dll->_w1 * dll->err - dll->_z1);
  dll->_z2 += dll->_w0 * (dll->_z1 - dll->_z2);
  dll->_z3 += dll->_w2 * dll->_z2;
  dll->ratio = 1.0 - dll->_z2 - dll->_z3;

  dll->ratio_sum += dll->ratio;
}

inline void
ow_dll_primary_calc_avg (struct ow_dll *dll, int cycles)
{
  dll->last_ratio_avg = dll->ratio_avg;
  dll->ratio_avg = dll->ratio_sum / cycles;
  dll->ratio_sum = 0.0;
}

inline void
ow_dll_primary_init (struct ow_dll *dll, double output_samplerate,
		     double input_samplerate, int output_frames_per_transfer,
		     int input_frames_per_transfer)
{
  dll->_z1 = 0.0;
  dll->_z2 = 0.0;
  dll->_z3 = 0.0;
  dll->ratio_sum = 0.0;
  dll->ratio_avg = 0.0;
  dll->last_ratio_avg = 0.0;

  dll->ratio = output_samplerate / input_samplerate;

  dll->kj = -input_frames_per_transfer / dll->ratio;

  dll->kdel =
    2.0 * input_frames_per_transfer + 1.5 * output_frames_per_transfer;

  debug_print (2, "Target delay: %.1f ms (%d frames)\n",
	       dll->kdel * 1000 / input_samplerate, dll->kdel);
}

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
inline void
ow_dll_primary_first_time_run (struct ow_dll *dll)
{
  int n = (int) (floor (dll->err + 0.5));
  dll->kj += n;
  dll->err -= n;
}

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
inline void
ow_dll_primary_set_loop_filter (struct ow_dll *dll, double bw,
				int output_frames_per_transfer,
				double output_samplerate)
{
  double w =
    2.0 * M_PI * 20 * bw * output_frames_per_transfer / output_samplerate;
  dll->_w0 = 1.0 - exp (-w);
  w = 2.0 * M_PI * bw * dll->ratio / output_samplerate;
  dll->_w1 = w * 1.6;
  dll->_w2 = w * output_frames_per_transfer / 1.6;
}

inline void
ow_dll_primary_load_dll_overwitch (struct ow_dll *dll)
{
  dll->ko0 = dll->dll_ow.i0.frames;
  dll->to0 = dll->dll_ow.i0.time;
  dll->ko1 = dll->dll_ow.i1.frames;
  dll->to1 = dll->dll_ow.i1.time;
}
