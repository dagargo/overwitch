/*
 *   jclient.h
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

#include <jack/ringbuffer.h>
#include <jack/midiport.h>
#include "overwitch.h"
#include "dll.h"

typedef enum
{
  RES_STATUS_ERROR = -1,
  RES_STATUS_STOP,
  RES_STATUS_READY,
  RES_STATUS_BOOT,
  RES_STATUS_TUNE,
  RES_STATUS_RUN
} resampler_status_t;

struct resampler
{
  resampler_status_t status;
  struct overwitch ow;
  struct dll dll;		//The DLL is based on o2j data
  double o2p_ratio;
  double p2o_ratio;
  SRC_STATE *p2o_state;
  SRC_STATE *o2p_state;
  size_t p2o_latency;
  size_t o2p_latency;
  size_t p2o_max_latency;
  size_t o2p_max_latency;
  float *p2o_buf_in;
  float *p2o_buf_out;
  float *p2o_aux;
  float *p2o_queue;
  float *o2p_buf_in;
  float *o2p_buf_out;
  size_t p2o_queue_len;
  int log_control_cycles;
  int log_cycles;
  int xruns;
  pthread_spinlock_t lock;	//Used to synchronize access to xruns.
  int reading_at_o2p_end;
  size_t o2p_buf_size;
  size_t p2o_buf_size;
};

struct jclient
{
  jack_client_t *client;
  jack_ringbuffer_t *o2p_audio_rb;
  jack_ringbuffer_t *p2o_audio_rb;
  jack_ringbuffer_t *o2p_midi_rb;
  jack_ringbuffer_t *p2o_midi_rb;
  jack_port_t **output_ports;
  jack_port_t **input_ports;
  jack_port_t *midi_output_port;
  jack_port_t *midi_input_port;
  jack_nframes_t bufsize;
  double samplerate;
  //Parameters
  uint8_t bus;
  uint8_t address;
  int blocks_per_transfer;
  int quality;
  int priority;
  struct resampler resampler;
};

int jclient_run (struct jclient *);

void *jclient_run_thread (void *);

void jclient_exit (struct jclient *);

void jclient_print_latencies (struct jclient *, const char *);
