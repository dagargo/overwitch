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

#define MIDI_BUF_EVENTS 64
#define MIDI_BUF_SIZE (MIDI_BUF_EVENTS * OB_MIDI_EVENT_SIZE)

#define MAX_LATENCY (8192 * 2)	//This is twice the maximum JACK latency.

size_t
jclient_buffer_write_space (void *buffer)
{
  return jack_ringbuffer_write_space (buffer);
}

size_t
jclient_buffer_write (void *buffer, char *src, size_t size)
{
  if (src)
    {
      return jack_ringbuffer_write (buffer, src, size);
    }
  else
    {
      jack_ringbuffer_write_advance (buffer, size);
      return 0;
    }
}

size_t
jclient_buffer_read_space (void *buffer)
{
  return jack_ringbuffer_read_space (buffer);
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


void
jclient_print_latencies (struct jclient *jclient, const char *end)
{
  printf
    ("%s: o2j latency: %.1f ms, max. %.1f ms; j2o latency: %.1f ms, max. %.1f ms%s",
     jclient->ow.device_desc->name,
     jclient->o2j_latency * 1000.0 / (jclient->ow.o2j_frame_size *
				      OB_SAMPLE_RATE),
     jclient->o2j_max_latency * 1000.0 / (jclient->ow.o2j_frame_size *
					  OB_SAMPLE_RATE),
     jclient->j2o_latency * 1000.0 / (jclient->ow.j2o_frame_size *
				      OB_SAMPLE_RATE),
     jclient->j2o_max_latency * 1000.0 / (jclient->ow.j2o_frame_size *
					  OB_SAMPLE_RATE), end);
}

void
jclient_reset_buffers (struct jclient *jclient)
{
  size_t rso2j, bytes;
  size_t j2o_bufsize = jclient->bufsize * jclient->ow.j2o_frame_size;
  size_t o2j_bufsize = jclient->bufsize * jclient->ow.o2j_frame_size;
  if (jclient->j2o_buf_in)
    {
      free (jclient->j2o_buf_in);
      free (jclient->j2o_buf_out);
      free (jclient->j2o_aux);
      free (jclient->j2o_queue);
      free (jclient->o2j_buf_in);
      free (jclient->o2j_buf_out);
    }

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

  jclient->o2j_buf_size = jclient->bufsize * jclient->ow.o2j_frame_size;
  jclient->j2o_buf_size = jclient->bufsize * jclient->ow.j2o_frame_size;

  jclient->j2o_max_latency = 0;
  jclient->o2j_max_latency = 0;
  jclient->j2o_latency = 0;
  jclient->o2j_latency = 0;
  jclient->reading_at_o2j_end = 0;

  rso2j = jack_ringbuffer_read_space (jclient->o2j_audio_rb);
  bytes = overwitch_bytes_to_frame_bytes (rso2j, jclient->ow.o2j_frame_size);
  jack_ringbuffer_read_advance (jclient->o2j_audio_rb, bytes);
}

void
jclient_reset_dll (struct jclient *jclient, jack_nframes_t new_samplerate)
{
  static int init = 0;
  if (!init || overwitch_get_status (&jclient->ow) < OW_STATUS_RUN)
    {
      debug_print (2, "Initializing dll...\n");
      dll_init (&jclient->o2j_dll, new_samplerate, OB_SAMPLE_RATE,
		jclient->bufsize, jclient->ow.frames_per_transfer);
      overwitch_set_status (&jclient->ow, OW_STATUS_READY);
      init = 1;
    }
  else
    {
      debug_print (2, "Just adjusting dll ratio...\n");
      jclient->o2j_dll.ratio =
	jclient->o2j_dll.last_ratio_avg * new_samplerate /
	jclient->samplerate;
      overwitch_set_status (&jclient->ow, OW_STATUS_READY);
      jclient->log_cycles = 0;
      jclient->log_control_cycles =
	STARTUP_TIME * new_samplerate / jclient->bufsize;
    }
  jclient->o2j_ratio = jclient->o2j_dll.ratio;
  jclient->samplerate = new_samplerate;
}

static int
jclient_thread_xrun_cb (void *cb_data)
{
  struct jclient *jclient = cb_data;
  error_print ("JACK xrun\n");
  pthread_spin_lock (&jclient->lock);
  jclient->xruns++;
  pthread_spin_unlock (&jclient->lock);
  return 0;
}

static void
jclient_port_connect_cb (jack_port_id_t a, jack_port_id_t b, int connect,
			 void *cb_data)
{
  struct jclient *jclient = cb_data;
  int j2o_enabled = 0;
  //We only check for j2o (imput) ports as o2j must always be running.
  for (int i = 0; i < jclient->ow.device_desc->inputs; i++)
    {
      if (jack_port_connected (jclient->input_ports[i]))
	{
	  j2o_enabled = 1;
	  break;
	}
    }
  overwitch_set_j2o_audio_enable (&jclient->ow, j2o_enabled);
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
  if (jclient->bufsize != nframes)
    {
      printf ("JACK buffer size: %d\n", nframes);
      jclient->bufsize = nframes;
      jclient_reset_buffers (jclient);
      jclient_reset_dll (jclient, jclient->samplerate);
    }
  return 0;
}

static int
jclient_set_sample_rate_cb (jack_nframes_t nframes, void *cb_data)
{
  struct jclient *jclient = cb_data;
  if (jclient->samplerate != nframes)
    {
      printf ("JACK sample rate: %d\n", nframes);
      if (jclient->j2o_buf_in)	//This means that jclient_reset_buffers has been called and thus bufsize has been set.
	{
	  jclient_reset_dll (jclient, nframes);
	}
      else
	{
	  jclient->samplerate = nframes;
	}
    }
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
	  ret * jclient->ow.j2o_frame_size);
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
  struct jclient *jclient = cb_data;

  *data = jclient->o2j_buf_in;

  rso2j = jack_ringbuffer_read_space (jclient->o2j_audio_rb);
  if (jclient->reading_at_o2j_end)
    {
      jclient->o2j_latency = rso2j;
      if (jclient->o2j_latency > jclient->o2j_max_latency)
	{
	  jclient->o2j_max_latency = jclient->o2j_latency;
	}

      if (rso2j >= jclient->ow.o2j_frame_size)
	{
	  frames = rso2j / jclient->ow.o2j_frame_size;
	  frames = frames > MAX_READ_FRAMES ? MAX_READ_FRAMES : frames;
	  bytes = frames * jclient->ow.o2j_frame_size;
	  jack_ringbuffer_read (jclient->o2j_audio_rb,
				(void *) jclient->o2j_buf_in, bytes);
	}
      else
	{
	  debug_print (2,
		       "o2j: Audio ring buffer underflow (%zu < %zu). Replicating last sample...\n",
		       rso2j, jclient->ow.o2j_transfer_size);
	  if (last_frames > 1)
	    {
	      memcpy (jclient->o2j_buf_in,
		      &jclient->o2j_buf_in[(last_frames - 1) *
					   jclient->ow.device_desc->outputs],
		      jclient->ow.o2j_frame_size);
	    }
	  frames = MAX_READ_FRAMES;
	}
    }
  else
    {
      if (rso2j >= jclient->o2j_buf_size)
	{
	  debug_print (2, "o2j: Emptying buffer and running...\n");
	  bytes =
	    overwitch_bytes_to_frame_bytes (rso2j, jclient->o2j_buf_size);
	  jack_ringbuffer_read_advance (jclient->o2j_audio_rb, bytes);
	  jclient->reading_at_o2j_end = 1;
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
    src_callback_read (jclient->o2j_state, jclient->o2j_ratio,
		       jclient->bufsize, jclient->o2j_buf_out);
  if (gen_frames != jclient->bufsize)
    {
      error_print
	("o2j: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	 jclient->o2j_ratio, gen_frames, jclient->bufsize);
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

  memcpy (&jclient->j2o_queue
	  [jclient->j2o_queue_len * jclient->ow.j2o_frame_size],
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

  if (jclient->status < JC_STATUS_RUN)
    {
      return;
    }

  bytes = gen_frames * jclient->ow.j2o_frame_size;
  wsj2o = jack_ringbuffer_write_space (jclient->j2o_audio_rb);

  if (bytes <= wsj2o)
    {
      jack_ringbuffer_write (jclient->j2o_audio_rb,
			     (void *) jclient->j2o_buf_out, bytes);
    }
  else
    {
      error_print ("j2o: Audio ring buffer overflow. Discarding data...\n");
    }
}

static inline int
jclient_compute_ratios (struct jclient *jclient, struct dll *dll)
{
  jack_nframes_t current_frames;
  jack_time_t current_usecs;
  jack_time_t next_usecs;
  float period_usecs;
  int xruns;
  overwitch_status_t ow_status;
  static char latency_msg[LATENCY_MSG_LEN];

  if (jack_get_cycle_times (jclient->client,
			    &current_frames,
			    &current_usecs, &next_usecs, &period_usecs))
    {
      error_print ("Error while getting JACK time\n");
    }

  pthread_spin_lock (&jclient->lock);
  xruns = jclient->xruns;
  jclient->xruns = 0;
  pthread_spin_unlock (&jclient->lock);

  pthread_spin_lock (&jclient->ow.lock);
  jclient->j2o_latency = jclient->ow.j2o_latency;
  jclient->j2o_max_latency = jclient->ow.j2o_max_latency;
  dll->ko0 = jclient->o2j_dll.counter.i0.frames;
  dll->to0 = jclient->o2j_dll.counter.i0.time;
  dll->ko1 = jclient->o2j_dll.counter.i1.frames;
  dll->to1 = jclient->o2j_dll.counter.i1.time;
  pthread_spin_unlock (&jclient->ow.lock);

  ow_status = overwitch_get_status (&jclient->ow);
  if (jclient->status == JC_STATUS_READY && ow_status <= OW_STATUS_BOOT)
    {
      if (ow_status == OW_STATUS_READY)
	{
	  overwitch_set_status (&jclient->ow, OW_STATUS_BOOT);
	  debug_print (2, "Booting Overbridge side...\n");
	}
      return 1;
    }

  if (jclient->status == JC_STATUS_READY && ow_status == OW_STATUS_WAIT)
    {
      dll_update_err (dll, current_usecs);
      dll_first_time_run (dll);

      debug_print (2, "Starting up...\n");
      dll_set_loop_filter (dll, 1.0, jclient->bufsize, jclient->samplerate);
      jclient->status = JC_STATUS_BOOT;

      jclient->log_cycles = 0;
      jclient->log_control_cycles =
	STARTUP_TIME * jclient->samplerate / jclient->bufsize;

      return 0;
    }

  if (xruns)
    {
      debug_print (2, "Fixing %d xruns...\n", xruns);

      //With this, we try to recover from the unreaded frames that are in the o2j buffer and...
      jclient->o2j_ratio = dll->ratio * (1 + xruns);
      jclient->j2o_ratio = 1.0 / jclient->o2j_ratio;
      jclient_o2j (jclient);

      jclient->j2o_max_latency = 0;
      jclient->o2j_max_latency = 0;

      //... we skip the current cycle dll update as time masurements are not precise enough and would lead to errors.
      return 0;
    }

  dll_update_err (dll, current_usecs);
  dll_update (dll);

  if (dll->ratio < 0.0)
    {
      error_print ("Negative ratio detected. Stopping...\n");
      overwitch_set_status (&jclient->ow, OW_STATUS_ERROR);
      return 1;
    }

  jclient->o2j_ratio = dll->ratio;
  jclient->j2o_ratio = 1.0 / jclient->o2j_ratio;

  jclient->log_cycles++;
  if (jclient->log_cycles == jclient->log_control_cycles)
    {
      dll_calc_avg (dll, jclient->log_control_cycles);

      if (debug_level)
	{
	  snprintf (latency_msg, LATENCY_MSG_LEN,
		    "; o2j ratio: %f, avg. %f\n", dll->ratio, dll->ratio_avg);
	  jclient_print_latencies (jclient, latency_msg);
	}

      jclient->log_cycles = 0;

      if (jclient->status == JC_STATUS_BOOT)
	{
	  debug_print (2, "Tunning...\n");
	  dll_set_loop_filter (dll, 0.05, jclient->bufsize,
			       jclient->samplerate);
	  jclient->status = JC_STATUS_TUNE;
	  jclient->log_control_cycles =
	    LOG_TIME * jclient->samplerate / jclient->bufsize;
	}

      if (jclient->status == JC_STATUS_TUNE
	  && fabs (dll->ratio_avg - dll->last_ratio_avg) < RATIO_DIFF_THRES)
	{
	  debug_print (2, "Running...\n");
	  dll_set_loop_filter (dll, 0.02, jclient->bufsize,
			       jclient->samplerate);
	  jclient->status = JC_STATUS_RUN;
	  overwitch_set_status (&jclient->ow, OW_STATUS_RUN);
	}
    }

  return 0;
}

static inline void
jclient_o2j_midi (struct jclient *jclient, jack_nframes_t nframes)
{
  size_t data_size;
  void *midi_port_buf;
  jack_midi_data_t *jmidi;
  struct overwitch_midi_event event;
  jack_nframes_t last_frames;
  jack_nframes_t frames;

  midi_port_buf = jack_port_get_buffer (jclient->midi_output_port, nframes);
  jack_midi_clear_buffer (midi_port_buf);
  last_frames = 0;

  while (jack_ringbuffer_read_space (jclient->o2j_midi_rb) >=
	 sizeof (struct overwitch_midi_event))
    {
      jack_ringbuffer_peek (jclient->o2j_midi_rb, (void *) &event,
			    sizeof (struct overwitch_midi_event));

      frames = jack_time_to_frames(jclient->client, event.time);

      debug_print (2, "Event frames: %u\n", frames);

      if (frames < last_frames)
	{
	  debug_print (2, "Skipping until the next cycle...\n");
	  last_frames = 0;
	  break;
	}
      last_frames = frames;

      jack_ringbuffer_read_advance (jclient->o2j_midi_rb,
				    sizeof (struct overwitch_midi_event));

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
  struct overwitch_midi_event oevent;
  jack_nframes_t event_count;
  jack_midi_data_t status_byte;

  if (jclient->status < JC_STATUS_RUN)
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
	  if (jack_ringbuffer_write_space (jclient->j2o_midi_rb) >=
	      sizeof (struct overwitch_midi_event))
	    {
	      jack_ringbuffer_write (jclient->j2o_midi_rb,
				     (void *) &oevent,
				     sizeof (struct overwitch_midi_event));
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

  if (jclient_compute_ratios (jclient, &jclient->o2j_dll))
    {
      return 0;
    }

  jclient_o2j (jclient);

  //o2j

  f = jclient->o2j_buf_out;
  for (int i = 0; i < jclient->ow.device_desc->outputs; i++)
    {
      buffer[i] = jack_port_get_buffer (jclient->output_ports[i], nframes);
    }
  for (int i = 0; i < nframes; i++)
    {
      for (int j = 0; j < jclient->ow.device_desc->outputs; j++)
	{
	  buffer[j][i] = *f;
	  f++;
	}
    }

  //j2o

  if (overwitch_is_j2o_audio_enable (&jclient->ow))
    {
      f = jclient->j2o_aux;
      for (int i = 0; i < jclient->ow.device_desc->inputs; i++)
	{
	  buffer[i] = jack_port_get_buffer (jclient->input_ports[i], nframes);
	}
      for (int i = 0; i < nframes; i++)
	{
	  for (int j = 0; j < jclient->ow.device_desc->inputs; j++)
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
  jclient_print_latencies (jclient, "\n");
  overwitch_set_status (&jclient->ow, OW_STATUS_STOP);
}

int
jclient_run (struct jclient *jclient)
{
  jack_options_t options = JackNoStartServer;
  jack_status_t status;
  overwitch_err_t ob_status;
  char *client_name;

  jclient->samplerate = 0;
  jclient->bufsize = 0;
  jclient->xruns = 0;

  jclient->status = JC_STATUS_READY;
  ob_status =
    overwitch_init (&jclient->ow, jclient->bus, jclient->address,
		    jclient->blocks_per_transfer);
  if (ob_status)
    {
      error_print ("USB error: %s\n", overbrigde_get_err_str (ob_status));
      goto end;
    }

  jclient->ow.sample_counter_data = &jclient->o2j_dll.counter;
  jclient->ow.sample_counter_init = dll_counter_init;
  jclient->ow.sample_counter_inc = dll_counter_inc;

  jclient->ow.buffer_write_space = jclient_buffer_write_space;
  jclient->ow.buffer_write = jclient_buffer_write;
  jclient->ow.buffer_read_space = jclient_buffer_read_space;
  jclient->ow.buffer_read = jclient_buffer_read;

  jclient->ow.get_time = jack_get_time;

  jclient->client =
    jack_client_open (jclient->ow.device_desc->name, options, &status, NULL);
  if (jclient->client == NULL)
    {
      error_print ("jack_client_open() failed, status = 0x%2.0x\n", status);

      if (status & JackServerFailed)
	{
	  error_print ("Unable to connect to JACK server\n");
	}

      goto cleanup_overwitch;
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

  pthread_spin_init (&jclient->lock, PTHREAD_PROCESS_SHARED);
  if (jack_set_xrun_callback
      (jclient->client, jclient_thread_xrun_cb, jclient))
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

  jclient->output_ports =
    malloc (sizeof (jack_port_t *) * jclient->ow.device_desc->outputs);
  for (int i = 0; i < jclient->ow.device_desc->outputs; i++)
    {
      jclient->output_ports[i] =
	jack_port_register (jclient->client,
			    jclient->ow.device_desc->output_track_names[i],
			    JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

      if (jclient->output_ports[i] == NULL)
	{
	  error_print (MSG_ERROR_PORT_REGISTER);
	  goto cleanup_jack;
	}
    }

  jclient->input_ports =
    malloc (sizeof (jack_port_t *) * jclient->ow.device_desc->inputs);
  for (int i = 0; i < jclient->ow.device_desc->inputs; i++)
    {
      jclient->input_ports[i] =
	jack_port_register (jclient->client,
			    jclient->ow.device_desc->input_track_names[i],
			    JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

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

  //Resamplers

  jclient->j2o_state =
    src_callback_new (jclient_j2o_reader, jclient->quality,
		      jclient->ow.device_desc->inputs, NULL, jclient);
  jclient->o2j_state =
    src_callback_new (jclient_o2j_reader, jclient->quality,
		      jclient->ow.device_desc->outputs, NULL, jclient);

  //Ring buffers

  jclient->o2j_audio_rb =
    jack_ringbuffer_create (MAX_LATENCY * jclient->ow.o2j_frame_size);
  jack_ringbuffer_mlock (jclient->o2j_audio_rb);
  jclient->ow.o2j_audio_buf = jclient->o2j_audio_rb;

  jclient->j2o_audio_rb =
    jack_ringbuffer_create (MAX_LATENCY * jclient->ow.j2o_frame_size);
  jack_ringbuffer_mlock (jclient->j2o_audio_rb);
  jclient->ow.j2o_audio_buf = jclient->j2o_audio_rb;

  jclient->j2o_midi_rb = jack_ringbuffer_create (MIDI_BUF_SIZE * 8);
  jack_ringbuffer_mlock (jclient->j2o_midi_rb);
  jclient->ow.j2o_midi_buf = jclient->j2o_midi_rb;

  jclient->o2j_midi_rb = jack_ringbuffer_create (MIDI_BUF_SIZE * 8);
  jack_ringbuffer_mlock (jclient->o2j_midi_rb);
  jclient->ow.o2j_midi_buf = jclient->o2j_midi_rb;

  if (overwitch_activate (&jclient->ow))
    {
      goto cleanup_jack;
    }

  set_rt_priority (&jclient->ow.j2o_midi_thread, jclient->priority);
  set_rt_priority (&jclient->ow.audio_o2j_midi_thread, jclient->priority);

  jclient_set_sample_rate_cb (jack_get_sample_rate (jclient->client),
			      jclient);
  jclient_set_buffer_size_cb (jack_get_buffer_size (jclient->client),
			      jclient);

  if (jack_activate (jclient->client))
    {
      error_print ("Cannot activate client\n");
      goto cleanup_jack;
    }

  overwitch_wait (&jclient->ow);

  debug_print (1, "Exiting...\n");
  jack_deactivate (jclient->client);

cleanup_jack:
  jack_ringbuffer_free (jclient->j2o_audio_rb);
  jack_ringbuffer_free (jclient->o2j_audio_rb);
  jack_ringbuffer_free (jclient->j2o_midi_rb);
  jack_ringbuffer_free (jclient->o2j_midi_rb);
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
  pthread_spin_destroy (&jclient->lock);
cleanup_overwitch:
  overwitch_destroy (&jclient->ow);

end:
  return jclient->status;
}

void *
jclient_run_thread (void *data)
{
  struct jclient *jclient = data;
  jclient_run (jclient);
  return NULL;
}
