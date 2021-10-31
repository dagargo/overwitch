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

static void
jclient_init_dll (struct dll *dll)
{
  dll->_z1 = 0.0;
  dll->_z2 = 0.0;
  dll->_z3 = 0.0;
  dll->ratio_sum = 0.0;
  dll->ratio_avg = 0.0;
  dll->last_ratio_avg = 0.0;
}

static void
jclient_set_loop_filter (struct jclient *jclient, struct dll * dll, double bw)
{
  //Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
  double w = 2.0 * M_PI * 20 * bw * jclient->bufsize / jclient->samplerate;
  dll->_w0 = 1.0 - exp (-w);
  w = 2.0 * M_PI * bw * dll->ratio / jclient->samplerate;
  dll->_w1 = w * 1.6;
  dll->_w2 = w * jclient->bufsize / 1.6;
}

static void
jclient_init_sample_rate (struct jclient *jclient)
{
  jclient->o2j_dll.ratio = jclient->samplerate / OB_SAMPLE_RATE;
  jclient->j2o_dll.ratio = jclient->samplerate / OB_SAMPLE_RATE;
  jclient->o2j_dll.ratio_max = 1.05 * jclient->o2j_dll.ratio;
  jclient->o2j_dll.ratio_min = 0.95 * jclient->o2j_dll.ratio;
}

static void
jclient_init_buffer_size (struct jclient *jclient)
{
  jclient->o2j_dll.kj = jclient->bufsize / -jclient->o2j_dll.ratio;
  jclient->read_frames = jclient->bufsize * jclient->j2o_dll.ratio;

  jclient->o2j_dll.kdel =
    (OB_FRAMES_PER_BLOCK * jclient->ob.blocks_per_transfer) +
    1.5 * (jclient->bufsize);
  debug_print (2, "Target delay: %.1f ms (%d frames)\n",
	       jclient->o2j_dll.kdel * 1000 / OB_SAMPLE_RATE,
	       jclient->o2j_dll.kdel);

  jclient->log_control_cycles =
    STARTUP_TIME * jclient->samplerate / jclient->bufsize;

  jclient->o2j_buf_size = jclient->bufsize * jclient->ob.o2j_frame_bytes;
  jclient->j2o_buf_size = jclient->bufsize * jclient->ob.j2o_frame_bytes;
}

static int
jclient_thread_xrun_cb (void *arg)
{
  error_print ("JACK xrun\n");
  return 0;
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
	  jclient->j2o_queue_len * jclient->ob.j2o_frame_bytes);
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
	}
      else
	{
	  debug_print (2,
		       "o2j: Can not read data from ring buffer. Replicating last sample...\n");
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
	  frames = rso2j / jclient->ob.o2j_frame_bytes;
	  bytes = frames * jclient->ob.o2j_frame_bytes;
	  jack_ringbuffer_read_advance (jclient->ob.o2j_rb, bytes);
	  frames = MAX_READ_FRAMES;
	  running = 1;
	}
      else
	{
	  frames = MAX_READ_FRAMES;
	}
    }

  jclient->read_frames += frames;
  last_frames = frames;
  return frames;
}

static inline void
jclient_o2j (struct jclient *jclient)
{
  long gen_frames;

  jclient->read_frames = 0;
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

static inline void
jclient_j2o (struct jclient *jclient)
{
  long gen_frames;
  int inc;
  int frames;
  size_t bytes;
  size_t wsj2o;
  static double j2o_acc = .0;

  memcpy (&jclient->j2o_queue[jclient->j2o_queue_len], jclient->j2o_aux,
	  jclient->j2o_buf_size);
  jclient->j2o_queue_len += jclient->bufsize;

  j2o_acc += jclient->bufsize * (jclient->j2o_dll.ratio - 1.0);
  inc = trunc (j2o_acc);
  j2o_acc -= inc;
  frames = jclient->bufsize + inc;

  gen_frames =
    src_callback_read (jclient->j2o_state, jclient->j2o_dll.ratio, frames,
		       jclient->j2o_buf_out);
  if (gen_frames != frames)
    {
      error_print
	("j2o: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	 jclient->j2o_dll.ratio, gen_frames, frames);
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
      error_print ("j2o: Buffer overflow. Discarding data...\n");
    }
}

static inline void
jclient_compute_ratios (struct jclient *jclient)
{
  jack_time_t current_usecs;
  jack_time_t next_usecs;
  float period_usecs;
  jack_nframes_t ko0;
  jack_nframes_t ko1;
  double to0;
  double to1;
  double tj;
  jack_nframes_t frames;
  double dob;
  double err;
  int n;
  struct dll *dll = &jclient->o2j_dll;
  static int i = 0;

  if (jack_get_cycle_times (jclient->client,
			    &jclient->current_frames,
			    &current_usecs, &next_usecs, &period_usecs))
    {
      error_print ("Error while getting JACK time\n");
    }

  pthread_spin_lock (&jclient->ob.lock);
  jclient->j2o_latency = jclient->ob.j2o_latency;
  ko0 = jclient->ob.o2j_counter.i0.frames;
  to0 = jclient->ob.o2j_counter.i0.time;
  ko1 = jclient->ob.o2j_counter.i1.frames;
  to1 = jclient->ob.o2j_counter.i1.time;
  jclient->status = jclient->ob.status;
  pthread_spin_unlock (&jclient->ob.lock);

  if (jclient->status == OB_STATUS_BOOT)
    {
      pthread_spin_lock (&jclient->ob.lock);
      jclient->ob.status = OB_STATUS_SKIP;
      pthread_spin_unlock (&jclient->ob.lock);

      return;
    }

  //The whole calculation of the delay and the loop filter is taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
  dll->kj += jclient->read_frames;
  tj = current_usecs * 1.0e-6;

  frames = ko1 - ko0;
  dob = frames * (tj - to0) / (to1 - to0);
  frames = ko0 - dll->kj;
  err = frames + dob - dll->kdel;

  if (jclient->status == OB_STATUS_SKIP)
    {
      //Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
      n = (int) (floor (err + 0.5));
      dll->kj += n;
      err -= n;

      debug_print (2, "Starting up...\n");

      jclient_set_loop_filter (jclient, dll, 1.0);

      pthread_spin_lock (&jclient->ob.lock);
      jclient->ob.status = OB_STATUS_STARTUP;
      pthread_spin_unlock (&jclient->ob.lock);
    }

  dll->_z1 += dll->_w0 * (dll->_w1 * err - dll->_z1);
  dll->_z2 += dll->_w0 * (dll->_z1 - dll->_z2);
  dll->_z3 += dll->_w2 * dll->_z2;
  dll->ratio = 1.0 - dll->_z2 - dll->_z3;
  if (dll->ratio > dll->ratio_max)
    {
      dll->ratio = dll->ratio_max;
    }
  if (dll->ratio < dll->ratio_min)
    {
      dll->ratio = dll->ratio_min;
    }
  jclient->j2o_dll.ratio = 1.0 / dll->ratio;

  i++;
  dll->ratio_sum += dll->ratio;
  if (i == jclient->log_control_cycles)
    {
      dll->last_ratio_avg = dll->ratio_avg;

      dll->ratio_avg = dll->ratio_sum / jclient->log_control_cycles;

      debug_print (1,
		   "Max. latencies (ms): %.1f; avg. ratios: %f; curr. ratios: %f\n",
		   jclient->o2j_latency * 1000.0 /
		   (jclient->ob.o2j_frame_bytes * OB_SAMPLE_RATE),
		   dll->ratio_avg, dll->ratio);

      i = 0;
      dll->ratio_sum = 0.0;

      if (jclient->status == OB_STATUS_STARTUP)
	{
	  debug_print (2, "Tunning...\n");
	  jclient_set_loop_filter (jclient, dll, 0.05);

	  pthread_spin_lock (&jclient->ob.lock);
	  jclient->ob.status = OB_STATUS_TUNE;
	  pthread_spin_unlock (&jclient->ob.lock);

	  jclient->log_control_cycles =
	    LOG_TIME * jclient->samplerate / jclient->bufsize;
	}

      if (jclient->status == OB_STATUS_TUNE
	  && fabs (dll->ratio_avg - dll->last_ratio_avg) < RATIO_DIFF_THRES)
	{
	  debug_print (2, "Running...\n");
	  jclient_set_loop_filter (jclient, dll, 0.02);

	  pthread_spin_lock (&jclient->ob.lock);
	  jclient->ob.status = OB_STATUS_RUN;
	  pthread_spin_unlock (&jclient->ob.lock);
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
      else if (jevent.size == 3)
	{
	  switch (status_byte & 0xf0)
	    {
	    case 0x80:		//Note-off
	      oevent.bytes[0] = 0x08;
	      break;
	    case 0x90:		//Note-on
	      oevent.bytes[0] = 0x09;
	      break;
	    case 0xa0:		//Poly-KeyPress
	      oevent.bytes[0] = 0x0a;
	      break;
	    case 0xb0:		//Control Change
	      oevent.bytes[0] = 0x0b;
	      break;
	    case 0xc0:		//Program Change
	      oevent.bytes[0] = 0x0c;
	      break;
	    case 0xd0:		//Channel Pressure
	      oevent.bytes[0] = 0x0d;
	      break;
	    case 0xe0:		//PitchBend Change
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
	      error_print ("j2o: Buffer MIDI overflow. Discarding data...\n");
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

  jclient_compute_ratios (jclient);

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

  jclient_o2j_midi (jclient, nframes);

  jclient_j2o_midi (jclient, nframes);

  return 0;
}

void
jclient_print_status (struct jclient *jclient)
{
  printf ("Max. latencies (ms): %.1f, %.1f\n",
	  jclient->o2j_latency * 1000.0 / (jclient->ob.o2j_frame_bytes *
					   OB_SAMPLE_RATE),
	  jclient->j2o_latency * 1000.0 / (jclient->ob.j2o_frame_bytes *
					   OB_SAMPLE_RATE));
}

void
jclient_exit (struct jclient *jclient)
{
  jclient_print_status (jclient);
  overbridge_set_status (&jclient->ob, OB_STATUS_STOP);
}

int
jclient_run (struct jclient *jclient, char *device_name,
	     int blocks_per_transfer, int quality)
{
  jack_options_t options = JackNoStartServer;
  jack_status_t status;
  overbridge_err_t ob_status;
  char *client_name;
  size_t o2j_bufsize;
  size_t j2o_bufsize;
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

  if (jack_set_xrun_callback (jclient->client, jclient_thread_xrun_cb, NULL))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  jclient->samplerate = jack_get_sample_rate (jclient->client);
  printf ("JACK sample rate: %.0f\n", jclient->samplerate);
  jclient_init_sample_rate (jclient);

  jclient->bufsize = jack_get_buffer_size (jclient->client);
  printf ("JACK buffer size: %d\n", jclient->bufsize);
  jclient_init_buffer_size (jclient);

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
    jack_port_register (jclient->client,
			"MIDI out",
			JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

  if (jclient->midi_output_port == NULL)
    {
      error_print (MSG_ERROR_PORT_REGISTER);
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  jclient->midi_input_port =
    jack_port_register (jclient->client,
			"MIDI in",
			JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

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

  j2o_bufsize =
    jclient->bufsize * OB_BYTES_PER_FRAME * jclient->ob.device_desc.inputs;
  jclient->j2o_buf_in = malloc (j2o_bufsize);
  jclient->j2o_buf_out = malloc (j2o_bufsize * 4.5);	//Up to 192 kHz and a few samples more
  jclient->j2o_aux = malloc (j2o_bufsize);
  jclient->j2o_queue = malloc (j2o_bufsize * 4.5);	//Up to 192 kHz and a few samples more
  jclient->j2o_queue_len = 0;

  o2j_bufsize =
    jclient->bufsize * OB_BYTES_PER_FRAME * jclient->ob.device_desc.outputs;
  jclient->o2j_buf_in = malloc (o2j_bufsize);
  jclient->o2j_buf_out = malloc (o2j_bufsize);

  memset (jclient->j2o_buf_in, 0, j2o_bufsize);
  memset (jclient->o2j_buf_in, 0, o2j_bufsize);

  if (overbridge_run (&jclient->ob, jclient->client))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  jclient_init_dll (&jclient->o2j_dll);

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
