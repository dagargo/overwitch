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

#define JCLIENT_DEFAULT_PRIORITY -1

typedef void (*jclient_end_notifier_t) (uint8_t, uint8_t);
typedef void (*jclient_notify_status_t) (int, jack_nframes_t, jack_nframes_t);

struct squeue
{
  uint8_t *data;
  uint32_t item_size;
  uint32_t len;
  uint32_t max_len;
};

struct jclient
{
  //JACK stuff
  const char *name;
  jack_client_t *client;
  jack_port_t **output_ports;
  jack_port_t **input_ports;
  jack_port_t *midi_output_port;
  jack_port_t *midi_input_port;
  int j2o_ongoing_sysex;
  struct squeue o2j_midi_queue;
  struct squeue j2o_midi_queue;
  int o2j_midi_skipping;
  uint32_t o2j_last_lost_count;
  //Parameters
  uint8_t bus;
  uint8_t address;
  unsigned int blocks_per_transfer;
  unsigned int xfr_timeout;
  int quality;
  int priority;
  jack_nframes_t bufsize;
  // Overwitch stuff
  struct ow_resampler *resampler;
  struct ow_context context;
  // Thread stuff
  int running;
  pthread_t thread;
};

void jclient_check_jack_server (jclient_notify_status_t);

int jclient_init (struct jclient *);

int jclient_start (struct jclient *);

void jclient_destroy (struct jclient *);

void jclient_wait (struct jclient *);

void jclient_stop (struct jclient *);

void jclient_print_latencies (struct ow_resampler *, const char *);

void jclient_copy_o2j_audio (float *, jack_nframes_t,
			     jack_default_audio_sample_t *[],
			     const struct ow_device_desc *);

void jclient_copy_j2o_audio (float *, jack_nframes_t,
			     jack_default_audio_sample_t *[],
			     const struct ow_device_desc *);

void squeue_init (struct squeue *queue, uint32_t max_len);

void squeue_destroy (struct squeue *queue);

int squeue_write (struct squeue *queue, void *data, uint32_t len);

void squeue_consume (struct squeue *queue, uint32_t consumed);

void squeue_read (struct squeue *queue, void *data);
