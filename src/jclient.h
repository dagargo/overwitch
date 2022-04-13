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

typedef void (*jclient_end_notifier_t) (uint8_t, uint8_t);

struct jclient
{
  //JACK stuff
  jack_client_t *client;
  jack_port_t **output_ports;
  jack_port_t **input_ports;
  jack_port_t *midi_output_port;
  jack_port_t *midi_input_port;
  //Parameters
  uint8_t bus;
  uint8_t address;
  int blocks_per_transfer;
  int quality;
  int priority;
  jack_nframes_t bufsize;
  // Overwitch stuff
  struct ow_resampler *resampler;
  struct ow_context context;
  struct ow_resampler_reporter reporter;
  // Thread end notifier
  jclient_end_notifier_t end_notifier;
};

int jclient_init (struct jclient *);

int jclient_run (struct jclient *);

void *jclient_run_thread (void *);

void jclient_exit (struct jclient *);

void jclient_print_latencies (struct ow_resampler *, const char *);
