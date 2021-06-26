/*
 *   overwitch.c
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

#include "../config.h"
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <math.h>
#include <jack/intclient.h>
#include <jack/thread.h>
#include <jack/midiport.h>
#include <libgen.h>
#define _GNU_SOURCE
#include <getopt.h>

#include "overbridge.h"

#define MAX_READ_FRAMES 5
#define STARTUP_TIME 5
#define LOG_TIME 2
#define RATIO_DIFF_THRES 0.00001
#define DEFAULT_QUALITY 2
#define DEFAULT_BLOCKS 24

#define MSG_ERROR_PORT_REGISTER "Error while registering JACK port\n"

struct overbridge ob;
jack_client_t *client;
jack_port_t **output_ports;
jack_port_t **input_ports;
jack_port_t *midi_output_port;
jack_port_t *midi_input_port;
jack_nframes_t bufsize = 0;
double samplerate;
size_t o2j_buf_size;
size_t j2o_buf_size;
jack_default_audio_sample_t *j2o_buf_in;
jack_default_audio_sample_t *j2o_buf_out;
jack_default_audio_sample_t *j2o_aux;
jack_default_audio_sample_t *j2o_queue;
size_t j2o_queue_len;
jack_default_audio_sample_t *o2j_buf_in;
jack_default_audio_sample_t *o2j_buf_out;
SRC_STATE *j2o_state;
SRC_DATA j2o_data;
SRC_STATE *o2j_state;
SRC_DATA o2j_data;
size_t j2o_latency;
size_t o2j_latency;
int cycles_to_skip;
double jsr;
double obsr;
double j2o_ratio;
double o2j_ratio;
jack_nframes_t kj;
double _w0;
double _w1;
double _w2;
int kdel;
double _z1 = 0.0;
double _z2 = 0.0;
double _z3 = 0.0;
double o2j_ratio_max;
double o2j_ratio_min;
int read_frames;
jack_nframes_t current_frames;

int log_control_cycles;
int quality = DEFAULT_QUALITY;

static struct option options[] = {
  {"use-device", 1, NULL, 'd'},
  {"resampling-quality", 1, NULL, 'q'},
  {"transfer-blocks", 1, NULL, 'b'},
  {"list-devices", 0, NULL, 'l'},
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
};

void
overwitch_set_loop_filter (double bw)
{
  //Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
  double w = 2.0 * M_PI * 20 * bw * bufsize / samplerate;
  _w0 = 1.0 - exp (-w);
  w = 2.0 * M_PI * bw * o2j_ratio / samplerate;
  _w1 = w * 1.6;
  _w2 = w * bufsize / 1.6;
}

static void
overwitch_init_sample_rate ()
{
  o2j_ratio = samplerate / OB_SAMPLE_RATE;
  j2o_ratio = 1.0 / o2j_ratio;
  o2j_ratio_max = 1.05 * o2j_ratio;
  o2j_ratio_min = 0.95 * o2j_ratio;
}

static void
overwitch_init_buffer_size ()
{
  kj = bufsize / -o2j_ratio;
  read_frames = bufsize * j2o_ratio;

  kdel = (OB_FRAMES_PER_BLOCK * ob.blocks_per_transfer) + 1.5 * bufsize;
  debug_print (2, "Target delay: %.1f ms (%d frames)\n",
	       kdel * 1000 / OB_SAMPLE_RATE, kdel);

  log_control_cycles = STARTUP_TIME * samplerate / bufsize;

  o2j_buf_size = bufsize * ob.o2j_frame_bytes;
  j2o_buf_size = bufsize * ob.j2o_frame_bytes;
}

static int
overwitch_thread_xrun_cb (void *arg)
{
  error_print ("JACK xrun\n");
  return 0;
}

static long
overwitch_j2o_reader (void *cb_data, float **data)
{
  long ret;

  *data = j2o_buf_in;

  if (j2o_queue_len == 0)
    {
      debug_print (2, "j2o: Can not read data from queue\n");
      return bufsize;
    }

  ret = j2o_queue_len;
  memcpy (j2o_buf_in, j2o_queue, j2o_queue_len * ob.j2o_frame_bytes);
  j2o_queue_len = 0;

  return ret;
}

static long
overwitch_o2j_reader (void *cb_data, float **data)
{
  size_t rso2j;
  size_t bytes;
  long frames;
  static int last_frames = 1;
  static int running = 0;

  *data = o2j_buf_in;

  rso2j = jack_ringbuffer_read_space (ob.o2j_rb);
  if (running)
    {
      if (o2j_latency < rso2j)
	{
	  o2j_latency = rso2j;
	}

      if (rso2j >= ob.o2j_frame_bytes)
	{
	  frames = rso2j / ob.o2j_frame_bytes;
	  frames = frames > MAX_READ_FRAMES ? MAX_READ_FRAMES : frames;
	  bytes = frames * ob.o2j_frame_bytes;
	  jack_ringbuffer_read (ob.o2j_rb, (void *) o2j_buf_in, bytes);
	}
      else
	{
	  debug_print (2,
		       "o2j: Can not read data from ring buffer. Replicating last sample...\n");
	  if (last_frames > 1)
	    {
	      memcpy (o2j_buf_in,
		      &o2j_buf_in[(last_frames - 1) * ob.device_desc.outputs],
		      ob.o2j_frame_bytes);
	    }
	  frames = MAX_READ_FRAMES;
	}
    }
  else
    {
      if (rso2j >= o2j_buf_size)
	{
	  frames = rso2j / ob.o2j_frame_bytes;
	  bytes = frames * ob.o2j_frame_bytes;
	  jack_ringbuffer_read_advance (ob.o2j_rb, bytes);
	  frames = MAX_READ_FRAMES;
	  running = 1;
	}
      else
	{
	  frames = MAX_READ_FRAMES;
	}
    }

  read_frames += frames;
  last_frames = frames;
  return frames;
}

static inline void
overwitch_o2j ()
{
  long gen_frames;

  read_frames = 0;
  gen_frames = src_callback_read (o2j_state, o2j_ratio, bufsize, o2j_buf_out);
  if (gen_frames != bufsize)
    {
      error_print
	("o2j: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	 o2j_ratio, gen_frames, bufsize);
    }
}

static inline void
overwitch_j2o ()
{
  long gen_frames;
  int inc;
  int frames;
  size_t bytes;
  size_t wsj2o;
  overbridge_status_t status;
  static double j2o_acc = .0;

  pthread_spin_lock (&ob.lock);
  status = ob.status;
  pthread_spin_unlock (&ob.lock);

  memcpy (&j2o_queue[j2o_queue_len], j2o_aux, j2o_buf_size);
  j2o_queue_len += bufsize;

  j2o_acc += bufsize * (j2o_ratio - 1.0);
  inc = trunc (j2o_acc);
  j2o_acc -= inc;
  frames = bufsize + inc;

  gen_frames = src_callback_read (j2o_state, j2o_ratio, frames, j2o_buf_out);
  if (gen_frames != frames)
    {
      error_print
	("j2o: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	 j2o_ratio, gen_frames, frames);
    }

  if (status < OB_STATUS_RUN)
    {
      return;
    }

  bytes = gen_frames * ob.j2o_frame_bytes;
  wsj2o = jack_ringbuffer_write_space (ob.j2o_rb);
  if (bytes <= wsj2o)
    {
      jack_ringbuffer_write (ob.j2o_rb, (void *) j2o_buf_out, bytes);
    }
  else
    {
      error_print ("j2o: Buffer overflow. Discarding data...\n");
    }
}

static inline void
overwitch_compute_ratios ()
{
  jack_time_t current_usecs;
  jack_time_t next_usecs;
  float period_usecs;
  jack_nframes_t ko0;
  jack_nframes_t ko1;
  double to0;
  double to1;
  double tj;
  overbridge_status_t status;
  static int i = 0;
  static double sum_o2j_ratio = 0.0;
  static double sum_j2o_ratio = 0.0;
  static double o2j_ratio_avg = 0.0;
  static double j2o_ratio_avg = 0.0;
  static double last_o2j_ratio_avg = 0.0;

  if (jack_get_cycle_times (client,
			    &current_frames,
			    &current_usecs, &next_usecs, &period_usecs))
    {
      error_print ("Error while getting JACK time\n");
    }

  pthread_spin_lock (&ob.lock);
  j2o_latency = ob.j2o_latency;
  ko0 = ob.i0.frames;
  to0 = ob.i0.time;
  ko1 = ob.i1.frames;
  to1 = ob.i1.time;
  status = ob.status;
  pthread_spin_unlock (&ob.lock);

  if (status == OB_STATUS_BOOT)
    {
      pthread_spin_lock (&ob.lock);
      ob.status = OB_STATUS_SKIP;
      pthread_spin_unlock (&ob.lock);

      return;
    }

  //The whole calculation of the delay and the loop filter is taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
  kj += read_frames;
  tj = current_usecs * 1.0e-6;

  jack_nframes_t frames = ko1 - ko0;
  double dob = frames * (tj - to0) / (to1 - to0);
  frames = ko0 - kj;
  double err = frames + dob - kdel;

  if (status == OB_STATUS_SKIP)
    {
      //Taken from https://github.com/jackaudio/tools/blob/master/zalsa/jackclient.cc.
      int n = (int) (floor (err + 0.5));
      kj += n;
      err -= n;

      debug_print (2, "Starting up...\n");

      overwitch_set_loop_filter (1.0);

      pthread_spin_lock (&ob.lock);
      ob.status = OB_STATUS_STARTUP;
      pthread_spin_unlock (&ob.lock);
    }

  _z1 += _w0 * (_w1 * err - _z1);
  _z2 += _w0 * (_z1 - _z2);
  _z3 += _w2 * _z2;
  o2j_ratio = 1.0 - _z2 - _z3;
  if (o2j_ratio > o2j_ratio_max)
    {
      o2j_ratio = o2j_ratio_max;
    }
  if (o2j_ratio < o2j_ratio_min)
    {
      o2j_ratio = o2j_ratio_min;
    }
  j2o_ratio = 1.0 / o2j_ratio;

  i++;
  sum_o2j_ratio += o2j_ratio;
  sum_j2o_ratio += j2o_ratio;
  if (i == log_control_cycles)
    {
      last_o2j_ratio_avg = o2j_ratio_avg;

      o2j_ratio_avg = sum_o2j_ratio / log_control_cycles;
      j2o_ratio_avg = sum_j2o_ratio / log_control_cycles;

      debug_print (1,
		   "Max. latencies (ms): %.1f, %.1f; avg. ratios: %f, %f; curr. ratios: %f, %f\n",
		   o2j_latency * 1000.0 / (ob.o2j_frame_bytes *
					   OB_SAMPLE_RATE),
		   j2o_latency * 1000.0 / (ob.j2o_frame_bytes *
					   OB_SAMPLE_RATE),
		   o2j_ratio_avg, j2o_ratio_avg, o2j_ratio, j2o_ratio);

      i = 0;
      sum_o2j_ratio = 0.0;
      sum_j2o_ratio = 0.0;

      if (status == OB_STATUS_STARTUP)
	{
	  debug_print (2, "Tunning...\n");
	  overwitch_set_loop_filter (0.05);

	  pthread_spin_lock (&ob.lock);
	  ob.status = OB_STATUS_TUNE;
	  pthread_spin_unlock (&ob.lock);

	  log_control_cycles = LOG_TIME * samplerate / bufsize;
	}

      if (status == OB_STATUS_TUNE
	  && fabs (o2j_ratio_avg - last_o2j_ratio_avg) < RATIO_DIFF_THRES)
	{
	  debug_print (2, "Running...\n");
	  overwitch_set_loop_filter (0.02);

	  pthread_spin_lock (&ob.lock);
	  ob.status = OB_STATUS_RUN;
	  pthread_spin_unlock (&ob.lock);
	}
    }
}

static inline void
overwitch_o2j_midi (jack_nframes_t nframes)
{
  size_t data_size;
  void *midi_port_buf;
  jack_midi_data_t *jmidi;
  struct ob_midi_event event;
  jack_nframes_t last_frames;
  jack_nframes_t frames;

  midi_port_buf = jack_port_get_buffer (midi_output_port, nframes);
  jack_midi_clear_buffer (midi_port_buf);
  last_frames = 0;

  while (jack_ringbuffer_read_space (ob.o2j_rb_midi) >=
	 sizeof (struct ob_midi_event))
    {
      jack_ringbuffer_peek (ob.o2j_rb_midi, (void *) &event,
			    sizeof (struct ob_midi_event));

      frames = (current_frames - event.frames) % nframes;

      debug_print (2, "Event frames: %u\n", frames);

      if (frames < last_frames)
	{
	  debug_print (2, "Skipping until the next cycle...\n");
	  last_frames = 0;
	  break;
	}
      last_frames = frames;

      jack_ringbuffer_read_advance (ob.o2j_rb_midi,
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
overwitch_j2o_midi (jack_nframes_t nframes)
{
  jack_midi_event_t jevent;
  void *midi_port_buf;
  struct ob_midi_event oevent;
  jack_nframes_t event_count;
  jack_midi_data_t status_byte;
  overbridge_status_t status;

  pthread_spin_lock (&ob.lock);
  status = ob.status;
  pthread_spin_unlock (&ob.lock);

  if (status < OB_STATUS_RUN)
    {
      return;
    }

  midi_port_buf = jack_port_get_buffer (midi_input_port, nframes);
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
	  if (jack_ringbuffer_write_space (ob.j2o_rb_midi) >=
	      sizeof (struct ob_midi_event))
	    {
	      jack_ringbuffer_write (ob.j2o_rb_midi, (void *) &oevent,
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
overwitch_process_cb (jack_nframes_t nframes, void *arg)
{
  jack_default_audio_sample_t *buffer[OB_MAX_TRACKS];
  float *f;

  overwitch_compute_ratios ();

  overwitch_o2j ();

  //o2j

  f = o2j_buf_out;
  for (int i = 0; i < ob.device_desc.outputs; i++)
    {
      buffer[i] = jack_port_get_buffer (output_ports[i], nframes);
    }
  for (int i = 0; i < nframes; i++)
    {
      for (int j = 0; j < ob.device_desc.outputs; j++)
	{
	  buffer[j][i] = *f;
	  f++;
	}
    }

  //j2o

  f = j2o_aux;
  for (int i = 0; i < ob.device_desc.inputs; i++)
    {
      buffer[i] = jack_port_get_buffer (input_ports[i], nframes);
    }
  for (int i = 0; i < nframes; i++)
    {
      for (int j = 0; j < ob.device_desc.inputs; j++)
	{
	  *f = buffer[j][i];
	  f++;
	}
    }

  overwitch_j2o ();

  overwitch_o2j_midi (nframes);

  overwitch_j2o_midi (nframes);

  return 0;
}

static void
overwitch_exit (int signo)
{
  printf ("Max. latencies (ms): %.1f, %.1f\n",
	  o2j_latency * 1000.0 / (ob.o2j_frame_bytes * OB_SAMPLE_RATE),
	  j2o_latency * 1000.0 / (ob.j2o_frame_bytes * OB_SAMPLE_RATE));

  overbridge_set_status (&ob, OB_STATUS_STOP);
}

static int
overwitch_run (char *device_name, int blocks_per_transfer)
{
  jack_options_t options = JackNullOption;
  jack_status_t status;
  overbridge_err_t ob_status;
  char *client_name;
  struct sigaction action;
  size_t o2j_bufsize;
  size_t j2o_bufsize;
  int ret = 0;

  action.sa_handler = overwitch_exit;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);

  ob_status = overbridge_init (&ob, device_name, blocks_per_transfer);
  if (ob_status)
    {
      error_print ("USB error: %s\n", overbrigde_get_err_str (ob_status));
      exit (EXIT_FAILURE);
    }

  client = jack_client_open (ob.device_desc.name, options, &status, NULL);
  if (client == NULL)
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
      client_name = jack_get_client_name (client);
      debug_print (0, "Name client in use. Using %s...\n", client_name);
    }

  if (jack_set_process_callback (client, overwitch_process_cb, NULL))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  if (jack_set_xrun_callback (client, overwitch_thread_xrun_cb, NULL))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  samplerate = jack_get_sample_rate (client);
  printf ("JACK sample rate: %.0f\n", samplerate);
  overwitch_init_sample_rate ();

  bufsize = jack_get_buffer_size (client);
  printf ("JACK buffer size: %d\n", bufsize);
  overwitch_init_buffer_size ();

  output_ports = malloc (sizeof (jack_port_t *) * ob.device_desc.outputs);
  for (int i = 0; i < ob.device_desc.outputs; i++)
    {
      output_ports[i] =
	jack_port_register (client,
			    ob.device_desc.output_track_names[i],
			    JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

      if (output_ports[i] == NULL)
	{
	  error_print (MSG_ERROR_PORT_REGISTER);
	  ret = EXIT_FAILURE;
	  goto cleanup_jack;
	}
    }

  input_ports = malloc (sizeof (jack_port_t *) * ob.device_desc.inputs);
  for (int i = 0; i < ob.device_desc.inputs; i++)
    {
      input_ports[i] =
	jack_port_register (client,
			    ob.device_desc.input_track_names[i],
			    JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

      if (input_ports[i] == NULL)
	{
	  error_print (MSG_ERROR_PORT_REGISTER);
	  ret = EXIT_FAILURE;
	  goto cleanup_jack;
	}
    }

  midi_output_port =
    jack_port_register (client,
			"MIDI out",
			JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

  if (midi_output_port == NULL)
    {
      error_print (MSG_ERROR_PORT_REGISTER);
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  midi_input_port =
    jack_port_register (client,
			"MIDI in",
			JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

  if (midi_input_port == NULL)
    {
      error_print (MSG_ERROR_PORT_REGISTER);
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  j2o_state =
    src_callback_new (overwitch_j2o_reader, quality,
		      ob.device_desc.inputs, NULL, NULL);
  o2j_state =
    src_callback_new (overwitch_o2j_reader, quality,
		      ob.device_desc.outputs, NULL, NULL);

  j2o_bufsize = bufsize * OB_BYTES_PER_FRAME * ob.device_desc.inputs;
  j2o_buf_in = malloc (j2o_bufsize);
  j2o_buf_out = malloc (j2o_bufsize * 4.5);	//Up to 192 kHz and a few samples more
  j2o_aux = malloc (j2o_bufsize);
  j2o_queue = malloc (j2o_bufsize * 4.5);	//Up to 192 kHz and a few samples more
  j2o_queue_len = 0;

  o2j_bufsize = bufsize * OB_BYTES_PER_FRAME * ob.device_desc.outputs;
  o2j_buf_in = malloc (o2j_bufsize);
  o2j_buf_out = malloc (o2j_bufsize);

  memset (j2o_buf_in, 0, j2o_bufsize);
  memset (o2j_buf_in, 0, o2j_bufsize);

  if (overbridge_run (&ob, client))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  if (jack_activate (client))
    {
      error_print ("Cannot activate client\n");
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  overbridge_wait (&ob);

  debug_print (1, "Exiting...\n");
  jack_deactivate (client);

cleanup_jack:
  jack_client_close (client);
  src_delete (j2o_state);
  src_delete (o2j_state);
  free (output_ports);
  free (input_ports);
  free (j2o_buf_in);
  free (j2o_buf_out);
  free (j2o_aux);
  free (j2o_queue);
  free (o2j_buf_in);
  free (o2j_buf_out);
cleanup_overbridge:
  overbridge_destroy (&ob);

  return ret;
}

void
overwitch_help (char *executable_path)
{
  char *exec_name;
  struct option *option;

  fprintf (stderr, "%s\n", PACKAGE_STRING);
  exec_name = basename (executable_path);
  fprintf (stderr, "Usage: %s [options]\n", exec_name);
  fprintf (stderr, "Options:\n");
  option = options;
  while (option->name)
    {
      fprintf (stderr, "  --%s, -%c", option->name, option->val);
      if (option->has_arg)
	{
	  fprintf (stderr, " value");
	}
      fprintf (stderr, "\n");
      option++;
    }
}

int
main (int argc, char *argv[])
{
  int opt;
  int vflg = 0, lflg = 0, dflg = 0, bflg = 0, errflg = 0;
  char *endstr;
  char *device = NULL;
  int long_index = 0;
  overbridge_err_t ob_status;
  int blocks_per_transfer = DEFAULT_BLOCKS;

  while ((opt = getopt_long (argc, argv, "d:q:b:lvh",
			     options, &long_index)) != -1)
    {
      switch (opt)
	{
	case 'd':
	  device = optarg;
	  dflg++;
	  break;
	case 'q':
	  quality = (int) strtol (optarg, &endstr, 10);
	  if (errno || endstr == optarg || *endstr != '\0' || quality > 4
	      || quality < 0)
	    {
	      quality = DEFAULT_QUALITY;
	      fprintf (stderr,
		       "Resampling quality value must be in [0..4]. Using value %d...\n",
		       quality);
	    }
	  break;
	case 'b':
	  blocks_per_transfer = (int) strtol (optarg, &endstr, 10);
	  if (blocks_per_transfer < 2 || blocks_per_transfer > 32)
	    {
	      blocks_per_transfer = DEFAULT_BLOCKS;
	      fprintf (stderr,
		       "Blocks value must be in [8..24]. Using value %d...\n",
		       blocks_per_transfer);
	    }
	  bflg++;
	  break;
	case 'l':
	  lflg++;
	  break;
	case 'v':
	  vflg++;
	  break;
	case 'h':
	  overwitch_help (argv[0]);
	  exit (EXIT_SUCCESS);
	case '?':
	  errflg++;
	}
    }

  if (errflg > 0)
    {
      overwitch_help (argv[0]);
      exit (EXIT_FAILURE);
    }

  if (vflg)
    {
      debug_level = vflg;
    }

  if (lflg)
    {
      ob_status = overbridge_list_devices ();
      if (ob_status)
	{
	  fprintf (stderr, "USB error: %s\n",
		   overbrigde_get_err_str (ob_status));
	  exit (EXIT_FAILURE);
	}
      exit (EXIT_SUCCESS);
    }

  if (dflg != 1)
    {
      fprintf (stderr, "No device provided\n");
      exit (EXIT_FAILURE);
    }

  if (bflg > 1)
    {
      fprintf (stderr, "Undetermined blocks\n");
      exit (EXIT_FAILURE);
    }

  return overwitch_run (device, blocks_per_transfer);
}
