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
#include <math.h>
#include <jack/intclient.h>
#include <jack/thread.h>
#include <jack/midiport.h>

#include "jclient.h"

#define MAX_READ_FRAMES 5
#define STARTUP_TIME 5
#define LOG_TIME 2
#define RATIO_DIFF_THRES 0.00001

#define MSG_ERROR_PORT_REGISTER "Error while registering JACK port\n"

#define LATENCY_MSG_LEN 1024

void
jclient_print_latencies (struct jclient *jclient, const char *end)
{
  printf ("Max. o2j latency: %.1f ms, max. j2o latency: %.1f ms%s",
	  jclient->o2j_latency * 1000.0 / (jclient->ob.o2j_frame_bytes *
					   OB_SAMPLE_RATE),
	  jclient->j2o_latency * 1000.0 / (jclient->ob.j2o_frame_bytes *
					   OB_SAMPLE_RATE), end);
}

void
jclient_init (struct jclient *jclient)
{
  size_t j2o_bufsize = jclient->bufsize * jclient->ob.j2o_frame_bytes;
  size_t o2j_bufsize = jclient->bufsize * jclient->ob.o2j_frame_bytes;

  //The 8 times scale allow up to more than 192 kHz sample rate in JACK.
  jclient->j2o_buf_in = malloc (j2o_bufsize * 8);
  jclient->j2o_buf_out = malloc (j2o_bufsize * 8);
  jclient->j2o_aux = malloc (j2o_bufsize);
  jclient->j2o_queue = malloc (j2o_bufsize * 8);
  jclient->j2o_queue_len = 0;


  jclient->o2j_buf_in = malloc (o2j_bufsize);
  jclient->o2j_buf_out = malloc (o2j_bufsize);

  memset (jclient->j2o_buf_in, 0, j2o_bufsize);
  memset (jclient->o2j_buf_in, 0, o2j_bufsize);

  jclient->log_control_cycles =
    STARTUP_TIME * jclient->samplerate / jclient->bufsize;

  jclient->o2j_buf_size = jclient->bufsize * jclient->ob.o2j_frame_bytes;
  jclient->j2o_buf_size = jclient->bufsize * jclient->ob.j2o_frame_bytes;
}

static int
jclient_thread_xrun_cb (void *cb_data)
{
  struct jclient *jclient = cb_data;
  error_print ("JACK xrun\n");
  jclient->xrun = 1;
  return 0;
}

static void
jclient_port_connect_cb (jack_port_id_t a, jack_port_id_t b, int connect,
			 void *cb_data)
{
  struct jclient *jclient = cb_data;
  int j2o_enabled = 0;
  //We only check for j2o (imput) ports as o2j must always be running.
  for (int i = 0; i < jclient->ob.device_desc.inputs; i++)
    {
      if (jack_port_connected (jclient->input_ports[i]))
	{
	  j2o_enabled = 1;
	  break;
	}
    }
  overbridge_set_j2o_audio_enable (&jclient->ob, j2o_enabled);
}

static void
jclient_jack_shutdown_cb (jack_status_t code, const char *reason,
			  void *cb_data)
{
  struct jclient *jclient = cb_data;
  jclient_exit (jclient);
}

static long
jclient_j2o_reader (void *cb_data, float **data)
{
  long ret;
  struct jclient *jclient = cb_data;

  *data = jclient->j2o_buf_in;

  if (jclient->j2o_queue_len == 0)
    {
      debug_print (2, "j2o: Can not read data from queue\n");
      return jclient->bufsize;
    }

  ret = jclient->j2o_queue_len;
  memcpy (jclient->j2o_buf_in, jclient->j2o_queue,
	  ret * jclient->ob.j2o_frame_bytes);
  jclient->j2o_queue_len = 0;

  return ret;
}

static long
jclient_o2j_reader (void *cb_data, float **data)
{
  size_t rso2j;
  size_t bytes;
  long frames;
  static int last_frames = 1;
  static int running = 0;
  struct jclient *jclient = cb_data;

  *data = jclient->o2j_buf_in;

  rso2j = jack_ringbuffer_read_space (jclient->ob.o2j_rb);
  if (running)
    {
      if (jclient->o2j_latency < rso2j)
	{
	  jclient->o2j_latency = rso2j;
	}

      if (rso2j >= jclient->ob.o2j_frame_bytes)
	{
	  frames = rso2j / jclient->ob.o2j_frame_bytes;
	  frames = frames > MAX_READ_FRAMES ? MAX_READ_FRAMES : frames;
	  bytes = frames * jclient->ob.o2j_frame_bytes;
	  jack_ringbuffer_read (jclient->ob.o2j_rb,
				(void *) jclient->o2j_buf_in, bytes);
	  if (jclient->xrun)
	    {
	      jack_ringbuffer_read (jclient->ob.o2j_rb,
				    (void *) jclient->o2j_buf_in, bytes);
	      jclient->o2j_dll.kj += frames;
	      jclient->xrun = 0;
	    }
	}
      else
	{
	  debug_print (2,
		       "o2j: Audio ring buffer underflow (%zu < %zu). Replicating last sample...\n",
		       rso2j, jclient->ob.o2j_buf_size);
	  if (last_frames > 1)
	    {
	      memcpy (jclient->o2j_buf_in,
		      &jclient->o2j_buf_in[(last_frames - 1) *
					   jclient->ob.device_desc.outputs],
		      jclient->ob.o2j_frame_bytes);
	    }
	  frames = MAX_READ_FRAMES;
	}
    }
  else
    {
      if (rso2j >= jclient->o2j_buf_size)
	{
	  debug_print (2, "o2j: Emptying buffer and running...\n");
	  frames = rso2j / jclient->ob.o2j_frame_bytes;
	  bytes = frames * jclient->ob.o2j_frame_bytes;
	  jack_ringbuffer_read_advance (jclient->ob.o2j_rb, bytes);
	  running = 1;
	}
      frames = MAX_READ_FRAMES;
    }

  jclient->o2j_dll.kj += frames;
  last_frames = frames;
  return frames;
}

static inline void
jclient_o2j (struct jclient *jclient)
{
  long gen_frames;

  gen_frames =
    src_callback_read (jclient->o2j_state, jclient->o2j_dll.ratio,
		       jclient->bufsize, jclient->o2j_buf_out);
  if (gen_frames != jclient->bufsize)
    {
      error_print
	("o2j: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	 jclient->o2j_dll.ratio, gen_frames, jclient->bufsize);
    }
}

//In the j2o case, we do not need to take care of the xruns as the only thing
//that could happen is that the Overbridge side at the other end of the
//j2o ring buffer might need to resample to compensate for the missing frames.
static inline void
jclient_j2o (struct jclient *jclient)
{
  long gen_frames;
  int inc;
  int frames;
  size_t bytes;
  size_t wsj2o;
  static double j2o_acc = .0;

  memcpy (&jclient->j2o_queue
	  [jclient->j2o_queue_len * jclient->ob.j2o_frame_bytes],
	  jclient->j2o_aux, jclient->j2o_buf_size);
  jclient->j2o_queue_len += jclient->bufsize;

  j2o_acc += jclient->bufsize * (jclient->j2o_ratio - 1.0);
  inc = trunc (j2o_acc);
  j2o_acc -= inc;
  frames = jclient->bufsize + inc;

  gen_frames =
    src_callback_read (jclient->j2o_state, jclient->j2o_ratio, frames,
		       jclient->j2o_buf_out);
  if (gen_frames != frames)
    {
      error_print
	("j2o: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	 jclient->j2o_ratio, gen_frames, frames);
    }

  if (jclient->status < OB_STATUS_RUN)
    {
      return;
    }

  bytes = gen_frames * jclient->ob.j2o_frame_bytes;
  wsj2o = jack_ringbuffer_write_space (jclient->ob.j2o_rb);

  if (bytes <= wsj2o)
    {
      jack_ringbuffer_write (jclient->ob.j2o_rb,
			     (void *) jclient->j2o_buf_out, bytes);
    }
  else
    {
      error_print ("j2o: Audio ring buffer overflow. Discarding data...\n");
    }
}

static inline void
jclient_compute_ratios (struct jclient *jclient, struct dll *dll)
{
  jack_time_t current_usecs;
  jack_time_t next_usecs;
  float period_usecs;
  static int i = 0;
  static char latency_msg[LATENCY_MSG_LEN];

  if (jack_get_cycle_times (jclient->client,
			    &jclient->current_frames,
			    &current_usecs, &next_usecs, &period_usecs))
    {
      error_print ("Error while getting JACK time\n");
    }

  pthread_spin_lock (&jclient->ob.lock);
  jclient->j2o_latency = jclient->ob.j2o_latency;
  dll->ko0 = jclient->ob.o2j_dll_counter.i0.frames;
  dll->to0 = jclient->ob.o2j_dll_counter.i0.time;
  dll->ko1 = jclient->ob.o2j_dll_counter.i1.frames;
  dll->to1 = jclient->ob.o2j_dll_counter.i1.time;
  jclient->status = jclient->ob.status;
  pthread_spin_unlock (&jclient->ob.lock);

  if (jclient->status == OB_STATUS_BOOT)
    {
      overbridge_set_status (&jclient->ob, OB_STATUS_SKIP);
      return;
    }

  dll_update_err (dll, current_usecs);

  if (jclient->status == OB_STATUS_SKIP)
    {
      //Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
      int n = (int) (floor (dll->err + 0.5));
      dll->kj += n;
      dll->err -= n;

      debug_print (2, "Starting up...\n");

      dll_set_loop_filter (dll, 1.0, jclient->bufsize, jclient->samplerate);

      overbridge_set_status (&jclient->ob, OB_STATUS_STARTUP);
    }

  dll_update (dll);
  jclient->j2o_ratio = 1.0 / dll->ratio;

  i++;
  dll->ratio_sum += dll->ratio;
  if (i == jclient->log_control_cycles)
    {
      dll->last_ratio_avg = dll->ratio_avg;

      dll->ratio_avg = dll->ratio_sum / jclient->log_control_cycles;

      if (debug_level)
	{
	  snprintf (latency_msg, LATENCY_MSG_LEN,
		    "; o2j ratio avg.: %f; curr. o2j ratio: %f\n",
		    dll->ratio_avg, dll->ratio);
	  jclient_print_latencies (jclient, latency_msg);
	}

      i = 0;
      dll->ratio_sum = 0.0;

      if (jclient->status == OB_STATUS_STARTUP)
	{
	  debug_print (2, "Tunning...\n");
	  dll_set_loop_filter (dll, 0.05, jclient->bufsize,
			       jclient->samplerate);

	  overbridge_set_status (&jclient->ob, OB_STATUS_TUNE);

	  jclient->log_control_cycles =
	    LOG_TIME * jclient->samplerate / jclient->bufsize;
	}

      if (jclient->status == OB_STATUS_TUNE
	  && fabs (dll->ratio_avg - dll->last_ratio_avg) < RATIO_DIFF_THRES)
	{
	  debug_print (2, "Running...\n");
	  dll_set_loop_filter (dll, 0.02, jclient->bufsize,
			       jclient->samplerate);

	  overbridge_set_status (&jclient->ob, OB_STATUS_RUN);
	}
    }
}

static inline void
jclient_o2j_midi (struct jclient *jclient, jack_nframes_t nframes)
{
  size_t data_size;
  void *midi_port_buf;
  jack_midi_data_t *jmidi;
  struct ob_midi_event event;
  jack_nframes_t last_frames;
  jack_nframes_t frames;

  midi_port_buf = jack_port_get_buffer (jclient->midi_output_port, nframes);
  jack_midi_clear_buffer (midi_port_buf);
  last_frames = 0;

  while (jack_ringbuffer_read_space (jclient->ob.o2j_rb_midi) >=
	 sizeof (struct ob_midi_event))
    {
      jack_ringbuffer_peek (jclient->ob.o2j_rb_midi, (void *) &event,
			    sizeof (struct ob_midi_event));

      frames = event.frames % nframes;

      debug_print (2, "Event frames: %u\n", frames);

      if (frames < last_frames)
	{
	  debug_print (2, "Skipping until the next cycle...\n");
	  last_frames = 0;
	  break;
	}
      last_frames = frames;

      jack_ringbuffer_read_advance (jclient->ob.o2j_rb_midi,
				    sizeof (struct ob_midi_event));

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
  struct ob_midi_event oevent;
  jack_nframes_t event_count;
  jack_midi_data_t status_byte;

  if (jclient->status < OB_STATUS_RUN)
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

      oevent.frames = jevent.time;

      if (oevent.bytes[0])
	{
	  if (jack_ringbuffer_write_space (jclient->ob.j2o_rb_midi) >=
	      sizeof (struct ob_midi_event))
	    {
	      jack_ringbuffer_write (jclient->ob.j2o_rb_midi,
				     (void *) &oevent,
				     sizeof (struct ob_midi_event));
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

  jclient_compute_ratios (jclient, &jclient->o2j_dll);

  jclient_o2j (jclient);

  //o2j

  f = jclient->o2j_buf_out;
  for (int i = 0; i < jclient->ob.device_desc.outputs; i++)
    {
      buffer[i] = jack_port_get_buffer (jclient->output_ports[i], nframes);
    }
  for (int i = 0; i < nframes; i++)
    {
      for (int j = 0; j < jclient->ob.device_desc.outputs; j++)
	{
	  buffer[j][i] = *f;
	  f++;
	}
    }

  //j2o

  if (overbridge_is_j2o_audio_enable (&jclient->ob))
    {
      f = jclient->j2o_aux;
      for (int i = 0; i < jclient->ob.device_desc.inputs; i++)
	{
	  buffer[i] = jack_port_get_buffer (jclient->input_ports[i], nframes);
	}
      for (int i = 0; i < nframes; i++)
	{
	  for (int j = 0; j < jclient->ob.device_desc.inputs; j++)
	    {
	      *f = buffer[j][i];
	      f++;
	    }
	}

      jclient_j2o (jclient);
    }

  jclient_o2j_midi (jclient, nframes);

  jclient_j2o_midi (jclient, nframes);

  return 0;
}

void
jclient_exit (struct jclient *jclient)
{
  jclient_print_latencies (jclient, "\n");
  overbridge_set_status (&jclient->ob, OB_STATUS_STOP);
}

int
jclient_run (struct jclient *jclient, char *device_name,
	     int blocks_per_transfer, int quality, int priority)
{
  jack_options_t options = JackNoStartServer;
  jack_status_t status;
  overbridge_err_t ob_status;
  char *client_name;
  int ret = 0;

  ob_status =
    overbridge_init (&jclient->ob, device_name, blocks_per_transfer);
  if (ob_status)
    {
      error_print ("USB error: %s\n", overbrigde_get_err_str (ob_status));
      exit (EXIT_FAILURE);
    }

  jclient->client =
    jack_client_open (jclient->ob.device_desc.name, options, &status, NULL);
  if (jclient->client == NULL)
    {
      error_print ("jack_client_open() failed, status = 0x%2.0x\n", status);

      if (status & JackServerFailed)
	{
	  error_print ("Unable to connect to JACK server\n");
	}

      ret = EXIT_FAILURE;
      goto cleanup_overbridge;
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
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  jclient->xrun = 0;
  if (jack_set_xrun_callback
      (jclient->client, jclient_thread_xrun_cb, jclient))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  if (jack_set_port_connect_callback (jclient->client,
				      jclient_port_connect_cb, jclient))
    {
      error_print
	("Cannot set port connect callback so j2o audio will not be possible\n");
    }

  jack_on_info_shutdown (jclient->client, jclient_jack_shutdown_cb, jclient);

  jclient->samplerate = jack_get_sample_rate (jclient->client);
  printf ("JACK sample rate: %.0f\n", jclient->samplerate);

  jclient->bufsize = jack_get_buffer_size (jclient->client);
  printf ("JACK buffer size: %d\n", jclient->bufsize);

  if (priority < 0)
    {
      priority = jack_client_real_time_priority (jclient->client);
    }
  debug_print (1, "Using RT priority %d...\n", priority);

  jclient->output_ports =
    malloc (sizeof (jack_port_t *) * jclient->ob.device_desc.outputs);
  for (int i = 0; i < jclient->ob.device_desc.outputs; i++)
    {
      jclient->output_ports[i] =
	jack_port_register (jclient->client,
			    jclient->ob.device_desc.output_track_names[i],
			    JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

      if (jclient->output_ports[i] == NULL)
	{
	  error_print (MSG_ERROR_PORT_REGISTER);
	  ret = EXIT_FAILURE;
	  goto cleanup_jack;
	}
    }

  jclient->input_ports =
    malloc (sizeof (jack_port_t *) * jclient->ob.device_desc.inputs);
  for (int i = 0; i < jclient->ob.device_desc.inputs; i++)
    {
      jclient->input_ports[i] =
	jack_port_register (jclient->client,
			    jclient->ob.device_desc.input_track_names[i],
			    JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

      if (jclient->input_ports[i] == NULL)
	{
	  error_print (MSG_ERROR_PORT_REGISTER);
	  ret = EXIT_FAILURE;
	  goto cleanup_jack;
	}
    }

  jclient->midi_output_port =
    jack_port_register (jclient->client, "MIDI out", JACK_DEFAULT_MIDI_TYPE,
			JackPortIsOutput, 0);

  if (jclient->midi_output_port == NULL)
    {
      error_print (MSG_ERROR_PORT_REGISTER);
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  jclient->midi_input_port =
    jack_port_register (jclient->client, "MIDI in", JACK_DEFAULT_MIDI_TYPE,
			JackPortIsInput, 0);

  if (jclient->midi_input_port == NULL)
    {
      error_print (MSG_ERROR_PORT_REGISTER);
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  jclient->j2o_state =
    src_callback_new (jclient_j2o_reader, quality,
		      jclient->ob.device_desc.inputs, NULL, jclient);
  jclient->o2j_state =
    src_callback_new (jclient_o2j_reader, quality,
		      jclient->ob.device_desc.outputs, NULL, jclient);

  jclient_init (jclient);

  if (overbridge_run (&jclient->ob, jclient->client, priority))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  overbridge_set_status (&jclient->ob, OB_STATUS_BOOT);
  dll_init (&jclient->o2j_dll, jclient->samplerate, OB_SAMPLE_RATE,
	    jclient->bufsize, jclient->ob.frames_per_transfer);

  if (jack_activate (jclient->client))
    {
      error_print ("Cannot activate client\n");
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  overbridge_wait (&jclient->ob);

  debug_print (1, "Exiting...\n");
  jack_deactivate (jclient->client);

cleanup_jack:
  jack_client_close (jclient->client);
  src_delete (jclient->j2o_state);
  src_delete (jclient->o2j_state);
  free (jclient->output_ports);
  free (jclient->input_ports);
  free (jclient->j2o_buf_in);
  free (jclient->j2o_buf_out);
  free (jclient->j2o_aux);
  free (jclient->j2o_queue);
  free (jclient->o2j_buf_in);
  free (jclient->o2j_buf_out);
cleanup_overbridge:
  overbridge_destroy (&jclient->ob);

  return ret;
}
