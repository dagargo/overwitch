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

typedef enum
{
  RES_STATUS_ERROR = -1,
  RES_STATUS_STOP,
  RES_STATUS_READY,
  RES_STATUS_BOOT,
  RES_STATUS_TUNE,
  RES_STATUS_RUN
} resampler_status_t;

struct ow_resampler
{
  resampler_status_t status;
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

ow_engine_err_t ow_resampler_init (struct ow_resampler *, int, int, int, int);

int ow_resampler_activate (struct ow_resampler *, uint64_t, int,
			   ow_engine_set_rt_priority_t);

void ow_resampler_wait (struct ow_resampler *);

void ow_resampler_destroy (struct ow_resampler *);

void ow_resampler_print_status (struct ow_resampler *);

void ow_resampler_reset_buffers (struct ow_resampler *);

void ow_resampler_reset_dll (struct ow_resampler *, uint32_t);

void ow_resampler_read_audio (struct ow_resampler *);

void ow_resampler_write_audio (struct ow_resampler *);

int ow_resampler_compute_ratios (struct ow_resampler *, double);

void ow_resampler_inc_xruns (struct ow_resampler *);
