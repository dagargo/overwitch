/*
 *   resampler.h
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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

#include "dll.h"
#include "engine.h"

struct ow_resampler
{
  ow_resampler_status_t status;
  struct ow_engine ow;
  struct dll dll;		//The DLL is based on o2j data
  double o2p_ratio;
  double p2o_ratio;
  SRC_STATE *p2o_state;
  SRC_STATE *o2p_state;
  size_t p2o_latency;
  size_t o2p_latency;
  size_t p2o_max_latency;
  size_t o2p_max_latency;
  float *p2o_buf_in;
  float *p2o_buf_out;
  float *p2o_aux;
  float *p2o_queue;
  float *o2p_buf_in;
  float *o2p_buf_out;
  size_t p2o_queue_len;
  int log_control_cycles;
  int log_cycles;
  int xruns;
  pthread_spinlock_t lock;	//Used to synchronize access to xruns.
  int reading_at_o2p_end;
  size_t o2p_buf_size;
  size_t p2o_buf_size;
  uint32_t bufsize;
  double samplerate;
};
