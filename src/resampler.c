/*
 *   resampler.c
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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "resampler.h"

#define MAX_READ_FRAMES 5
#define STARTUP_TIME 5
#define DEFAULT_REPORT_PERIOD 2

inline void
ow_resampler_report_status (struct ow_resampler *resampler)
{
  size_t o2p_latency_s, o2p_max_latency_s, p2o_latency_s, p2o_max_latency_s;
  double o2p_latency_d, o2p_max_latency_d, p2o_latency_d, p2o_max_latency_d;
  ow_engine_status_t status;
  int p2o_audio_enabled;

  pthread_spin_lock (&resampler->engine->lock);
  o2p_latency_s = resampler->engine->o2p_latency;
  o2p_max_latency_s = resampler->engine->o2p_max_latency;
  p2o_latency_s = resampler->engine->p2o_latency;
  p2o_max_latency_s = resampler->engine->p2o_max_latency;
  p2o_audio_enabled = resampler->engine->options.p2o_audio;
  status = resampler->engine->status;
  pthread_spin_unlock (&resampler->engine->lock);

  if (status == OW_ENGINE_STATUS_RUN)
    {
      o2p_latency_d =
	o2p_latency_s * 1000.0 / (resampler->engine->o2p_frame_size *
				  OB_SAMPLE_RATE);
      o2p_max_latency_d =
	o2p_max_latency_s * 1000.0 / (resampler->engine->o2p_frame_size *
				      OB_SAMPLE_RATE);
    }
  else
    {
      o2p_latency_d = -1.0;
      o2p_max_latency_d = -1.0;
    }

  if (p2o_audio_enabled)
    {
      p2o_latency_d =
	p2o_latency_s * 1000.0 / (resampler->engine->p2o_frame_size *
				  OB_SAMPLE_RATE);
      p2o_max_latency_d =
	p2o_max_latency_s * 1000.0 / (resampler->engine->p2o_frame_size *
				      OB_SAMPLE_RATE);
    }
  else
    {
      p2o_latency_d = -1.0;
      p2o_max_latency_d = -1.0;
    }

  if (debug_level)
    {
      printf
	("%s: o2p latency: %4.1f ms, max. %4.1f ms; p2o latency: %4.1f ms, max. %4.1f ms, o2p ratio: %f, avg. %f\n",
	 resampler->engine->name, o2p_latency_d, o2p_max_latency_d,
	 p2o_latency_d, p2o_max_latency_d, resampler->dll.ratio,
	 resampler->dll.ratio_avg);
    }

  if (resampler->reporter.callback)
    {
      resampler->reporter.callback (resampler->reporter.data, o2p_latency_d,
				    p2o_latency_d, o2p_max_latency_d,
				    p2o_max_latency_d, resampler->o2p_ratio,
				    resampler->p2o_ratio);
    }
}

void
ow_resampler_reset_buffers (struct ow_resampler *resampler)
{
  size_t rso2p, bytes;
  size_t frame_size = resampler->engine->o2p_frame_size;
  struct ow_context *context = resampler->engine->context;

  resampler->o2p_bufsize =
    resampler->bufsize * resampler->engine->o2p_frame_size;
  resampler->p2o_bufsize =
    resampler->bufsize * resampler->engine->p2o_frame_size;

  if (resampler->p2o_aux)
    {
      free (resampler->p2o_aux);
      free (resampler->p2o_buf_out);
      free (resampler->p2o_buf_in);
      free (resampler->p2o_queue);
      free (resampler->o2p_buf_in);
      free (resampler->o2p_buf_out);
    }

  //The 8 times scale allow up to more than 192 kHz sample rate in JACK.
  resampler->p2o_buf_in = malloc (resampler->p2o_bufsize);
  resampler->p2o_buf_out = malloc (resampler->p2o_bufsize * 8);
  resampler->p2o_aux = malloc (resampler->p2o_bufsize * 8);
  resampler->p2o_queue = malloc (resampler->p2o_bufsize * 8);
  resampler->p2o_queue_len = 0;

  resampler->o2p_buf_in = malloc (resampler->o2p_bufsize);
  resampler->o2p_buf_out = malloc (resampler->o2p_bufsize);

  memset (resampler->p2o_aux, 0, resampler->p2o_bufsize);
  memset (resampler->o2p_buf_in, 0, resampler->p2o_bufsize);

  resampler->reading_at_o2p_end = 0;

  rso2p = context->read_space (context->o2p_audio);
  bytes = ow_bytes_to_frame_bytes (rso2p, frame_size);
  context->read (context->o2p_audio, NULL, bytes);
}

void
ow_resampler_reset_dll (struct ow_resampler *resampler,
			uint32_t new_samplerate)
{
  if (resampler->dll.set
      && ow_engine_get_status (resampler->engine) == OW_ENGINE_STATUS_RUN)
    {
      debug_print (2, "Just adjusting DLL ratio...\n");
      resampler->dll.ratio =
	resampler->dll.last_ratio_avg * new_samplerate /
	resampler->samplerate;
      ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_READY);
      resampler->log_cycles = 0;
      resampler->log_control_cycles =
	STARTUP_TIME * new_samplerate / resampler->bufsize;
    }
  else
    {
      debug_print (2, "Resetting the DLL...\n");
      ow_dll_primary_reset (&resampler->dll, new_samplerate, OB_SAMPLE_RATE,
			    resampler->bufsize,
			    resampler->engine->frames_per_transfer);
      ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_READY);
    }

  resampler->o2p_ratio = resampler->dll.ratio;
  resampler->samplerate = new_samplerate;
}

static long
resampler_p2o_reader (void *cb_data, float **data)
{
  long ret;
  struct ow_resampler *resampler = cb_data;

  *data = resampler->p2o_aux;

  if (resampler->p2o_queue_len == 0)
    {
      debug_print (2, "p2o: Can not read data from queue\n");
      return resampler->bufsize;
    }

  ret = resampler->p2o_queue_len;
  memcpy (resampler->p2o_aux, resampler->p2o_queue,
	  ret * resampler->engine->p2o_frame_size);
  resampler->p2o_queue_len = 0;

  return ret;
}

static long
resampler_o2p_reader (void *cb_data, float **data)
{
  size_t rso2p;
  size_t bytes;
  long frames;
  static int last_frames = 1;
  struct ow_resampler *resampler = cb_data;

  *data = resampler->o2p_buf_in;

  rso2p =
    resampler->engine->context->read_space (resampler->engine->context->
					    o2p_audio);
  if (resampler->reading_at_o2p_end)
    {
      if (rso2p >= resampler->engine->o2p_frame_size)
	{
	  frames = rso2p / resampler->engine->o2p_frame_size;
	  frames = frames > MAX_READ_FRAMES ? MAX_READ_FRAMES : frames;
	  bytes = frames * resampler->engine->o2p_frame_size;
	  resampler->engine->context->read (resampler->engine->
					    context->o2p_audio,
					    (void *) resampler->o2p_buf_in,
					    bytes);
	}
      else
	{
	  debug_print (2,
		       "o2p: Audio ring buffer underflow (%zu < %zu). Replicating last sample...\n",
		       rso2p, resampler->engine->o2p_transfer_size);
	  if (last_frames > 1)
	    {
	      uint64_t pos =
		(last_frames - 1) * resampler->engine->device_desc->outputs;
	      memcpy (resampler->o2p_buf_in, &resampler->o2p_buf_in[pos],
		      resampler->engine->o2p_frame_size);
	    }
	  frames = MAX_READ_FRAMES;
	}
    }
  else
    {
      if (rso2p >= resampler->o2p_bufsize)
	{
	  debug_print (2, "o2p: Emptying buffer and running...\n");
	  bytes = ow_bytes_to_frame_bytes (rso2p, resampler->o2p_bufsize);
	  resampler->engine->context->read (resampler->engine->
					    context->o2p_audio, NULL, bytes);
	  resampler->reading_at_o2p_end = 1;
	}
      frames = MAX_READ_FRAMES;
    }

  resampler->dll.kj += frames;
  last_frames = frames;
  return frames;
}

void
ow_resampler_read_audio (struct ow_resampler *resampler)
{
  long gen_frames;

  gen_frames =
    src_callback_read (resampler->o2p_state, resampler->o2p_ratio,
		       resampler->bufsize, resampler->o2p_buf_out);
  if (gen_frames != resampler->bufsize)
    {
      error_print
	("o2p: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	 resampler->o2p_ratio, gen_frames, resampler->bufsize);
    }
}

void
ow_resampler_write_audio (struct ow_resampler *resampler)
{
  long gen_frames;
  int inc;
  int frames;
  size_t bytes;
  size_t wsp2o;
  static double p2o_acc = .0;

  memcpy (&resampler->p2o_queue
	  [resampler->p2o_queue_len *
	   resampler->engine->p2o_frame_size], resampler->p2o_buf_in,
	  resampler->p2o_bufsize);
  resampler->p2o_queue_len += resampler->bufsize;

  p2o_acc += resampler->bufsize * (resampler->p2o_ratio - 1.0);
  inc = trunc (p2o_acc);
  p2o_acc -= inc;
  frames = resampler->bufsize + inc;

  gen_frames =
    src_callback_read (resampler->p2o_state,
		       resampler->p2o_ratio, frames, resampler->p2o_buf_out);
  if (gen_frames != frames)
    {
      error_print
	("p2o: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	 resampler->p2o_ratio, gen_frames, frames);
    }

  if (resampler->status < OW_RESAMPLER_STATUS_RUN)
    {
      return;
    }

  bytes = gen_frames * resampler->engine->p2o_frame_size;
  wsp2o =
    resampler->engine->context->write_space (resampler->engine->context->
					     p2o_audio);

  if (bytes <= wsp2o)
    {
      resampler->engine->context->write (resampler->engine->context->
					 p2o_audio,
					 (void *) resampler->p2o_buf_out,
					 bytes);
    }
  else
    {
      error_print ("p2o: Audio ring buffer overflow. Discarding data...\n");
    }
}

int
ow_resampler_compute_ratios (struct ow_resampler *resampler, double time)
{
  int xruns;
  ow_engine_status_t engine_status;
  struct ow_dll *dll = &resampler->dll;

  pthread_spin_lock (&resampler->lock);
  xruns = resampler->xruns;
  resampler->xruns = 0;
  pthread_spin_unlock (&resampler->lock);

  pthread_spin_lock (&resampler->engine->lock);
  ow_dll_primary_load_dll_overwitch (dll);
  pthread_spin_unlock (&resampler->engine->lock);

  engine_status = ow_engine_get_status (resampler->engine);
  if (resampler->status == OW_RESAMPLER_STATUS_READY
      && engine_status <= OW_ENGINE_STATUS_BOOT)
    {
      if (engine_status == OW_ENGINE_STATUS_READY)
	{
	  ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_BOOT);
	  debug_print (2, "Booting Overbridge side...\n");
	}
      return 1;
    }

  if (resampler->status == OW_RESAMPLER_STATUS_READY
      && engine_status == OW_ENGINE_STATUS_WAIT)
    {
      ow_dll_primary_update_err (dll, time);
      ow_dll_primary_first_time_run (dll);

      debug_print (2, "Starting up resampler...\n");
      ow_dll_primary_set_loop_filter (dll, 1.0, resampler->bufsize,
				      resampler->samplerate);
      resampler->status = OW_RESAMPLER_STATUS_BOOT;

      resampler->log_cycles = 0;
      resampler->log_control_cycles =
	STARTUP_TIME * resampler->samplerate / resampler->bufsize;
      return 0;
    }

  if (xruns)
    {
      debug_print (2, "Fixing %d xruns...\n", xruns);

      //With this, we try to recover from the unreaded frames that are in the o2p buffer and...
      resampler->o2p_ratio = dll->ratio * (1 + xruns);
      resampler->p2o_ratio = 1.0 / resampler->o2p_ratio;
      ow_resampler_read_audio (resampler);

      //... we skip the current cycle DLL update as time masurements are not precise enough and would lead to errors.
      return 0;
    }

  ow_dll_primary_update_err (dll, time);
  ow_dll_primary_update (dll);

  if (dll->ratio < 0.0)
    {
      error_print ("Negative ratio detected. Stopping resampler...\n");
      ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_ERROR);
      return 1;
    }

  resampler->o2p_ratio = dll->ratio;
  resampler->p2o_ratio = 1.0 / resampler->o2p_ratio;

  resampler->log_cycles++;
  if (resampler->log_cycles == resampler->log_control_cycles)
    {
      ow_dll_primary_calc_avg (dll, resampler->log_control_cycles);

      ow_resampler_report_status (resampler);

      resampler->log_cycles = 0;

      if (resampler->status == OW_RESAMPLER_STATUS_BOOT)
	{
	  debug_print (2, "Tunning resampler...\n");
	  ow_dll_primary_set_loop_filter (dll, 0.05, resampler->bufsize,
					  resampler->samplerate);
	  resampler->status = OW_RESAMPLER_STATUS_TUNE;
	  resampler->log_control_cycles =
	    resampler->reporter.period * resampler->samplerate /
	    resampler->bufsize;
	}

      if (resampler->status == OW_RESAMPLER_STATUS_TUNE && ow_dll_tuned (dll))
	{
	  debug_print (2, "Running resampler...\n");
	  ow_dll_primary_set_loop_filter (dll, 0.02, resampler->bufsize,
					  resampler->samplerate);
	  resampler->status = OW_RESAMPLER_STATUS_RUN;
	  ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_RUN);
	}
    }

  return 0;
}

ow_err_t
ow_resampler_init_from_bus_address (struct ow_resampler **resampler_,
				    uint8_t bus, uint8_t address,
				    int blocks_per_transfer, int quality)
{
  struct ow_resampler *resampler = malloc (sizeof (struct ow_resampler));
  ow_err_t err =
    ow_engine_init_from_bus_address (&resampler->engine, bus, address,
				     blocks_per_transfer);
  if (err)
    {
      free (resampler);
      return err;
    }

  *resampler_ = resampler;

  resampler->samplerate = 0;
  resampler->bufsize = 0;
  resampler->xruns = 0;
  resampler->p2o_aux = NULL;
  resampler->status = OW_RESAMPLER_STATUS_READY;

  resampler->p2o_state =
    src_callback_new (resampler_p2o_reader, quality,
		      resampler->engine->device_desc->inputs, NULL,
		      resampler);
  resampler->o2p_state =
    src_callback_new (resampler_o2p_reader, quality,
		      resampler->engine->device_desc->outputs, NULL,
		      resampler);

  pthread_spin_init (&resampler->lock, PTHREAD_PROCESS_SHARED);

  resampler->reporter.callback = NULL;
  resampler->reporter.data = NULL;
  resampler->reporter.period = DEFAULT_REPORT_PERIOD;

  ow_dll_primary_init (&resampler->dll);

  return OW_OK;
}

void
ow_resampler_destroy (struct ow_resampler *resampler)
{
  src_delete (resampler->p2o_state);
  src_delete (resampler->o2p_state);
  if (resampler->p2o_aux)
    {
      free (resampler->p2o_aux);
      free (resampler->p2o_buf_out);
      free (resampler->p2o_buf_in);
      free (resampler->p2o_queue);
      free (resampler->o2p_buf_in);
      free (resampler->o2p_buf_out);
    }
  pthread_spin_destroy (&resampler->lock);
  ow_engine_destroy (resampler->engine);
  free (resampler);
}

ow_err_t
ow_resampler_activate (struct ow_resampler *resampler,
		       struct ow_context *context)
{
  context->dll = &resampler->dll.dll_ow;
  context->dll_init = (ow_dll_overwitch_init_t) ow_dll_overwitch_init;
  context->dll_inc = (ow_dll_overwitch_inc_t) ow_dll_overwitch_inc;
  context->options |= OW_ENGINE_OPTION_DLL;
  return ow_engine_activate (resampler->engine, context);
}

void
ow_resampler_wait (struct ow_resampler *resampler)
{
  ow_engine_wait (resampler->engine);
}

void
ow_resampler_inc_xruns (struct ow_resampler *resampler)
{
  pthread_spin_lock (&resampler->lock);
  resampler->xruns++;
  pthread_spin_unlock (&resampler->lock);
}

inline ow_resampler_status_t
ow_resampler_get_status (struct ow_resampler *resampler)
{
  return resampler->status;
}

inline struct ow_engine *
ow_resampler_get_engine (struct ow_resampler *resampler)
{
  return resampler->engine;
}

inline void
ow_resampler_stop (struct ow_resampler *resampler)
{
  ow_engine_stop (resampler->engine);
}

inline void
ow_resampler_set_buffer_size (struct ow_resampler *resampler,
			      uint32_t bufsize)
{
  if (resampler->bufsize != bufsize)
    {
      resampler->bufsize = bufsize;
      ow_resampler_reset_buffers (resampler);
      ow_resampler_reset_dll (resampler, resampler->samplerate);
    }
}

inline void
ow_resampler_set_samplerate (struct ow_resampler *resampler,
			     uint32_t samplerate)
{
  if (resampler->samplerate != samplerate)
    {
      if (resampler->p2o_buf_in)	//This means that ow_resampler_reset_buffers has been called and thus bufsize has been set.
	{
	  ow_resampler_reset_dll (resampler, samplerate);
	}
      else
	{
	  resampler->samplerate = samplerate;
	}
    }
}

inline size_t
ow_resampler_get_o2p_frame_size (struct ow_resampler *resampler)
{
  return resampler->engine->o2p_frame_size;
}

inline size_t
ow_resampler_get_p2o_frame_size (struct ow_resampler *resampler)
{
  return resampler->engine->p2o_frame_size;
}

inline float *
ow_resampler_get_o2p_audio_buffer (struct ow_resampler *resampler)
{
  return resampler->o2p_buf_out;
}

inline float *
ow_resampler_get_p2o_audio_buffer (struct ow_resampler *resampler)
{
  return resampler->p2o_buf_in;
}

void
ow_resampler_set_report_callback (struct ow_resampler *resampler,
				  const struct ow_resampler_reporter
				  *reporter)
{
  resampler->reporter.callback = reporter->callback;
  resampler->reporter.data = reporter->data;
  resampler->reporter.period =
    reporter->period > 0 ? reporter->period : DEFAULT_REPORT_PERIOD;
}
