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
#include "overwitch.h"

struct ow_resampler
{
  ow_resampler_status_t status;
  struct ow_engine *engine;
  struct ow_dll dll;		//The DLL is based on o2j data
  double o2h_ratio;
  double h2o_ratio;
  SRC_STATE *h2o_state;
  SRC_STATE *o2h_state;
  float *h2o_buf_in;
  float *h2o_buf_out;
  float *h2o_aux;
  float *h2o_queue;
  float *o2h_buf_in;
  float *o2h_buf_out;
  size_t h2o_queue_len;
  int log_control_cycles;
  int log_cycles;
  int reading_at_o2h_end;
  //These frame sizes can differ from the engine frame sizes when
  //the device is using fewer than 4 bytes for tracks and are always
  //based on sizeof(float).
  size_t o2h_frame_size;
  size_t h2o_frame_size;
  size_t o2h_bufsize;
  size_t h2o_bufsize;
  uint32_t bufsize;
  double samplerate;
  double max_target_ratio;
  double min_target_ratio;
  struct ow_resampler_reporter reporter;
};
