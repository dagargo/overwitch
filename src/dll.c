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
#include <stdlib.h>
#include "utils.h"
#include "dll.h"

#define ERR_TUNED_THRES 2
#define USEC_PER_SEC 1.0e6
#define SEC_PER_USEC 1.0e-6

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/alsathread.cc
//Transform us to seconds only considering the lowest 28 bits
#define UINT64_USEC_TO_DOUBLE_SEC(t) (SEC_PER_USEC * (int)(t & 0x0FFFFFFF))
#define MODTIME_THRESHOLD 200	//A value smaller than the maximum returned by UINT64_USEC_TO_DOUBLE_SEC

double
wrap_time (double d, double q)
{
  if (d < -MODTIME_THRESHOLD)
    {
      d += q;
    }
  if (d > MODTIME_THRESHOLD)
    {
      d -= q;
    }
  return d;
}

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/alsathread.cc.
inline void
ow_dll_overbridge_init (void *data, double samplerate, uint32_t frames)
{
  double w;
  struct ow_dll *dll = data;
  struct ow_dll_overbridge *dll_ob = &dll->dll_overbridge;

  debug_print (2, "Initializing Overbridge side of DLL...");

  dll_ob->dt = frames / (double) samplerate;
  w = 2 * M_PI * 0.1 * dll_ob->dt;
  dll_ob->w1 = 1.6 * w;
  dll_ob->w2 = w * w;
}

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/alsathread.cc.
inline void
ow_dll_overbridge_update (void *data, uint32_t frames, uint64_t t)
{
  double time, err;
  struct ow_dll *dll = data;
  struct ow_dll_overbridge *dll_ob = &dll->dll_overbridge;

  debug_print (4, "Updating Overbridge side of DLL...");

  time = UINT64_USEC_TO_DOUBLE_SEC (t);

  if (dll_ob->boot)
    {
      dll_ob->i0.time = time;
      dll_ob->i1.time = dll_ob->i0.time + dll_ob->dt;

      dll_ob->i0.frames = 0;
      dll_ob->i1.frames = frames;
      dll_ob->boot = 0;
    }

  err = time - dll_ob->i1.time;
  if (err < -MODTIME_THRESHOLD)
    {
      dll_ob->i1.time -= dll->t_quantum;
      err = time - dll_ob->i1.time;
    }

  dll_ob->i0.time = dll_ob->i1.time;
  dll_ob->i1.time += dll_ob->w1 * err + dll_ob->dt;
  dll_ob->dt += dll_ob->w2 * err;

  dll_ob->i0.frames = dll_ob->i1.frames;
  dll_ob->i1.frames += frames;

  debug_print (4, "time: %3.6f; t0: %3.6f: t1: %3.6f; f0: % 8d; f1: % 8d",
	       time, dll_ob->i0.time, dll_ob->i1.time, dll_ob->i0.frames,
	       dll_ob->i1.frames);
}

//The whole calculation of the target_delay and the loop filter is taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
inline void
ow_dll_host_update_error (struct ow_dll *dll, uint64_t t)
{
  double delta_overbridge, dn, dd;
  int32_t delta_frames_exp, delta_frames_act;
  double time = UINT64_USEC_TO_DOUBLE_SEC (t);

  debug_print (4, "Updating error in host side of DLL...");

  delta_frames_exp = dll->i1.frames - dll->i0.frames;
  dn = wrap_time (time - dll->i0.time, dll->t_quantum);
  dd = wrap_time (dll->i1.time - dll->i0.time, dll->t_quantum);
  delta_overbridge = delta_frames_exp * dn / dd;
  delta_frames_act = dll->i0.frames - dll->frames;	// - (dll->frames + dll->i0.frames)
  dll->err = delta_frames_act + delta_overbridge - dll->target_delay;

  if (dll->boot)
    {
      int n = (int) (floor (dll->err + 0.5));
      dll->frames += n;
      dll->err -= n;
      dll->boot = 0;
    }

  debug_print (4,
	       "delta_frames_exp: %d; delta_frames_act: %d; delta_overbridge: %f; DLL target delay: %d; DLL error: %f",
	       delta_frames_exp, delta_frames_act, delta_overbridge,
	       dll->target_delay, dll->err);
}

inline void
ow_dll_host_update (struct ow_dll *dll)
{
  debug_print (4, "Updating host side of DLL...");

  dll->z1 += dll->w0 * (dll->w1 * dll->err - dll->z1);
  dll->z2 += dll->w0 * (dll->z1 - dll->z2);
  dll->z3 += dll->w2 * dll->z2;
  dll->ratio = 1.0 - dll->z2 - dll->z3;
}

inline void
ow_dll_host_init (struct ow_dll *dll)
{
  debug_print (2, "Initializing host side of DLL...");
  dll->set = 0;
  dll->boot = 1;
  dll->dll_overbridge.boot = 1;
  dll->t_quantum = ldexp (1e-6, 28);	//28 bits as used in UINT64_USEC_TO_DOUBLE_SEC
}

inline void
ow_dll_host_reset (struct ow_dll *dll, double output_samplerate,
		   double input_samplerate, uint32_t output_frames,
		   uint32_t input_frames)
{
  debug_print (2, "Resetting the DLL...");

  dll->set = 1;

  dll->z1 = 0.0;
  dll->z2 = 0.0;
  dll->z3 = 0.0;

  dll->ratio = output_samplerate / input_samplerate;

  dll->frames = -input_frames / dll->ratio;

  dll->target_delay = 2.0 * input_frames + 1.5 * output_frames;
}

//Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
inline void
ow_dll_host_set_loop_filter (struct ow_dll *dll, double bw,
			     uint32_t output_frames, double output_samplerate)
{
  double w = 2.0 * M_PI * 20 * bw * output_frames / output_samplerate;
  dll->w0 = 1.0 - exp (-w);
  w = 2.0 * M_PI * bw * dll->ratio / output_samplerate;
  dll->w1 = w * 1.6;
  dll->w2 = w * output_frames / 1.6;
}

inline void
ow_dll_host_load_dll_overbridge (struct ow_dll *dll)
{
  dll->i0 = dll->dll_overbridge.i0;
  dll->i1 = dll->dll_overbridge.i1;
}

inline int
ow_dll_tuned (struct ow_dll *dll)
{
  return abs (dll->err) < ERR_TUNED_THRES;
}
