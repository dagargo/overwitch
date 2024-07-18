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

#define RATIO_DIFF_THRES 0.00001
#define USEC_PER_SEC 1.0e6

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/alsathread.cc.
inline void
ow_dll_overbridge_init (void *data, double samplerate,
			uint32_t frames_per_transfer, uint64_t time)
{
  struct ow_dll_overbridge *dll_ob = data;
  double dtime = frames_per_transfer / samplerate;
  double w = 2 * M_PI * 0.1 * dtime;
  dll_ob->b = 1.6 * w;
  dll_ob->c = w * w;

  dll_ob->e2 = dtime * USEC_PER_SEC;
  dll_ob->i0.time = time;
  dll_ob->i1.time = dll_ob->i0.time + dll_ob->e2;

  dll_ob->i0.frames = 0;
  dll_ob->i1.frames = frames_per_transfer;
}

inline void
ow_dll_overbridge_inc (void *data, uint32_t frames, uint64_t time)
{
  struct ow_dll_overbridge *dll_ob = data;
  uint64_t e = time - dll_ob->i1.time;
  dll_ob->i0.time = dll_ob->i1.time;
  dll_ob->i1.time += dll_ob->b * e + dll_ob->e2;
  dll_ob->e2 += dll_ob->c * e;
  dll_ob->i0.frames = dll_ob->i1.frames;
  dll_ob->i1.frames += frames;
}

//The whole calculation of the delay and the loop filter is taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
inline void
ow_dll_primary_update_err (struct ow_dll *dll, uint64_t time)
{
  uint64_t tj = time;
  uint32_t frames = dll->ko1 - dll->ko0;
  double dob = frames * (tj - dll->to0) / (dll->to1 - dll->to0);
  int n = dll->ko0 > dll->kj ? dll->ko0 - dll->kj : -(dll->kj - dll->ko0);
  dll->err = n + dob - dll->kdel;
}

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
inline void
ow_dll_primary_update_err_first_time (struct ow_dll *dll, uint64_t time)
{
  ow_dll_primary_update_err (dll, time);
  int n = (int) (floor (dll->err + 0.5));
  dll->kj += n;
  dll->err -= n;
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
ow_dll_primary_init (struct ow_dll *dll)
{
  dll->set = 0;
}

inline void
ow_dll_primary_reset (struct ow_dll *dll, double output_samplerate,
		      double input_samplerate, int output_frames_per_transfer,
		      int input_frames_per_transfer)
{
  dll->set = 1;
  dll->_z1 = 0.0;
  dll->_z2 = 0.0;
  dll->_z3 = 0.0;
  dll->ratio_sum = 0.0;
  dll->ratio_avg = 0.0;
  dll->last_ratio_avg = 0.0;

  dll->ratio = output_samplerate / input_samplerate;

  dll->kj = -input_frames_per_transfer / dll->ratio;

  dll->kdel = 2.0 * input_frames_per_transfer +
    1.5 * output_frames_per_transfer;

  debug_print (2, "Target delay: %.1f ms (%d frames)\n",
	       dll->kdel * 1000 / input_samplerate, dll->kdel);
}

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
inline void
ow_dll_primary_set_loop_filter (struct ow_dll *dll, double bw,
				int output_frames_per_transfer,
				double output_samplerate)
{
  double w = 2.0 * M_PI * 20 * bw * output_frames_per_transfer /
    output_samplerate;
  dll->_w0 = 1.0 - exp (-w);
  w = 2.0 * M_PI * bw * dll->ratio / output_samplerate;
  dll->_w1 = w * 1.6;
  dll->_w2 = w * output_frames_per_transfer / 1.6;
}

inline void
ow_dll_primary_load_dll_overwitch (struct ow_dll *dll)
{
  dll->ko0 = dll->dll_overbridge.i0.frames;
  dll->to0 = dll->dll_overbridge.i0.time;
  dll->ko1 = dll->dll_overbridge.i1.frames;
  dll->to1 = dll->dll_overbridge.i1.time;
}

inline int
ow_dll_tuned (struct ow_dll *dll)
{
  double diff = fabs (dll->ratio_avg - dll->last_ratio_avg);
  return (diff < RATIO_DIFF_THRES);
}
