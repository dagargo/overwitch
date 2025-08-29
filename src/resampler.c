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
#include "overwitch.h"

#define MAX_READ_FRAMES 5
#define DEFAULT_REPORT_PERIOD 2

#define BOOTING_PERIOD_US (3 * USEC_PER_SEC)
#define TUNING_PERIOD_US (4 * USEC_PER_SEC)

#define BOOTING_ERROR 0.001
#define TUNING_ERROR 0.00001

#define OB_PERIOD_MS (1000.0 / OB_SAMPLE_RATE)

#define HOST_UPDATE_ERROR 0.05

inline ow_resampler_status_t
ow_resampler_get_status (struct ow_resampler *resampler)
{
  ow_resampler_status_t status;
  pthread_spin_lock (&resampler->lock);
  status = resampler->status;
  pthread_spin_unlock (&resampler->lock);
  return status;
}

inline void
ow_resampler_set_status (struct ow_resampler *resampler,
			 ow_resampler_status_t status)
{
  pthread_spin_lock (&resampler->lock);
  resampler->status = status;
  pthread_spin_unlock (&resampler->lock);
}

static inline void
ow_resampler_set_latency (struct ow_resampler *resampler)
{
  struct ow_resampler_state *state = &resampler->state;

  pthread_spin_lock (&resampler->engine->lock);
  state->f_latency_h2o = resampler->engine->latency_h2o;
  state->f_latency_h2o_min = MAX (resampler->engine->latency_h2o_min,
				  resampler->bufsize);
  state->f_latency_h2o_max = MAX (resampler->engine->latency_h2o_max,
				  resampler->bufsize);

  state->f_latency_o2h = resampler->engine->latency_o2h;
  state->f_latency_o2h_min = MAX (resampler->engine->latency_o2h_min,
				  resampler->bufsize);
  state->f_latency_o2h_max = MAX (resampler->engine->latency_o2h_max,
				  resampler->bufsize);
  pthread_spin_unlock (&resampler->engine->lock);
}

static inline void
ow_resampler_set_state (struct ow_resampler *resampler)
{
  ow_engine_status_t status;
  struct ow_resampler_state *state = &resampler->state;

  pthread_spin_lock (&resampler->lock);

  ow_resampler_set_latency (resampler);

  status = ow_engine_get_status (resampler->engine);

  int h2o_enabled = ow_engine_is_option (resampler->engine,
					 OW_ENGINE_OPTION_H2O_AUDIO);

  if (status == OW_ENGINE_STATUS_RUN)
    {
      state->t_latency_o2h = state->f_latency_o2h * OB_PERIOD_MS;
      state->t_latency_o2h_max = state->f_latency_o2h_max * OB_PERIOD_MS;
      state->t_latency_o2h_min = state->f_latency_o2h_min * OB_PERIOD_MS;

      if (h2o_enabled)
	{
	  state->t_latency_h2o = state->f_latency_h2o * OB_PERIOD_MS;
	  state->t_latency_h2o_max = state->f_latency_h2o_max * OB_PERIOD_MS;
	  state->t_latency_h2o_min = state->f_latency_h2o_min * OB_PERIOD_MS;
	}
      else
	{
	  state->t_latency_h2o = -1.0;
	  state->t_latency_h2o_max = -1.0;
	  state->t_latency_h2o_min = -1.0;
	}
    }
  else
    {
      state->t_latency_o2h = -1.0;
      state->t_latency_o2h_max = -1.0;
      state->t_latency_o2h_min = -1.0;

      state->t_latency_h2o = -1.0;
      state->t_latency_h2o_max = -1.0;
      state->t_latency_h2o_min = -1.0;
    }

  state->ratio_o2h = resampler->o2h_ratio;
  state->ratio_h2o = resampler->h2o_ratio;

  state->status = resampler->status;

  pthread_spin_unlock (&resampler->lock);
}

static inline void
ow_resampler_report_state (struct ow_resampler *resampler)
{
  struct ow_resampler_state *state = &resampler->state;

  ow_resampler_set_state (resampler);

  if (debug_level > 1)
    {
      fprintf (stderr,
	       "%s (%s): o2h latency: %4.1f [%4.1f, %4.1f] ms; h2o latency: %4.1f [%4.1f, %4.1f] ms, o2h ratio: %f\n",
	       resampler->engine->name, resampler->engine->overbridge_name,
	       state->t_latency_o2h, state->t_latency_o2h_min,
	       state->t_latency_o2h_max, state->t_latency_h2o,
	       state->t_latency_h2o_min, state->t_latency_h2o_max,
	       state->ratio_o2h);
    }
}

void
ow_resampler_clear_buffers (struct ow_resampler *resampler)
{
  size_t rso2h, bytes;
  struct ow_context *context = resampler->engine->context;

  debug_print (2, "Clearing buffers...");

  resampler->h2o_queue_len = 0;
  resampler->reading_at_o2h_end = 0;

  if (context && context->o2h_audio)
    {
      rso2h = context->read_space (context->o2h_audio);
      bytes = ow_bytes_to_frame_bytes (rso2h, resampler->o2h_frame_size);
      context->read (context->o2h_audio, NULL, bytes);
    }

  ow_engine_clear_buffers (resampler->engine);
}

static void
ow_resampler_reset_buffers (struct ow_resampler *resampler)
{
  debug_print (2, "Resetting buffers...");

  resampler->o2h_bufsize = resampler->bufsize * resampler->o2h_frame_size;
  resampler->h2o_bufsize = resampler->bufsize * resampler->h2o_frame_size;

  if (resampler->h2o_aux)
    {
      free (resampler->h2o_aux);
      free (resampler->h2o_buf_out);
      free (resampler->h2o_buf_in);
      free (resampler->h2o_queue);
      free (resampler->o2h_buf_in);
      free (resampler->o2h_buf_out);
    }

  //The 8 times scale allow up to more than 192 kHz sample rate in JACK.
  resampler->h2o_buf_in = malloc (resampler->h2o_bufsize);
  resampler->h2o_buf_out = malloc (resampler->h2o_bufsize * 8);
  resampler->h2o_aux = malloc (resampler->h2o_bufsize * 8);
  resampler->h2o_queue = malloc (resampler->h2o_bufsize * 8);

  resampler->o2h_buf_in = malloc (resampler->o2h_bufsize);
  resampler->o2h_buf_out = malloc (resampler->o2h_bufsize);

  memset (resampler->h2o_aux, 0, resampler->h2o_bufsize);
  memset (resampler->o2h_buf_in, 0, resampler->h2o_bufsize);

  ow_resampler_clear_buffers (resampler);
}

double
ow_resampler_get_target_delay_ms (struct ow_resampler *resampler)
{
  return resampler->dll.target_delay * 1000 / OB_SAMPLE_RATE;
}

static inline void
ow_resampler_set_ratios_from_dll (struct ow_resampler *resampler)
{
  resampler->o2h_ratio = resampler->dll.ratio;
  resampler->h2o_ratio = 1.0 / resampler->o2h_ratio;
}

static inline void
ow_resampler_reset_dll (struct ow_resampler *resampler)
{
  ow_dll_host_reset (&resampler->dll, resampler->samplerate, OB_SAMPLE_RATE,
		     resampler->bufsize,
		     resampler->engine->frames_per_transfer);

  ow_resampler_set_ratios_from_dll (resampler);
}

void
ow_resampler_reset (struct ow_resampler *resampler)
{
  ow_engine_status_t engine_status = ow_engine_get_status (resampler->engine);

  debug_print (1, "Resetting resampler...");

  pthread_spin_lock (&resampler->engine->lock);
  ow_dll_host_init (&resampler->dll);
  pthread_spin_unlock (&resampler->engine->lock);

  ow_resampler_reset_dll (resampler);

  //If the engine has not booted yet, we need to let it boot by itself and can not force the transition.
  if (engine_status > OW_ENGINE_STATUS_BOOT)
    {
      ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_BOOT);
    }
  ow_resampler_set_status (resampler, OW_RESAMPLER_STATUS_READY);
  ow_resampler_clear_buffers (resampler);
}

static long
resampler_h2o_reader (void *cb_data, float **data)
{
  long ret;
  struct ow_resampler *resampler = cb_data;

  *data = resampler->h2o_aux;

  if (resampler->h2o_queue_len == 0)
    {
      debug_print (2, "h2o: Can not read data from queue");
      return resampler->bufsize;
    }

  ret = resampler->h2o_queue_len;
  memcpy (resampler->h2o_aux, resampler->h2o_queue,
	  ret * resampler->h2o_frame_size);
  resampler->h2o_queue_len = 0;

  return ret;
}

static long
resampler_o2h_reader (void *cb_data, float **data)
{
  size_t rso2h;
  size_t bytes;
  long frames;
  struct ow_resampler *resampler = cb_data;
  struct ow_context *context = resampler->engine->context;

  *data = resampler->o2h_buf_in;

  rso2h = context->read_space (context->o2h_audio);
  if (resampler->reading_at_o2h_end)
    {
      if (rso2h >= resampler->o2h_frame_size)
	{
	  frames = rso2h / resampler->o2h_frame_size;
	  frames = frames > MAX_READ_FRAMES ? MAX_READ_FRAMES : frames;
	  bytes = frames * resampler->o2h_frame_size;
	  context->read (context->o2h_audio, (void *) resampler->o2h_buf_in,
			 bytes);
	}
      else
	{
	  debug_print (2,
		       "o2h: Audio ring buffer underflow (%zu B < %zu B). No fix possible.",
		       rso2h, resampler->engine->o2h_transfer_size);

	  // Any maximum value is invalid at this point
	  pthread_spin_lock (&resampler->engine->lock);
	  resampler->engine->latency_o2h_max =
	    resampler->engine->latency_o2h_min;
	  pthread_spin_unlock (&resampler->engine->lock);

	  frames = MAX_READ_FRAMES;
	}
    }
  else
    {
      if (rso2h >= resampler->o2h_bufsize)
	{
	  bytes = ow_bytes_to_frame_bytes (rso2h, resampler->o2h_bufsize);
	  debug_print (2, "o2h: Emptying buffer (%zu B) and running...",
		       bytes);
	  context->read (context->o2h_audio, NULL, bytes);
	  resampler->reading_at_o2h_end = 1;
	}
      frames = MAX_READ_FRAMES;
    }

  resampler->dll.frames += frames;

  return frames;
}

int
ow_resampler_read_audio (struct ow_resampler *resampler)
{
  long gen_frames;

  gen_frames = src_callback_read (resampler->o2h_state, resampler->o2h_ratio,
				  resampler->bufsize, resampler->o2h_buf_out);
  if (gen_frames != resampler->bufsize)
    {
      error_print
	("o2h: Unexpected frames with ratio %f (output %ld, expected %d)",
	 resampler->o2h_ratio, gen_frames, resampler->bufsize);
      return -1;
    }

  return 0;
}

int
ow_resampler_write_audio (struct ow_resampler *resampler)
{
  long gen_frames;
  int inc;
  int frames;
  size_t bytes;
  size_t wsh2o;
  static double h2o_acc = .0;
  ow_resampler_status_t status = ow_resampler_get_status (resampler);
  struct ow_context *context = resampler->engine->context;

  if (status < OW_RESAMPLER_STATUS_RUN)
    {
      return 0;
    }

  memcpy (&resampler->h2o_queue
	  [resampler->h2o_queue_len *
	   resampler->h2o_frame_size], resampler->h2o_buf_in,
	  resampler->h2o_bufsize);
  resampler->h2o_queue_len += resampler->bufsize;

  h2o_acc += resampler->bufsize * (resampler->h2o_ratio - 1.0);
  inc = trunc (h2o_acc);
  h2o_acc -= inc;
  frames = resampler->bufsize + inc;

  gen_frames = src_callback_read (resampler->h2o_state, resampler->h2o_ratio,
				  frames, resampler->h2o_buf_out);
  if (gen_frames != frames)
    {
      error_print
	("h2o: Unexpected frames with ratio %f (output %ld, expected %d)",
	 resampler->h2o_ratio, gen_frames, frames);

      return -1;
    }

  bytes = gen_frames * resampler->h2o_frame_size;
  wsh2o = context->write_space (context->h2o_audio);

  if (bytes <= wsh2o)
    {
      context->write (context->h2o_audio, (void *) resampler->h2o_buf_out,
		      bytes);
    }
  else
    {
      error_print ("h2o: Audio ring buffer overflow. Discarding data...");
    }

  return 0;
}

int
ow_resampler_compute_ratios (struct ow_resampler *resampler,
			     uint64_t current_usecs,
			     void (*audio_running_cb) (void *), void *cb_data)
{
  ow_engine_status_t engine_status;
  struct ow_dll *dll = &resampler->dll;
  static uint64_t booting_start_usecs, tuning_start_usecs;
  ow_resampler_status_t status;

  engine_status = ow_engine_get_status (resampler->engine);
  status = ow_resampler_get_status (resampler);

  if (status == OW_RESAMPLER_STATUS_READY &&
      engine_status <= OW_ENGINE_STATUS_BOOT)
    {
      if (engine_status <= OW_ENGINE_STATUS_STOP)
	{
	  return 1;
	}
      else if (engine_status == OW_ENGINE_STATUS_READY)
	{
	  debug_print (1,
		       "%s (%s): Setting Overbridge side to steady (notifying readiness)...",
		       resampler->engine->name,
		       resampler->engine->overbridge_name);

	  ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_STEADY);

	  return 0;
	}
      else if (engine_status == OW_ENGINE_STATUS_STEADY)
	{
	  return 0;
	}
      else
	{
	  // OW_ENGINE_STATUS_BOOT
	  return 0;
	}
    }

  pthread_spin_lock (&resampler->engine->lock);
  ow_dll_host_load_dll_overbridge (dll);
  pthread_spin_unlock (&resampler->engine->lock);

  ow_dll_host_update_error (dll, current_usecs);

  if (status == OW_RESAMPLER_STATUS_READY &&
      engine_status == OW_ENGINE_STATUS_WAIT)
    {
      debug_print (1, "%s (%s): Booting resampler...",
		   resampler->engine->name,
		   resampler->engine->overbridge_name);

      ow_dll_host_set_loop_filter (dll, 1.0, resampler->bufsize,
				   resampler->samplerate);

      ow_resampler_set_status (resampler, OW_RESAMPLER_STATUS_BOOT);
      ow_resampler_report_state (resampler);

      booting_start_usecs = current_usecs;

      return 0;
    }

  ow_dll_host_update (dll);

  ow_resampler_set_ratios_from_dll (resampler);

  if (status == OW_RESAMPLER_STATUS_BOOT &&
      current_usecs - booting_start_usecs > BOOTING_PERIOD_US &&
      ow_dll_tuned (dll, BOOTING_ERROR))
    {
      debug_print (1, "%s (%s): Tuning resampler...", resampler->engine->name,
		   resampler->engine->overbridge_name);

      ow_dll_host_set_loop_filter (dll, 0.5, resampler->bufsize,
				   resampler->samplerate);

      ow_resampler_set_status (resampler, OW_RESAMPLER_STATUS_TUNE);

      resampler->log_control_cycles =
	resampler->report_period * resampler->samplerate / resampler->bufsize;
      resampler->log_cycles = 0;

      tuning_start_usecs = current_usecs;
    }

  if (status == OW_RESAMPLER_STATUS_TUNE &&
      current_usecs - tuning_start_usecs > TUNING_PERIOD_US &&
      ow_dll_tuned (dll, TUNING_ERROR))
    {
      debug_print (1, "%s (%s): Running resampler...",
		   resampler->engine->name,
		   resampler->engine->overbridge_name);

      ow_dll_host_set_loop_filter (dll, 0.05, resampler->bufsize,
				   resampler->samplerate);

      ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_RUN);

      ow_resampler_set_status (resampler, OW_RESAMPLER_STATUS_RUN);

      audio_running_cb (cb_data);
    }


  resampler->log_cycles++;
  if (resampler->log_cycles == resampler->log_control_cycles)
    {
      ow_resampler_report_state (resampler);
      resampler->log_cycles = 0;
    }

  return 0;
}

ow_err_t
ow_resampler_init_from_device (struct ow_resampler **resampler_,
			       struct ow_device *device,
			       unsigned int blocks_per_transfer,
			       unsigned int xfr_timeout, unsigned int quality)
{
  struct ow_resampler *resampler = malloc (sizeof (struct ow_resampler));
  ow_err_t err = ow_engine_init_from_device (&resampler->engine,
					     device,
					     blocks_per_transfer,
					     xfr_timeout);
  if (err)
    {
      free (resampler);
      return err;
    }

  *resampler_ = resampler;

  pthread_spin_init (&resampler->lock, PTHREAD_PROCESS_SHARED);

  resampler->samplerate = 0;
  resampler->bufsize = 0;
  resampler->o2h_frame_size = device->desc.outputs * OW_BYTES_PER_SAMPLE;
  resampler->h2o_frame_size = device->desc.inputs * OW_BYTES_PER_SAMPLE;
  resampler->h2o_aux = NULL;
  resampler->status = OW_RESAMPLER_STATUS_STOP;

  resampler->h2o_state = src_callback_new (resampler_h2o_reader, quality,
					   device->desc.inputs, NULL,
					   resampler);
  resampler->o2h_state = src_callback_new (resampler_o2h_reader, quality,
					   device->desc.outputs, NULL,
					   resampler);

  resampler->report_period = DEFAULT_REPORT_PERIOD;
  resampler->log_control_cycles = 0;

  pthread_spin_lock (&resampler->engine->lock);
  ow_dll_host_init (&resampler->dll);
  pthread_spin_unlock (&resampler->engine->lock);

  return OW_OK;
}

void
ow_resampler_destroy (struct ow_resampler *resampler)
{
  src_delete (resampler->h2o_state);
  src_delete (resampler->o2h_state);
  pthread_spin_destroy (&resampler->lock);
  if (resampler->h2o_aux)
    {
      free (resampler->h2o_aux);
      free (resampler->h2o_buf_out);
      free (resampler->h2o_buf_in);
      free (resampler->h2o_queue);
      free (resampler->o2h_buf_in);
      free (resampler->o2h_buf_out);
    }
  ow_engine_destroy (resampler->engine);
  free (resampler);
}

static inline void
ow_resampler_init_buffer_size (struct ow_resampler *resampler,
			       uint32_t bufsize)
{
  debug_print (1, "Setting resampler buffer size to %d", bufsize);
  resampler->bufsize = bufsize;
  ow_resampler_reset_buffers (resampler);
}

static inline void
ow_resampler_init_samplerate (struct ow_resampler *resampler,
			      uint32_t samplerate)
{
  debug_print (1, "Setting resampler sample rate to %d", samplerate);
  resampler->samplerate = samplerate;
}

ow_err_t
ow_resampler_start (struct ow_resampler *resampler,
		    struct ow_context *context, uint32_t samplerate,
		    uint32_t bufsize)
{
  ow_resampler_init_samplerate (resampler, samplerate);
  ow_resampler_init_buffer_size (resampler, bufsize);
  ow_resampler_reset_dll (resampler);

  context->dll = &resampler->dll;
  context->dll_overbridge_init = ow_dll_overbridge_init;
  context->dll_overbridge_update = ow_dll_overbridge_update;

  ow_resampler_set_status (resampler, OW_RESAMPLER_STATUS_READY);

  return ow_engine_start (resampler->engine, context);
}

void
ow_resampler_wait (struct ow_resampler *resampler)
{
  ow_engine_wait (resampler->engine);
  ow_resampler_report_state (resampler);
}

inline void
ow_resampler_reset_latencies (struct ow_resampler *resampler)
{
  pthread_spin_lock (&resampler->engine->lock);
  resampler->engine->latency_o2h_max = resampler->engine->latency_o2h_min;
  resampler->engine->latency_h2o_max = resampler->engine->latency_h2o_min;
  pthread_spin_unlock (&resampler->engine->lock);
}

inline struct ow_engine *
ow_resampler_get_engine (struct ow_resampler *resampler)
{
  return resampler->engine;
}

inline void
ow_resampler_stop (struct ow_resampler *resampler)
{
  debug_print (1, "Stopping resampler...");
  ow_engine_stop (resampler->engine);
}

inline void
ow_resampler_set_buffer_size (struct ow_resampler *resampler,
			      uint32_t bufsize)
{
  if (resampler->bufsize && resampler->bufsize != bufsize)
    {
      ow_resampler_init_buffer_size (resampler, bufsize);
      ow_resampler_reset (resampler);
    }
}

uint32_t
ow_resampler_get_buffer_size (struct ow_resampler *resampler)
{
  return resampler->bufsize;
}

inline void
ow_resampler_set_samplerate (struct ow_resampler *resampler,
			     uint32_t samplerate)
{
  if (resampler->samplerate && resampler->samplerate != samplerate)
    {
      ow_resampler_init_samplerate (resampler, samplerate);
      ow_resampler_reset (resampler);
    }
}

uint32_t
ow_resampler_get_samplerate (struct ow_resampler *resampler)
{
  return resampler->samplerate;
}

inline size_t
ow_resampler_get_o2h_frame_size (struct ow_resampler *resampler)
{
  return resampler->o2h_frame_size;
}

inline size_t
ow_resampler_get_h2o_frame_size (struct ow_resampler *resampler)
{
  return resampler->h2o_frame_size;
}

inline float *
ow_resampler_get_o2h_audio_buffer (struct ow_resampler *resampler)
{
  return resampler->o2h_buf_out;
}

inline float *
ow_resampler_get_h2o_audio_buffer (struct ow_resampler *resampler)
{
  return resampler->h2o_buf_in;
}

struct ow_resampler_state *
ow_resampler_get_state (struct ow_resampler *resampler)
{
  return &resampler->state;
}

void
ow_resampler_get_state_copy (struct ow_resampler *resampler,
			     struct ow_resampler_state *state)
{
  pthread_spin_lock (&resampler->lock);
  memcpy (state, &resampler->state, sizeof (struct ow_resampler_state));
  pthread_spin_unlock (&resampler->lock);
}
