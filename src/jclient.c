/*
 *   jclient.c
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

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <jack/jack.h>
#include <jack/intclient.h>
#include <jack/thread.h>
#include <jack/midiport.h>

#include "utils.h"
#include "jclient.h"

#define MSG_ERROR_PORT_REGISTER "Error while registering JACK port"

#define MAX_LATENCY (8192 * 2)	//This is twice the maximum JACK latency.

#define JCLIENT_WAIT_TIME_US 500000

size_t
jclient_buffer_read (void *buffer, char *src, size_t size)
{
  if (src)
    {
      return jack_ringbuffer_read (buffer, src, size);
    }
  else
    {
      jack_ringbuffer_read_advance (buffer, size);
      return 0;
    }
}

static int
jclient_thread_xrun_cb (void *cb_data)
{
  struct ow_resampler *resampler = cb_data;
  error_print ("JACK xrun");
  ow_resampler_reset_latencies (resampler);
  return 0;
}

static void
jclient_set_latency_cb (jack_latency_callback_mode_t mode, void *cb_data)
{
  jack_latency_range_t range;
  struct jclient *jclient = cb_data;
  uint32_t latency, min_latency, max_latency;
  struct ow_engine *engine = ow_resampler_get_engine (jclient->resampler);
  const struct ow_device_desc *desc = &ow_engine_get_device (engine)->desc;

  debug_print (2, "JACK latency request");

  if (mode == JackPlaybackLatency)
    {
      ow_resampler_get_o2h_latency (jclient->resampler, &latency,
				    &min_latency, &max_latency);
      debug_print (2, "o2h latency: [ %d, %d ]", min_latency, max_latency);

      for (int i = 0; i < desc->outputs; i++)
	{
	  jack_port_get_latency_range (jclient->input_ports[0], mode, &range);
	  range.min += min_latency;
	  range.max += max_latency;
	  jack_port_set_latency_range (jclient->output_ports[i], mode,
				       &range);
	}
    }
  else if (mode == JackCaptureLatency)
    {
      ow_resampler_get_h2o_latency (jclient->resampler, &latency,
				    &min_latency, &max_latency);
      debug_print (2, "h2o latency: [ %d, %d ]", min_latency, max_latency);

      for (int i = 0; i < desc->inputs; i++)
	{
	  jack_port_get_latency_range (jclient->output_ports[0], mode,
				       &range);
	  range.min += min_latency;
	  range.max += max_latency;
	  jack_port_set_latency_range (jclient->input_ports[i], mode, &range);
	}
    }
}

static void
jclient_port_connect_cb (jack_port_id_t a, jack_port_id_t b, int connect,
			 void *cb_data)
{
  int total_connections = 0;
  struct jclient *jclient = cb_data;
  struct ow_engine *engine = ow_resampler_get_engine (jclient->resampler);
  const struct ow_device_desc *desc = &ow_engine_get_device (engine)->desc;

  debug_print (2, "JACK port connect request");

  for (int i = 0; i < desc->inputs; i++)
    {
      total_connections += jack_port_connected (jclient->input_ports[i]);
    }

  ow_engine_set_option (engine, OW_ENGINE_OPTION_H2O_AUDIO,
			total_connections != 0);

  for (int i = 0; i < desc->outputs; i++)
    {
      total_connections += jack_port_connected (jclient->output_ports[i]);
    }
}

static void
jclient_jack_shutdown_cb (jack_status_t code, const char *reason,
			  void *cb_data)
{
  struct jclient *jclient = cb_data;
  debug_print (1, "JACK is shutting down: %s", reason);
  jclient_stop (jclient);
}

static void
jclient_jack_freewheel (int starting, void *cb_data)
{
  debug_print (1, "JACK in freewheel mode: %d", starting);
}

static int
jclient_jack_graph_order_cb (void *cb_data)
{
  struct ow_resampler *resampler = cb_data;
  debug_print (1, "JACK calling graph order...");
  ow_resampler_reset_latencies (resampler);
  return 0;
}

static void
jclient_jack_client_registration_cb (const char *name, int op, void *cb_data)
{
  debug_print (1, "JACK client %s is being %s...", name,
	       op ? "registered" : "unregistered");
}

static int
jclient_set_buffer_size_cb (jack_nframes_t nframes, void *cb_data)
{
  struct jclient *jclient = cb_data;
  debug_print (1, "JACK buffer size: %d", nframes);
  ow_resampler_set_buffer_size (jclient->resampler, nframes);
  return 0;
}

static int
jclient_set_sample_rate_cb (jack_nframes_t nframes, void *cb_data)
{
  struct jclient *jclient = cb_data;
  debug_print (1, "JACK sample rate: %d", nframes);
  ow_resampler_set_samplerate (jclient->resampler, nframes);
  return 0;
}

inline void
jclient_copy_o2j_audio (float *f, jack_nframes_t nframes,
			jack_default_audio_sample_t *buffer[],
			const struct ow_device_desc *desc)
{
  for (int i = 0; i < nframes; i++)
    {
      for (int j = 0; j < desc->outputs; j++)
	{
	  buffer[j][i] = *f;
	  f++;
	}
    }
}

inline void
jclient_copy_j2o_audio (float *f, jack_nframes_t nframes,
			jack_default_audio_sample_t *buffer[],
			const struct ow_device_desc *desc)
{
  for (int i = 0; i < nframes; i++)
    {
      for (int j = 0; j < desc->inputs; j++)
	{
	  *f = buffer[j][i];
	  f++;
	}
    }
}

static void
jclient_audio_running (void *data)
{
  jack_recompute_total_latencies (data);
}

static inline int
jclient_process_cb (jack_nframes_t nframes, void *arg)
{
  float *f;
  jack_default_audio_sample_t *buffer[OB_MAX_TRACKS];
  struct jclient *jclient = arg;
  jack_nframes_t current_frames;
  jack_time_t current_usecs;
  jack_time_t next_usecs;
  float period_usecs;
  struct ow_engine *engine = ow_resampler_get_engine (jclient->resampler);
  const struct ow_device_desc *desc = &ow_engine_get_device (engine)->desc;

  if (jack_get_cycle_times (jclient->client, &current_frames, &current_usecs,
			    &next_usecs, &period_usecs))
    {
      error_print ("Error while getting JACK time");
    }

  if (ow_resampler_compute_ratios (jclient->resampler, current_usecs,
				   jclient_audio_running, jclient->client))
    {
      return 0;
    }

  //o2h

  for (int i = 0; i < desc->outputs; i++)
    {
      buffer[i] = jack_port_get_buffer (jclient->output_ports[i], nframes);
    }

  f = ow_resampler_get_o2h_audio_buffer (jclient->resampler);
  ow_resampler_read_audio (jclient->resampler);
  jclient_copy_o2j_audio (f, nframes, buffer, desc);

  //h2o

  if (ow_engine_is_option (engine, OW_ENGINE_OPTION_H2O_AUDIO))
    {
      for (int i = 0; i < desc->inputs; i++)
	{
	  buffer[i] = jack_port_get_buffer (jclient->input_ports[i], nframes);
	}

      f = ow_resampler_get_h2o_audio_buffer (jclient->resampler);
      jclient_copy_j2o_audio (f, nframes, buffer, desc);
      ow_resampler_write_audio (jclient->resampler);
    }

  return 0;
}

static void
set_rt_priority (pthread_t thread, int priority)
{
  int err = jack_acquire_real_time_scheduling (thread, priority);
  if (err)
    {
      error_print ("Could not set real time priority");
    }
}

void
jclient_stop (struct jclient *jclient)
{
  debug_print (1, "Stopping client...");
  ow_resampler_stop (jclient->resampler);
}

int
jclient_init (struct jclient *jclient, struct ow_device *device,
	      unsigned int blocks_per_transfer, unsigned int xfr_timeout,
	      int quality, int priority)
{
  ow_err_t err;
  struct ow_resampler *resampler;

  jclient->device = device;
  jclient->priority = priority;
  jclient->running = 0;

  pthread_spin_init (&jclient->lock, PTHREAD_PROCESS_PRIVATE);

  err = ow_resampler_init_from_device (&resampler, device,
				       blocks_per_transfer, xfr_timeout,
				       quality);

  if (err)
    {
      error_print ("Overwitch error: %s", ow_get_err_str (err));
      return -1;
    }

  jclient->resampler = resampler;

  return 0;
}

void
jclient_destroy (struct jclient *jclient)
{
  ow_resampler_destroy (jclient->resampler);
  pthread_spin_destroy (&jclient->lock);
}

static ow_err_t
jclient_run (struct jclient *jclient)
{
  jack_status_t status;
  ow_err_t err = OW_OK;
  char *client_name;
  const char *name;
  struct ow_engine *engine;
  const struct ow_device_desc *desc;

  jclient->output_ports = NULL;
  jclient->input_ports = NULL;
  jclient->context.h2o_audio = NULL;
  jclient->context.o2h_audio = NULL;

  pthread_spin_lock (&jclient->lock);
  jclient->running = 1;
  pthread_spin_unlock (&jclient->lock);

  engine = ow_resampler_get_engine (jclient->resampler);
  desc = &ow_engine_get_device (engine)->desc;
  name = ow_engine_get_overbridge_name (engine);

  jclient->client = jack_client_open (name, JackNoStartServer, &status, NULL);
  if (jclient->client == NULL)
    {
      if (status & JackServerFailed)
	{
	  error_print ("Unable to connect to JACK server");
	}
      else
	{
	  error_print ("Unable to open client. Error 0x%2.0x", status);
	}
      return OW_GENERIC_ERROR;
    }

  if (status & JackServerStarted)
    {
      debug_print (1, "JACK server started");
    }

  if (status & JackNameNotUnique)
    {
      client_name = jack_get_client_name (jclient->client);
      debug_print (0, "Name client in use. Using %s...", client_name);
    }

  if (jack_set_process_callback (jclient->client, jclient_process_cb,
				 jclient))
    {
      goto cleanup_jack;
    }

  if (jack_set_xrun_callback (jclient->client, jclient_thread_xrun_cb,
			      jclient->resampler))
    {
      goto cleanup_jack;
    }

  if (jack_set_latency_callback (jclient->client, jclient_set_latency_cb,
				 jclient))
    {
      goto cleanup_jack;
    }

  if (jack_set_port_connect_callback (jclient->client,
				      jclient_port_connect_cb, jclient))
    {
      error_print
	("Cannot set port connect callback so j2o audio will not be possible");
    }

  jack_on_info_shutdown (jclient->client, jclient_jack_shutdown_cb, jclient);

  if (jack_set_freewheel_callback (jclient->client, jclient_jack_freewheel,
				   jclient))
    {
      error_print ("Cannot set JACK freewheel callback");
    }

  if (jack_set_graph_order_callback (jclient->client,
				     jclient_jack_graph_order_cb,
				     jclient->resampler))
    {
      error_print ("Cannot set JACK graph order callback");
    }

  if (jack_set_client_registration_callback (jclient->client,
					     jclient_jack_client_registration_cb,
					     jclient))
    {
      error_print ("Cannot set JACK client registration callback");
    }

  if (jack_set_buffer_size_callback (jclient->client,
				     jclient_set_buffer_size_cb, jclient))
    {
      err = OW_GENERIC_ERROR;
      goto cleanup_jack;
    }

  if (jack_set_sample_rate_callback (jclient->client,
				     jclient_set_sample_rate_cb, jclient))
    {
      err = OW_GENERIC_ERROR;
      goto cleanup_jack;
    }

  if (jclient->priority < 0)
    {
      jclient->priority = jack_client_real_time_priority (jclient->client);
    }
  debug_print (1, "Using RT priority %d...", jclient->priority);

  debug_print (1, "Registering ports...");
  jclient->output_ports = malloc (sizeof (jack_port_t *) * desc->outputs);
  for (int i = 0; i < desc->outputs; i++)
    {
      const char *name = desc->output_tracks[i].name;
      debug_print (2, "Registering output port %s...", name);
      jclient->output_ports[i] = jack_port_register (jclient->client,
						     name,
						     JACK_DEFAULT_AUDIO_TYPE,
						     JackPortIsOutput |
						     JackPortIsTerminal, 0);

      if (jclient->output_ports[i] == NULL)
	{
	  error_print (MSG_ERROR_PORT_REGISTER);
	  err = OW_GENERIC_ERROR;
	  goto cleanup_jack;
	}
    }

  jclient->input_ports = malloc (sizeof (jack_port_t *) * desc->inputs);
  for (int i = 0; i < desc->inputs; i++)
    {
      const char *name = desc->input_tracks[i].name;
      debug_print (2, "Registering input port %s...", name);
      jclient->input_ports[i] = jack_port_register (jclient->client,
						    name,
						    JACK_DEFAULT_AUDIO_TYPE,
						    JackPortIsInput |
						    JackPortIsTerminal, 0);

      if (jclient->input_ports[i] == NULL)
	{
	  error_print (MSG_ERROR_PORT_REGISTER);
	  err = OW_GENERIC_ERROR;
	  goto cleanup_jack;
	}
    }

  jclient->context.o2h_audio = jack_ringbuffer_create (MAX_LATENCY *
						       ow_resampler_get_o2h_frame_size
						       (jclient->resampler));
  jack_ringbuffer_mlock (jclient->context.o2h_audio);

  jclient->context.h2o_audio = jack_ringbuffer_create (MAX_LATENCY *
						       ow_resampler_get_h2o_frame_size
						       (jclient->resampler));
  jack_ringbuffer_mlock (jclient->context.h2o_audio);

  jclient->context.read_space =
    (ow_buffer_rw_space_t) jack_ringbuffer_read_space;
  jclient->context.write_space =
    (ow_buffer_rw_space_t) jack_ringbuffer_write_space;
  jclient->context.read = jclient_buffer_read;
  jclient->context.write = (ow_buffer_write_t) jack_ringbuffer_write;
  jclient->context.get_time = jack_get_time;

  jclient->context.set_rt_priority = set_rt_priority;
  jclient->context.priority = jclient->priority;

  jclient->context.options = OW_ENGINE_OPTION_O2H_AUDIO;

  err = ow_resampler_start (jclient->resampler, &jclient->context);
  if (err)
    {
      goto cleanup_jack;
    }

  if (jack_activate (jclient->client))
    {
      error_print ("Cannot activate client");
      err = OW_GENERIC_ERROR;
      goto cleanup_jack;
    }

  ow_resampler_wait (jclient->resampler);

  debug_print (1, "Exiting...");
  jack_deactivate (jclient->client);

cleanup_jack:
  if (jclient->context.h2o_audio)
    {
      jack_ringbuffer_free (jclient->context.h2o_audio);
    }
  if (jclient->context.o2h_audio)
    {
      jack_ringbuffer_free (jclient->context.o2h_audio);
    }
  jack_client_close (jclient->client);
  free (jclient->output_ports);
  free (jclient->input_ports);
  return err;
}

static void *
jclient_thread_runner (void *data)
{
  struct jclient *jclient = data;
  jclient_run (jclient);
  return NULL;
}

int
jclient_start (struct jclient *jclient)
{
  int running, err;
  gchar buf[OW_LABEL_MAX_LEN];

  debug_print (1, "Starting thread...");

  if (pthread_create (&jclient->thread, NULL, jclient_thread_runner, jclient))
    {
      return err;
    }

  snprintf (buf, OW_LABEL_MAX_LEN, "jclient-%.7s",
	    jclient->device->desc.name);
  pthread_setname_np (jclient->thread, buf);

  while (1)
    {
      pthread_spin_lock (&jclient->lock);
      running = jclient->running;
      pthread_spin_unlock (&jclient->lock);

      if (running)
	{
	  break;
	}

      debug_print (2, "Waiting for the thread to be ready...");

      usleep (JCLIENT_WAIT_TIME_US);
    }

  return 0;
}

void
jclient_wait (struct jclient *jclient)
{
  pthread_join (jclient->thread, NULL);
  jclient->running = 0;
}
