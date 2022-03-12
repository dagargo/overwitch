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

#define MSG_ERROR_PORT_REGISTER "Error while registering JACK port\n"

#define MIDI_BUF_EVENTS 64
#define MIDI_BUF_SIZE (MIDI_BUF_EVENTS * OB_MIDI_EVENT_SIZE * 8)

#define MAX_LATENCY (8192 * 2)	//This is twice the maximum JACK latency.

double
jclient_get_time ()
{
  return jack_get_time () * 1.0e-6;
}

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
  error_print ("JACK xrun\n");
  ow_resampler_inc_xruns (resampler);
  return 0;
}

static void
jclient_port_connect_cb (jack_port_id_t a, jack_port_id_t b, int connect,
			 void *cb_data)
{
  struct jclient *jclient = cb_data;
  int p2o_enabled = 0;
  struct ow_engine *engine = ow_resampler_get_engine (jclient->resampler);
  const struct ow_device_desc *desc = ow_engine_get_device_desc (engine);
  //We only check for j2o (imput) ports as o2j must always be running.
  for (int i = 0; i < desc->inputs; i++)
    {
      if (jack_port_connected (jclient->input_ports[i]))
	{
	  p2o_enabled = 1;
	  break;
	}
    }
  ow_engine_set_p2o_audio_enable (engine, p2o_enabled);
}

static void
jclient_jack_shutdown_cb (jack_status_t code, const char *reason,
			  void *cb_data)
{
  struct jclient *jclient = cb_data;
  jclient_exit (jclient);
}

static int
jclient_set_buffer_size_cb (jack_nframes_t nframes, void *cb_data)
{
  struct jclient *jclient = cb_data;
  printf ("JACK buffer size: %d\n", nframes);
  jclient->bufsize = nframes;
  ow_resampler_set_buffer_size (jclient->resampler, nframes);
  return 0;
}

static int
jclient_set_sample_rate_cb (jack_nframes_t nframes, void *cb_data)
{
  struct jclient *jclient = cb_data;
  printf ("JACK sample rate: %d\n", nframes);
  ow_resampler_set_samplerate (jclient->resampler, nframes);
  return 0;
}

static inline void
jclient_o2j_midi (struct jclient *jclient, jack_nframes_t nframes)
{
  size_t data_size;
  void *midi_port_buf;
  jack_midi_data_t *jmidi;
  struct ow_midi_event event;
  jack_nframes_t last_frames, frames, last_frame_time, event_frames;

  midi_port_buf = jack_port_get_buffer (jclient->midi_output_port, nframes);
  jack_midi_clear_buffer (midi_port_buf);
  last_frames = 0;

  while (jack_ringbuffer_read_space (jclient->io_buffers.o2p_midi) >=
	 sizeof (struct ow_midi_event))
    {
      jack_ringbuffer_peek (jclient->io_buffers.o2p_midi, (void *) &event,
			    sizeof (struct ow_midi_event));

      event_frames = jack_time_to_frames (jclient->client, event.time);
      last_frame_time = jack_time_to_frames (jclient->client, event.time);

      if (last_frame_time < event_frames)
	{
	  debug_print (2, "Event delayed: %u frames\n",
		       event_frames - last_frame_time);
	  frames = 0;
	}
      else
	{
	  frames = (last_frame_time - event_frames) % jclient->bufsize;
	}

      debug_print (2, "Event frames: %u\n", frames);

      if (frames < last_frames)
	{
	  debug_print (2, "Skipping until the next cycle...\n");
	  last_frames = 0;
	  break;
	}
      last_frames = frames;

      jack_ringbuffer_read_advance (jclient->io_buffers.o2p_midi,
				    sizeof (struct ow_midi_event));

      if (event.bytes[0] == 0x0f)
	{
	  data_size = 1;
	}
      else
	{
	  data_size = 3;
	}
      jmidi = jack_midi_event_reserve (midi_port_buf, frames, data_size);
      if (jmidi)
	{
	  jmidi[0] = event.bytes[1];
	  if (data_size == 3)
	    {
	      jmidi[1] = event.bytes[2];
	      jmidi[2] = event.bytes[3];
	    }
	}
    }
}

static inline void
jclient_j2o_midi (struct jclient *jclient, jack_nframes_t nframes)
{
  jack_midi_event_t jevent;
  void *midi_port_buf;
  struct ow_midi_event oevent;
  jack_nframes_t event_count;
  jack_midi_data_t status_byte;

  if (ow_resampler_get_status (jclient->resampler) < RES_STATUS_RUN)
    {
      return;
    }

  midi_port_buf = jack_port_get_buffer (jclient->midi_input_port, nframes);
  event_count = jack_midi_get_event_count (midi_port_buf);

  for (int i = 0; i < event_count; i++)
    {
      oevent.bytes[0] = 0;
      jack_midi_event_get (&jevent, midi_port_buf, i);
      status_byte = jevent.buffer[0];

      if (jevent.size == 1 && status_byte >= 0xf8 && status_byte <= 0xfc)
	{
	  oevent.bytes[0] = 0x0f;	//Single Byte
	  oevent.bytes[1] = jevent.buffer[0];
	}
      else if (jevent.size == 2)
	{
	  switch (status_byte & 0xf0)
	    {
	    case 0xc0:		//Program Change
	      oevent.bytes[0] = 0x0c;
	      break;
	    case 0xd0:		//Channel Pressure (After-touch)
	      oevent.bytes[0] = 0x0d;
	      break;
	    }
	  oevent.bytes[1] = jevent.buffer[0];
	  oevent.bytes[2] = jevent.buffer[1];
	}
      else if (jevent.size == 3)
	{
	  switch (status_byte & 0xf0)
	    {
	    case 0x80:		//Note Off
	      oevent.bytes[0] = 0x08;
	      break;
	    case 0x90:		//Note On
	      oevent.bytes[0] = 0x09;
	      break;
	    case 0xa0:		//Polyphonic Key Pressure
	      oevent.bytes[0] = 0x0a;
	      break;
	    case 0xb0:		//Control Change
	      oevent.bytes[0] = 0x0b;
	      break;
	    case 0xe0:		//Pitch Bend Change
	      oevent.bytes[0] = 0x0e;
	      break;
	    }
	  oevent.bytes[1] = jevent.buffer[0];
	  oevent.bytes[2] = jevent.buffer[1];
	  oevent.bytes[3] = jevent.buffer[2];
	}

      oevent.time = jack_frames_to_time (jclient->client, jevent.time);

      if (oevent.bytes[0])
	{
	  if (jack_ringbuffer_write_space (jclient->io_buffers.p2o_midi)
	      >= sizeof (struct ow_midi_event))
	    {
	      jack_ringbuffer_write (jclient->io_buffers.p2o_midi,
				     (void *) &oevent,
				     sizeof (struct ow_midi_event));
	    }
	  else
	    {
	      error_print
		("j2o: MIDI ring buffer overflow. Discarding data...\n");
	    }
	}
    }
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
  double time;
  struct ow_engine *engine = ow_resampler_get_engine (jclient->resampler);
  const struct ow_device_desc *desc = ow_engine_get_device_desc (engine);

  if (jack_get_cycle_times (jclient->client,
			    &current_frames,
			    &current_usecs, &next_usecs, &period_usecs))
    {
      error_print ("Error while getting JACK time\n");
    }

  time = current_usecs * 1.0e-6;

  if (ow_resampler_compute_ratios (jclient->resampler, time))
    {
      return 0;
    }

  ow_resampler_read_audio (jclient->resampler);

  //o2p

  f = ow_resampler_get_o2p_audio_buffer (jclient->resampler);
  for (int i = 0; i < desc->outputs; i++)
    {
      buffer[i] = jack_port_get_buffer (jclient->output_ports[i], nframes);
    }
  for (int i = 0; i < nframes; i++)
    {
      for (int j = 0; j < desc->outputs; j++)
	{
	  buffer[j][i] = *f;
	  f++;
	}
    }

  //p2o

  if (ow_engine_is_p2o_audio_enable (engine))
    {
      f = ow_resampler_get_p2o_audio_buffer (jclient->resampler);
      for (int i = 0; i < desc->inputs; i++)
	{
	  buffer[i] = jack_port_get_buffer (jclient->input_ports[i], nframes);
	}
      for (int i = 0; i < nframes; i++)
	{
	  for (int j = 0; j < desc->inputs; j++)
	    {
	      *f = buffer[j][i];
	      f++;
	    }
	}

      ow_resampler_write_audio (jclient->resampler);
    }

  jclient_o2j_midi (jclient, nframes);

  jclient_j2o_midi (jclient, nframes);

  return 0;
}

static void
set_rt_priority (pthread_t * thread, int priority)
{
  int err = jack_acquire_real_time_scheduling (*thread, priority);
  if (err)
    {
      error_print ("Could not set real time priority\n");
    }
}

void
jclient_exit (struct jclient *jclient)
{
  ow_resampler_print_status (jclient->resampler);
  ow_resampler_stop (jclient->resampler);
}

int
jclient_run (struct jclient *jclient)
{
  jack_options_t options = JackNoStartServer;
  jack_status_t status;
  ow_err_t err = OW_OK;
  char *client_name;
  struct ow_engine *engine;
  const struct ow_device_desc *desc;

  err = ow_resampler_init (&jclient->resampler, jclient->bus,
			   jclient->address, jclient->blocks_per_transfer,
			   jclient->quality);
  if (err)
    {
      error_print ("Overwitch error: %s\n", ow_get_err_str (err));
      goto end;
    }

  engine = ow_resampler_get_engine (jclient->resampler);
  desc = ow_engine_get_device_desc (engine);

  jclient->client = jack_client_open (desc->name, options, &status, NULL);
  if (jclient->client == NULL)
    {
      error_print ("jack_client_open() failed, status = 0x%2.0x\n", status);

      if (status & JackServerFailed)
	{
	  error_print ("Unable to connect to JACK server\n");
	}

      goto cleanup_jack;
    }

  if (status & JackServerStarted)
    {
      debug_print (1, "JACK server started\n");
    }

  if (status & JackNameNotUnique)
    {
      client_name = jack_get_client_name (jclient->client);
      debug_print (0, "Name client in use. Using %s...\n", client_name);
    }

  if (jack_set_process_callback
      (jclient->client, jclient_process_cb, jclient))
    {
      goto cleanup_jack;
    }

  if (jack_set_xrun_callback
      (jclient->client, jclient_thread_xrun_cb, jclient->resampler))
    {
      goto cleanup_jack;
    }

  if (jack_set_port_connect_callback (jclient->client,
				      jclient_port_connect_cb, jclient))
    {
      error_print
	("Cannot set port connect callback so j2o audio will not be possible\n");
    }

  jack_on_info_shutdown (jclient->client, jclient_jack_shutdown_cb, jclient);

  if (jack_set_sample_rate_callback
      (jclient->client, jclient_set_sample_rate_cb, jclient))
    {
      goto cleanup_jack;
    }

  if (jack_set_buffer_size_callback
      (jclient->client, jclient_set_buffer_size_cb, jclient))
    {
      goto cleanup_jack;
    }

  if (jclient->priority < 0)
    {
      jclient->priority = jack_client_real_time_priority (jclient->client);
    }
  debug_print (1, "Using RT priority %d...\n", jclient->priority);

  jclient->output_ports = malloc (sizeof (jack_port_t *) * desc->outputs);
  for (int i = 0; i < desc->outputs; i++)
    {
      const char *name = desc->output_track_names[i];
      jclient->output_ports[i] = jack_port_register (jclient->client,
						     name,
						     JACK_DEFAULT_AUDIO_TYPE,
						     JackPortIsOutput, 0);

      if (jclient->output_ports[i] == NULL)
	{
	  error_print (MSG_ERROR_PORT_REGISTER);
	  goto cleanup_jack;
	}
    }

  jclient->input_ports = malloc (sizeof (jack_port_t *) * desc->inputs);
  for (int i = 0; i < desc->inputs; i++)
    {
      const char *name = desc->input_track_names[i];
      jclient->input_ports[i] = jack_port_register (jclient->client,
						    name,
						    JACK_DEFAULT_AUDIO_TYPE,
						    JackPortIsInput, 0);

      if (jclient->input_ports[i] == NULL)
	{
	  error_print (MSG_ERROR_PORT_REGISTER);
	  goto cleanup_jack;
	}
    }

  jclient->midi_output_port =
    jack_port_register (jclient->client, "MIDI out", JACK_DEFAULT_MIDI_TYPE,
			JackPortIsOutput, 0);

  if (jclient->midi_output_port == NULL)
    {
      error_print (MSG_ERROR_PORT_REGISTER);
      goto cleanup_jack;
    }

  jclient->midi_input_port =
    jack_port_register (jclient->client, "MIDI in", JACK_DEFAULT_MIDI_TYPE,
			JackPortIsInput, 0);

  if (jclient->midi_input_port == NULL)
    {
      error_print (MSG_ERROR_PORT_REGISTER);
      goto cleanup_jack;
    }

  jclient->io_buffers.o2p_audio =
    jack_ringbuffer_create (MAX_LATENCY *
			    ow_resampler_get_o2p_frame_size
			    (jclient->resampler));
  jack_ringbuffer_mlock (jclient->io_buffers.o2p_audio);

  jclient->io_buffers.p2o_audio =
    jack_ringbuffer_create (MAX_LATENCY *
			    ow_resampler_get_p2o_frame_size
			    (jclient->resampler));
  jack_ringbuffer_mlock (jclient->io_buffers.p2o_audio);

  jclient->io_buffers.o2p_midi = jack_ringbuffer_create (MIDI_BUF_SIZE);
  jack_ringbuffer_mlock (jclient->io_buffers.o2p_midi);

  jclient->io_buffers.p2o_midi = jack_ringbuffer_create (MIDI_BUF_SIZE);
  jack_ringbuffer_mlock (jclient->io_buffers.p2o_midi);

  jclient->io_buffers.read_space =
    (ow_buffer_rw_space_t) jack_ringbuffer_read_space;
  jclient->io_buffers.write_space =
    (ow_buffer_rw_space_t) jack_ringbuffer_write_space;
  jclient->io_buffers.read = jclient_buffer_read;
  jclient->io_buffers.write = (ow_buffer_write_t) jack_ringbuffer_write;
  jclient->io_buffers.get_time = jclient_get_time;

  err =
    ow_resampler_activate (jclient->resampler, &jclient->io_buffers,
			   jclient->priority, set_rt_priority);
  if (err)
    {
      goto cleanup_jack;
    }

  //Sometimes these callbacks are not called so we need to do it.
  jclient_set_sample_rate_cb (jack_get_sample_rate (jclient->client),
			      jclient);
  jclient_set_buffer_size_cb (jack_get_buffer_size (jclient->client),
			      jclient);

  if (jack_activate (jclient->client))
    {
      error_print ("Cannot activate client\n");
      err = -1;
      goto cleanup_jack;
    }

  ow_resampler_wait (jclient->resampler);

  debug_print (1, "Exiting...\n");
  jack_deactivate (jclient->client);

cleanup_jack:
  jack_ringbuffer_free (jclient->io_buffers.p2o_audio);
  jack_ringbuffer_free (jclient->io_buffers.o2p_audio);
  jack_ringbuffer_free (jclient->io_buffers.p2o_midi);
  jack_ringbuffer_free (jclient->io_buffers.o2p_midi);
  jack_client_close (jclient->client);
  free (jclient->output_ports);
  free (jclient->input_ports);
  ow_resampler_destroy (jclient->resampler);

end:
  return err;
}

void *
jclient_run_thread (void *data)
{
  struct jclient *jclient = data;
  jclient_run (jclient);
  return NULL;
}
