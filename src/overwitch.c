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
#include <limits.h>
#include <arpa/inet.h>
#include <math.h>
#include <jack/intclient.h>
#include <jack/thread.h>
#include <libgen.h>

#include "overbridge.h"

#define JACK_MAX_BUF_SIZE 128

struct overbridge ob;
jack_client_t *client;
jack_port_t **output_ports;
jack_port_t **input_ports;
jack_nframes_t bufsize = 0;
jack_nframes_t samplerate = 0;
struct jt_cb_counter counter;
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
int calibration;

static inline void
overwitch_print_status (int force)
{
  size_t rso2j;
  size_t wsj2o;
  size_t wso2j;
  size_t rsj2o;

  if (debug_level || force)
    {
      rso2j = jack_ringbuffer_read_space (ob.o2j_rb);
      wso2j = jack_ringbuffer_write_space (ob.o2j_rb);
      rsj2o = jack_ringbuffer_read_space (ob.j2o_rb);
      wsj2o = jack_ringbuffer_write_space (ob.j2o_rb);
      debug_print (1,
		   "rbo2j %5ld, %5ld, %5ld; rbj2o %4ld, %4ld, %4ld; OB @ %.2f Hz; JACK @ %.2f Hz; ratios: %f, %f\n",
		   rso2j, wso2j, o2j_latency, rsj2o, wsj2o, j2o_latency, obsr,
		   jsr, o2j_ratio, j2o_ratio);
    }
}

int
overwitch_sample_rate_cb (jack_nframes_t nframes, void *arg)
{
  if (samplerate)
    {
      return 1;
    }

  samplerate = nframes;
  debug_print (0, "JACK sample rate: %d\n", samplerate);
  return 0;
}

int
overwitch_buffer_size_cb (jack_nframes_t nframes, void *arg)
{
  if (bufsize)
    {
      return 1;
    }

  if (nframes > OB_FRAMES_PER_TRANSFER)
    {
      error_print
	("JACK buffer size is greater than device buffer size (%d > %d)\n",
	 nframes, OB_FRAMES_PER_TRANSFER);
      overbridge_set_status (&ob, OB_STATUS_STOP);
      return 1;
    }

  bufsize = nframes;
  debug_print (0, "JACK buffer size: %d\n", bufsize);

  if (!counter.jack_get_time)
    {
      jt_cb_counter_init_jack (&counter, client, OB_COUNTER_FRAMES, bufsize);
    }

  o2j_buf_size = bufsize * ob.o2j_frame_bytes;
  j2o_buf_size = bufsize * ob.j2o_frame_bytes;

  return 0;
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

      if (rso2j >= o2j_buf_size)
	{
	  frames = bufsize;
	  bytes = o2j_buf_size;
	  jack_ringbuffer_read (ob.o2j_rb, (void *) o2j_buf_in, bytes);
	}
      else if (rso2j >= ob.o2j_frame_bytes)
	{
	  frames = (rso2j / ob.o2j_frame_bytes);
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
	  frames = 1;
	}
    }
  else
    {
      if (rso2j >= o2j_buf_size)
	{
	  jack_ringbuffer_read (ob.o2j_rb, (void *) o2j_buf_in, o2j_buf_size);
	  frames = bufsize;
	  running = 1;
	}
      else
	{
	  frames = 1;
	}
    }

  last_frames = frames;
  return frames;
}

static inline void
overwitch_o2j ()
{
  long gen_frames;
  static int ready = 1;

  if (ready)
    {
      overbridge_set_o2j_reading (&ob, 1);
      ready = 0;
    }

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
  size_t rsj2o;
  static int waiting = 1;
  static double j2o_acc = .0;

  if (waiting && !overbridge_is_j2o_reading (&ob))
    {
      return;
    }

  waiting = 0;

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
	("j2o: Unexpected frames with ratio %.16f (output %ld, expected %d)\n",
	 j2o_ratio, gen_frames, frames);
    }

  bytes = gen_frames * ob.j2o_frame_bytes;
  rsj2o = jack_ringbuffer_read_space (ob.j2o_rb);
  if (rsj2o < ob.j2o_buf_size * 2 - bytes)
    {
      jack_ringbuffer_write (ob.j2o_rb, (void *) j2o_buf_out, bytes);
    }
  else
    {
      error_print ("j2o: Skipping writing at %ld...\n", rsj2o);
    }
}

static inline int
overwitch_run_cb (jack_nframes_t nframes)
{
  jack_default_audio_sample_t *buffer[OB_MAX_TRACKS];
  int check;
  float *f;
  static int not_running = 1;

  if (not_running && overbridge_get_status (&ob) != OB_STATUS_RUN)
    {
      overbridge_set_status (&ob, OB_STATUS_READY);
      not_running = 0;
    }

  pthread_spin_lock (&ob.lock);
  check = jt_cb_counter_inc (&counter);
  jsr = counter.estimated_sr;
  if (check)
    {
      obsr = jt_cb_counter_restart (&ob.counter);
      j2o_latency = ob.j2o_latency;

      o2j_ratio = jsr / obsr;
      j2o_ratio = obsr / jsr;

      overwitch_print_status (0);
    }
  pthread_spin_unlock (&ob.lock);

  //o2j

  overwitch_o2j ();

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

  return 0;
}

static inline int
overwitch_cal_cb ()
{
  int check;
  static int skip = 1;
  static int waiting = 1;

  if (waiting && overbridge_get_status (&ob) == OB_STATUS_WAIT)
    {
      overbridge_set_status (&ob, OB_STATUS_CAL);
      waiting = 0;
    }

  pthread_spin_lock (&ob.lock);
  check = jt_cb_counter_inc (&counter);
  jsr = counter.estimated_sr;
  if (check)
    {
      obsr = jt_cb_counter_restart (&ob.counter);
      j2o_latency = ob.j2o_latency;
      if (skip)
	{
	  o2j_ratio = jsr / obsr;
	  j2o_ratio = obsr / jsr;

	  skip--;
	}
    }
  pthread_spin_unlock (&ob.lock);

  if (skip == 0)
    {
      debug_print (0, "Calibration value: %f\n", o2j_ratio);
    }

  return skip;
}

static int
overwitch_process_cb (jack_nframes_t nframes, void *arg)
{
  if (calibration)
    {
      calibration = overwitch_cal_cb ();
    }
  else
    {
      overwitch_run_cb (nframes);
    }

  return 0;
}

static void
overwitch_exit (int signo)
{
  debug_print (0, "Maximum measured buffer latencies: %.1f ms, %.1f ms\n",
	       o2j_latency * 1000.0 / (ob.o2j_frame_bytes * OB_SAMPLE_RATE),
	       j2o_latency * 1000.0 / (ob.j2o_frame_bytes * OB_SAMPLE_RATE));

  overbridge_set_status (&ob, OB_STATUS_STOP);
}

static int
overwitch_run ()
{
  jack_options_t options = JackNullOption;
  jack_status_t status;
  int ob_status;
  char *client_name;
  struct sigaction action;
  size_t o2j_buf_max_size;
  size_t j2o_buf_max_size;
  int ret = 0;

  action.sa_handler = overwitch_exit;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction (SIGHUP, &action, NULL);
  sigaction (SIGINT, &action, NULL);
  sigaction (SIGTERM, &action, NULL);

  ob_status = overbridge_init (&ob);
  if (ob_status)
    {
      error_print ("Device error: %s\n", overbrigde_get_err_str (ob_status));
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
      debug_print (0, "JACK server started\n");
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

  if (jack_set_buffer_size_callback (client, overwitch_buffer_size_cb, NULL))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  if (jack_set_sample_rate_callback (client, overwitch_sample_rate_cb, NULL))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  output_ports = malloc (sizeof (jack_port_t *) * ob.device_desc.outputs);
  for (int i = 0; i < ob.device_desc.outputs; i++)
    {
      output_ports[i] =
	jack_port_register (client,
			    ob.device_desc.output_track_names[i],
			    JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

      if (output_ports[i] == NULL)
	{
	  error_print ("No more JACK ports available\n");
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
	  error_print ("No more JACK ports available\n");
	  ret = EXIT_FAILURE;
	  goto cleanup_jack;
	}
    }

  j2o_state =
    src_callback_new (overwitch_j2o_reader, SRC_SINC_FASTEST,
		      ob.device_desc.inputs, NULL, NULL);
  o2j_state =
    src_callback_new (overwitch_o2j_reader, SRC_SINC_FASTEST,
		      ob.device_desc.outputs, NULL, NULL);

  j2o_buf_max_size =
    JACK_MAX_BUF_SIZE * OB_BYTES_PER_FRAME * ob.device_desc.inputs;
  j2o_buf_in = malloc (j2o_buf_max_size);
  j2o_buf_out = malloc (j2o_buf_max_size * 4.5);	//Up to 192 kHz and a few samples more
  j2o_aux = malloc (j2o_buf_max_size);
  j2o_queue = malloc (j2o_buf_max_size * 4.5);	//Up to 192 kHz and a few samples more
  j2o_queue_len = 0;

  o2j_buf_max_size =
    JACK_MAX_BUF_SIZE * OB_BYTES_PER_FRAME * ob.device_desc.outputs;
  o2j_buf_in = malloc (o2j_buf_max_size);
  o2j_buf_out = malloc (o2j_buf_max_size);

  memset (j2o_buf_in, 0, j2o_buf_max_size);
  memset (o2j_buf_in, 0, o2j_buf_max_size);

  if (overbridge_run (&ob, client))
    {
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  //Device USB is synchronized to JACK client so we ensure that JACK is started after.
  sleep (1);

  if (jack_activate (client))
    {
      error_print ("Cannot activate client\n");
      ret = EXIT_FAILURE;
      goto cleanup_jack;
    }

  overbridge_wait (&ob);

  debug_print (0, "Exiting...\n");
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

  fprintf (stderr, "%s\n", PACKAGE_STRING);
  exec_name = basename (executable_path);
  fprintf (stderr, "Usage: %s [-r ratio] [-v]\n", exec_name);
}

int
main (int argc, char *argv[])
{
  int c;
  int vflg = 0, rflg = 0, errflg = 0;
  char *ratio;

  while ((c = getopt (argc, argv, "hvr:")) != -1)
    {
      switch (c)
	{
	case 'h':
	  overwitch_help (argv[0]);
	  exit (EXIT_SUCCESS);
	case 'v':
	  vflg++;
	  break;
	case 'r':
	  rflg++;
	  ratio = optarg;
	  break;
	case '?':
	  errflg++;
	}
    }

  if (vflg)
    {
      debug_level = vflg;
    }

  if (rflg)
    {
      calibration = 0;
      o2j_ratio = atof (ratio);
      j2o_ratio = 1.0 / o2j_ratio;
    }
  else
    {
      calibration = 1;
    }

  if (errflg > 0)
    {
      overwitch_help (argv[0]);
      exit (EXIT_FAILURE);
    }

  return overwitch_run ();
}
