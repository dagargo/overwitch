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
#define TUNING_PERIOD_US 5000000

#define OB_PERIOD_MS (1000.0 / OB_SAMPLE_RATE)

#define RATIO_ERROR_TOLERANCE 4

// If `JSON_DEVS_FILE=no` is passed passed to `./configure`, the compilation is independent of GLib.
// But since the MAX macro is defined there, not only do we need to add it,
// but it is also needed to have a different name to avoid redefining it.

#define RESAMPLER_MAX(a,b) (((a) > (b)) ? (a) : (b))

inline void
ow_resampler_report_status (struct ow_resampler *resampler)
{
  size_t o2h_latency_s, o2h_min_latency_s, o2h_max_latency_s, h2o_latency_s,
    h2o_min_latency_s, h2o_max_latency_s;
  struct ow_resampler_latency latency;
  ow_engine_status_t status;

  ow_resampler_get_o2h_latency (resampler, &o2h_latency_s, &o2h_min_latency_s,
				&o2h_max_latency_s);
  ow_resampler_get_h2o_latency (resampler, &h2o_latency_s, &h2o_min_latency_s,
				&h2o_max_latency_s);

  status = resampler->engine->status;

  int h2o_enabled = ow_engine_is_option (resampler->engine,
					 OW_ENGINE_OPTION_P2O_AUDIO);

  if (status == OW_ENGINE_STATUS_RUN)
    {
      latency.o2h = o2h_latency_s * OB_PERIOD_MS /
	resampler->engine->o2h_frame_size;
      latency.o2h_max = o2h_max_latency_s * OB_PERIOD_MS /
	resampler->engine->o2h_frame_size;
      latency.o2h_min = o2h_min_latency_s * OB_PERIOD_MS /
	resampler->engine->o2h_frame_size;

      if (h2o_enabled)
	{
	  latency.h2o = h2o_latency_s * OB_PERIOD_MS /
	    resampler->engine->h2o_frame_size;
	  latency.h2o_max = h2o_max_latency_s * OB_PERIOD_MS /
	    resampler->engine->h2o_frame_size;
	  latency.h2o_min = h2o_min_latency_s * OB_PERIOD_MS /
	    resampler->engine->h2o_frame_size;
	}
      else
	{
	  latency.h2o = -1.0;
	  latency.h2o_max = -1.0;
	  latency.h2o_min = -1.0;
	}
    }
  else
    {
      latency.o2h = -1.0;
      latency.o2h_max = -1.0;
      latency.o2h_min = -1.0;

      latency.h2o = -1.0;
      latency.h2o_max = -1.0;
      latency.h2o_min = -1.0;
    }

  if (debug_level)
    {
      printf
	("%s: o2h latency: %4.1f [%4.1f, %4.1f] ms; h2o latency: %4.1f [%4.1f, %4.1f] ms, o2h ratio: %f\n",
	 resampler->engine->name, latency.o2h, latency.o2h_min,
	 latency.o2h_max, latency.h2o, latency.h2o_min, latency.h2o_max,
	 resampler->dll.ratio);
    }

  if (resampler->reporter.callback)
    {
      resampler->reporter.callback (resampler->reporter.data, &latency,
				    resampler->o2h_ratio,
				    resampler->h2o_ratio);
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
      bytes = ow_bytes_to_frame_bytes (rso2h,
				       resampler->engine->o2h_frame_size);
      context->read (context->o2h_audio, NULL, bytes);
    }

  ow_engine_clear_buffers (resampler->engine);
}

static void
ow_resampler_reset_buffers (struct ow_resampler *resampler)
{
  debug_print (2, "Resetting buffers...");

  resampler->o2h_bufsize =
    resampler->bufsize * resampler->engine->o2h_frame_size;
  resampler->h2o_bufsize =
    resampler->bufsize * resampler->engine->h2o_frame_size;

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

static void
ow_resampler_reset_dll (struct ow_resampler *resampler,
			uint32_t new_samplerate)
{
  double target_ratio;

  if (resampler->dll.set
      && ow_engine_get_status (resampler->engine) == OW_ENGINE_STATUS_RUN)
    {
      debug_print (2, "Just adjusting DLL ratio...");
      resampler->log_cycles = 0;
      resampler->log_control_cycles =
	STARTUP_TIME * new_samplerate / resampler->bufsize;
    }
  else
    {
      ow_dll_host_reset (&resampler->dll, new_samplerate, OB_SAMPLE_RATE,
			 resampler->bufsize,
			 resampler->engine->frames_per_transfer);
    }

  ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_BOOT);

  resampler->status = OW_RESAMPLER_STATUS_READY;

  resampler->o2h_ratio = resampler->dll.ratio;
  resampler->samplerate = new_samplerate;

  target_ratio = new_samplerate / OB_SAMPLE_RATE;
  resampler->max_target_ratio = target_ratio * RATIO_ERROR_TOLERANCE;
  resampler->min_target_ratio = target_ratio / RATIO_ERROR_TOLERANCE;
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
	  ret * resampler->engine->h2o_frame_size);
  resampler->h2o_queue_len = 0;

  return ret;
}

static long
resampler_o2h_reader (void *cb_data, float **data)
{
  size_t rso2h;
  size_t bytes;
  long frames;
  static int last_frames = 1;
  struct ow_resampler *resampler = cb_data;

  *data = resampler->o2h_buf_in;

  rso2h =
    resampler->engine->context->read_space (resampler->engine->context->
					    o2h_audio);
  if (resampler->reading_at_o2h_end)
    {
      if (rso2h >= resampler->engine->o2h_frame_size)
	{
	  frames = rso2h / resampler->engine->o2h_frame_size;
	  frames = frames > MAX_READ_FRAMES ? MAX_READ_FRAMES : frames;
	  bytes = frames * resampler->engine->o2h_frame_size;
	  resampler->engine->context->read (resampler->engine->
					    context->o2h_audio,
					    (void *) resampler->o2h_buf_in,
					    bytes);
	}
      else
	{
	  debug_print (2,
		       "o2h: Audio ring buffer underflow (%zu < %zu). Replicating last samples...",
		       rso2h, resampler->engine->o2h_transfer_size);

	  pthread_spin_lock (&resampler->engine->lock);
	  resampler->engine->o2h_max_latency = 0;	// Any maximum values is invalid at this point
	  pthread_spin_unlock (&resampler->engine->lock);

	  if (last_frames > 1)
	    {
	      uint64_t pos =
		(last_frames - 1) * resampler->engine->device_desc.outputs;
	      memcpy (resampler->o2h_buf_in, &resampler->o2h_buf_in[pos],
		      resampler->engine->o2h_frame_size);
	    }
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
	  resampler->engine->context->read (resampler->engine->
					    context->o2h_audio, NULL, bytes);
	  resampler->reading_at_o2h_end = 1;
	}
      frames = MAX_READ_FRAMES;
    }

  resampler->dll.frames += frames;
  last_frames = frames;
  return frames;
}

void
ow_resampler_read_audio (struct ow_resampler *resampler)
{
  long gen_frames;
  int xruns;

  pthread_spin_lock (&resampler->lock);
  xruns = resampler->o2h_xruns;
  if (xruns)
    {
      resampler->o2h_xruns--;
    }
  pthread_spin_unlock (&resampler->lock);

  if (xruns)
    {
      error_print ("Forcing o2h read (xrun)...");

      pthread_spin_lock (&resampler->engine->lock);
      resampler->engine->o2h_max_latency = 0;
      pthread_spin_unlock (&resampler->engine->lock);

      resampler->engine->context->read (resampler->engine->context->o2h_audio,
					NULL, resampler->bufsize);
    }

  gen_frames = src_callback_read (resampler->o2h_state, resampler->o2h_ratio,
				  resampler->bufsize, resampler->o2h_buf_out);
  if (gen_frames != resampler->bufsize)
    {
      error_print
	("o2h: Unexpected frames with ratio %f (output %ld, expected %d)",
	 resampler->o2h_ratio, gen_frames, resampler->bufsize);
    }
}

void
ow_resampler_write_audio (struct ow_resampler *resampler)
{
  long gen_frames;
  int inc;
  int frames;
  int xruns;
  size_t bytes;
  size_t wsh2o;
  static double h2o_acc = .0;

  if (resampler->status < OW_RESAMPLER_STATUS_RUN)
    {
      return;
    }

  pthread_spin_lock (&resampler->lock);
  xruns = resampler->h2o_xruns;
  if (xruns)
    {
      resampler->h2o_xruns--;
    }
  pthread_spin_unlock (&resampler->lock);

  memcpy (&resampler->h2o_queue
	  [resampler->h2o_queue_len *
	   resampler->engine->h2o_frame_size], resampler->h2o_buf_in,
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
    }

  bytes = gen_frames * resampler->engine->h2o_frame_size;
  wsh2o =
    resampler->engine->context->write_space (resampler->engine->context->
					     h2o_audio);

  if (bytes <= wsh2o)
    {
      if (xruns)
	{
	  error_print ("Skipping h2o write (xrun)...");

	  pthread_spin_lock (&resampler->engine->lock);
	  resampler->engine->h2o_max_latency = 0;
	  pthread_spin_unlock (&resampler->engine->lock);
	}
      else
	{
	  resampler->engine->context->write (resampler->engine->
					     context->h2o_audio,
					     (void *) resampler->h2o_buf_out,
					     bytes);
	}
    }
  else
    {
      error_print ("h2o: Audio ring buffer overflow. Discarding data...");
    }
}

int
ow_resampler_compute_ratios (struct ow_resampler *resampler,
			     uint64_t current_usecs,
			     void (*audio_running_cb) (void *), void *cb_data)
{
  int xruns;
  ow_engine_status_t engine_status;
  struct ow_dll *dll = &resampler->dll;
  static uint64_t tuning_start_usecs;

  pthread_spin_lock (&resampler->lock);
  xruns = resampler->xruns;
  if (xruns)
    {
      resampler->xruns--;
    }
  pthread_spin_unlock (&resampler->lock);

  engine_status = ow_engine_get_status (resampler->engine);
  if (resampler->status == OW_RESAMPLER_STATUS_READY
      && engine_status <= OW_ENGINE_STATUS_BOOT)
    {
      if (engine_status == OW_ENGINE_STATUS_READY)
	{
	  debug_print (2, "Booting Overbridge side...");

	  ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_STEADY);
	}
      return 1;
    }

  pthread_spin_lock (&resampler->engine->lock);
  ow_dll_host_load_dll_overbridge (dll);
  pthread_spin_unlock (&resampler->engine->lock);

  ow_dll_host_update_error (dll, current_usecs);

  if (resampler->status == OW_RESAMPLER_STATUS_READY
      && engine_status == OW_ENGINE_STATUS_WAIT)
    {
      debug_print (2, "Starting up resampler...");

      ow_dll_host_set_loop_filter (dll, 1.0, resampler->bufsize,
				   resampler->samplerate);

      resampler->status = OW_RESAMPLER_STATUS_BOOT;
      ow_resampler_report_status (resampler);

      resampler->log_cycles = 0;
      resampler->log_control_cycles =
	STARTUP_TIME * resampler->samplerate / resampler->bufsize;

      return 0;
    }

  if (xruns)
    {
      error_print ("Skipping DLL update (xrun)...");

      //We skip the current cycle DLL update as time masurements are not precise enough and would lead to errors.
      return 0;
    }

  ow_dll_host_update (dll);

  if (dll->ratio < resampler->min_target_ratio ||
      dll->ratio > resampler->max_target_ratio)
    {
      error_print ("Invalid ratio %f detected. Stopping resampler...",
		   dll->ratio);

      ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_ERROR);

      resampler->status = OW_RESAMPLER_STATUS_ERROR;
      ow_resampler_report_status (resampler);

      return 1;
    }

  resampler->o2h_ratio = dll->ratio;
  resampler->h2o_ratio = 1.0 / resampler->o2h_ratio;

  if (resampler->status == OW_RESAMPLER_STATUS_BOOT && ow_dll_tuned (dll))
    {
      debug_print (2, "Tuning resampler...");

      ow_dll_host_set_loop_filter (dll, 0.5, resampler->bufsize,
				   resampler->samplerate);

      resampler->status = OW_RESAMPLER_STATUS_TUNE;

      resampler->log_control_cycles =
	resampler->reporter.period * resampler->samplerate /
	resampler->bufsize;

      tuning_start_usecs = current_usecs;
    }

  if (resampler->status == OW_RESAMPLER_STATUS_TUNE &&
      current_usecs - tuning_start_usecs > TUNING_PERIOD_US)
    {
      debug_print (2, "Running resampler...");

      ow_dll_host_set_loop_filter (dll, 0.05, resampler->bufsize,
				   resampler->samplerate);

      ow_engine_set_status (resampler->engine, OW_ENGINE_STATUS_RUN);

      resampler->status = OW_RESAMPLER_STATUS_RUN;

      audio_running_cb (cb_data);
    }

  resampler->log_cycles++;
  if (resampler->log_cycles == resampler->log_control_cycles)
    {
      ow_resampler_report_status (resampler);
      resampler->log_cycles = 0;
    }

  return 0;
}

ow_err_t
ow_resampler_init_from_bus_address (struct ow_resampler **resampler_,
				    uint8_t bus, uint8_t address,
				    unsigned int blocks_per_transfer,
				    unsigned int xfr_timeout, int quality)
{
  struct ow_resampler *resampler = malloc (sizeof (struct ow_resampler));
  ow_err_t err = ow_engine_init_from_bus_address (&resampler->engine, bus,
						  address,
						  blocks_per_transfer,
						  xfr_timeout);
  if (err)
    {
      free (resampler);
      return err;
    }

  *resampler_ = resampler;

  resampler->samplerate = 0;
  resampler->bufsize = 0;
  resampler->xruns = 0;
  resampler->o2h_xruns = 0;
  resampler->h2o_xruns = 0;
  resampler->h2o_aux = NULL;
  resampler->status = OW_RESAMPLER_STATUS_STOP;

  resampler->h2o_state =
    src_callback_new (resampler_h2o_reader, quality,
		      resampler->engine->device_desc.inputs, NULL, resampler);
  resampler->o2h_state =
    src_callback_new (resampler_o2h_reader, quality,
		      resampler->engine->device_desc.outputs, NULL,
		      resampler);

  pthread_spin_init (&resampler->lock, PTHREAD_PROCESS_SHARED);

  resampler->reporter.callback = NULL;
  resampler->reporter.data = NULL;
  resampler->reporter.period = DEFAULT_REPORT_PERIOD;

  ow_dll_host_init (&resampler->dll);

  return OW_OK;
}

void
ow_resampler_destroy (struct ow_resampler *resampler)
{
  src_delete (resampler->h2o_state);
  src_delete (resampler->o2h_state);
  if (resampler->h2o_aux)
    {
      free (resampler->h2o_aux);
      free (resampler->h2o_buf_out);
      free (resampler->h2o_buf_in);
      free (resampler->h2o_queue);
      free (resampler->o2h_buf_in);
      free (resampler->o2h_buf_out);
    }
  pthread_spin_destroy (&resampler->lock);
  ow_engine_destroy (resampler->engine);
  free (resampler);
}

ow_err_t
ow_resampler_start (struct ow_resampler *resampler,
		    struct ow_context *context)
{
  context->dll = &resampler->dll;
  context->dll_overbridge_init = ow_dll_overbridge_init;
  context->dll_overbridge_update = ow_dll_overbridge_update;

  resampler->status = OW_RESAMPLER_STATUS_READY;

  return ow_engine_start (resampler->engine, context);
}

void
ow_resampler_wait (struct ow_resampler *resampler)
{
  ow_engine_wait (resampler->engine);
  if (resampler->engine->status == OW_ENGINE_STATUS_ERROR)
    {
      resampler->status = OW_RESAMPLER_STATUS_ERROR;
    }
  else
    {
      resampler->status = OW_RESAMPLER_STATUS_STOP;
    }
  ow_resampler_report_status (resampler);
}

void
ow_resampler_inc_xruns (struct ow_resampler *resampler)
{
  pthread_spin_lock (&resampler->lock);
  resampler->xruns++;
  resampler->o2h_xruns++;
  resampler->h2o_xruns++;
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
      debug_print (1, "Setting resampler buffer size to %d", bufsize);
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
      debug_print (1, "Setting resampler sample rate to %d", samplerate);
      if (resampler->h2o_aux)	//This means that ow_resampler_reset_buffers has been called and thus bufsize has been set.
	{
	  ow_resampler_reset_dll (resampler, samplerate);
	}
      else
	{
	  resampler->samplerate = samplerate;
	}
    }
}

inline void
ow_resampler_reset (struct ow_resampler *resampler)
{
  ow_resampler_clear_buffers (resampler);
  ow_resampler_reset_dll (resampler, resampler->samplerate);
}

inline size_t
ow_resampler_get_o2h_frame_size (struct ow_resampler *resampler)
{
  return resampler->engine->o2h_frame_size;
}

inline size_t
ow_resampler_get_h2o_frame_size (struct ow_resampler *resampler)
{
  return resampler->engine->h2o_frame_size;
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

struct ow_resampler_reporter *
ow_resampler_get_reporter (struct ow_resampler *resampler)
{
  return &resampler->reporter;
}

inline void
ow_resampler_get_h2o_latency (struct ow_resampler *resampler,
			      size_t *h2o_latency, size_t *h2o_min_latency,
			      size_t *h2o_max_latency)
{
  pthread_spin_lock (&resampler->engine->lock);
  *h2o_latency = resampler->engine->h2o_latency;
  *h2o_min_latency = RESAMPLER_MAX (resampler->engine->h2o_min_latency,
				    resampler->h2o_bufsize);
  *h2o_max_latency = resampler->engine->h2o_max_latency;
  pthread_spin_unlock (&resampler->engine->lock);
}

inline void
ow_resampler_get_o2h_latency (struct ow_resampler *resampler,
			      size_t *o2h_latency, size_t *o2h_min_latency,
			      size_t *o2h_max_latency)
{
  pthread_spin_lock (&resampler->engine->lock);
  *o2h_latency = resampler->engine->o2h_latency;
  *o2h_min_latency = RESAMPLER_MAX (resampler->engine->o2h_min_latency,
				    resampler->o2h_bufsize);
  *o2h_max_latency = resampler->engine->o2h_max_latency;
  pthread_spin_unlock (&resampler->engine->lock);
}
