/*
 *   overwitch.h
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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

#ifndef OVERWITCH_H
#define OVERWITCH_H

#include <stdint.h>
#include <stdlib.h>

#define OB_SAMPLE_RATE 48000.0
#define OB_FRAMES_PER_BLOCK 7
#define OB_BYTES_PER_SAMPLE sizeof(float)
#define OB_MAX_TRACKS 12
#define OB_MIDI_EVENT_SIZE 4

typedef size_t (*ow_buffer_rw_space_t) (void *);
typedef size_t (*ow_buffer_read_t) (void *, char *, size_t);
typedef size_t (*ow_buffer_write_t) (void *, const char *, size_t);

typedef double (*ow_get_time_t) ();	//Time in seconds

typedef void (*ow_set_rt_priority_t) (pthread_t *, int);

typedef enum
{
  OW_OK = 0,
  OW_GENERIC_ERROR,
  OW_USB_ERROR_LIBUSB_INIT_FAILED,
  OW_USB_ERROR_CANT_OPEN_DEV,
  OW_USB_ERROR_CANT_SET_USB_CONFIG,
  OW_USB_ERROR_CANT_CLAIM_IF,
  OW_USB_ERROR_CANT_SET_ALT_SETTING,
  OW_USB_ERROR_CANT_CLEAR_EP,
  OW_USB_ERROR_CANT_PREPARE_TRANSFER,
  OW_USB_ERROR_CANT_FIND_DEV,
  OW_INIT_ERROR_NO_READ_SPACE,
  OW_INIT_ERROR_NO_WRITE_SPACE,
  OW_INIT_ERROR_NO_READ,
  OW_INIT_ERROR_NO_WRITE,
  OW_INIT_ERROR_NO_O2P_AUDIO_BUF,
  OW_INIT_ERROR_NO_P2O_AUDIO_BUF,
  OW_INIT_ERROR_NO_O2P_MIDI_BUF,
  OW_INIT_ERROR_NO_P2O_MIDI_BUF,
  OW_INIT_ERROR_NO_GET_TIME
} ow_err_t;

typedef enum
{
  OW_STATUS_ERROR = -1,
  OW_STATUS_STOP,
  OW_STATUS_READY,
  OW_STATUS_BOOT,
  OW_STATUS_WAIT,
  OW_STATUS_RUN
} ow_engine_status_t;

typedef enum
{
  RES_STATUS_ERROR = -1,
  RES_STATUS_STOP,
  RES_STATUS_READY,
  RES_STATUS_BOOT,
  RES_STATUS_TUNE,
  RES_STATUS_RUN
} ow_resampler_status_t;

struct ow_io_buffers
{
  //Functions
  ow_buffer_rw_space_t write_space;
  ow_buffer_write_t write;
  ow_buffer_rw_space_t read_space;
  ow_buffer_read_t read;
  //Needed for MIDI
  ow_get_time_t get_time;
  //Data
  void *p2o_audio;
  void *o2p_audio;
  void *p2o_midi;
  void *o2p_midi;
};

struct ow_usb_device
{
  char *name;
  uint16_t vid;
  uint16_t pid;
  uint8_t bus;
  uint8_t address;
};

struct ow_device_desc
{
  uint16_t pid;
  char *name;
  int inputs;
  int outputs;
  char *input_track_names[OB_MAX_TRACKS];
  char *output_track_names[OB_MAX_TRACKS];
};

struct ow_midi_event
{
  double time;
  uint8_t bytes[OB_MIDI_EVENT_SIZE];
};

struct ow_engine;
struct ow_resampler;

extern const struct ow_device_desc *OB_DEVICE_DESCS[];

//Common
const char *ow_get_err_str (ow_err_t);

int ow_get_devices (struct ow_usb_device **, ssize_t *);

void ow_free_usb_device_list (struct ow_usb_device *, ssize_t);

int ow_get_device_desc_from_vid_pid (uint16_t, uint16_t,
				     const struct ow_device_desc **);

int ow_get_usb_device_from_device_attrs (int, const char *,
					 struct ow_usb_device **);

//Engine
ow_err_t ow_engine_init (struct ow_engine **, uint8_t, uint8_t, int);

ow_err_t ow_engine_activate (struct ow_engine *, struct ow_io_buffers *);

void ow_engine_destroy (struct ow_engine *);

void ow_engine_wait (struct ow_engine *);

ow_engine_status_t ow_engine_get_status (struct ow_engine *);

void ow_engine_set_status (struct ow_engine *, ow_engine_status_t);

void ow_engine_set_p2o_audio_enable (struct ow_engine *, int);

int ow_engine_is_p2o_audio_enable (struct ow_engine *);

const struct ow_device_desc *ow_engine_get_device_desc (struct ow_engine *);

void ow_engine_stop (struct ow_engine *);

//Resampler
ow_err_t ow_resampler_init (struct ow_resampler **, int, int, int, int);

ow_err_t ow_resampler_activate (struct ow_resampler *,
				struct ow_io_buffers *, int,
				ow_set_rt_priority_t);

void ow_resampler_wait (struct ow_resampler *);

void ow_resampler_destroy (struct ow_resampler *);

void ow_resampler_print_status (struct ow_resampler *);

void ow_resampler_reset_buffers (struct ow_resampler *);

void ow_resampler_reset_dll (struct ow_resampler *, uint32_t);

void ow_resampler_read_audio (struct ow_resampler *);

void ow_resampler_write_audio (struct ow_resampler *);

int ow_resampler_compute_ratios (struct ow_resampler *, double);

void ow_resampler_inc_xruns (struct ow_resampler *);

ow_resampler_status_t ow_resampler_get_status (struct ow_resampler *);

struct ow_engine *ow_resampler_get_engine (struct ow_resampler *);

void ow_resampler_stop (struct ow_resampler *);

void ow_resampler_set_buffer_size (struct ow_resampler *, uint32_t);

void ow_resampler_set_samplerate (struct ow_resampler *, uint32_t);

ow_resampler_status_t ow_resampler_get_status (struct ow_resampler *);

size_t ow_resampler_get_o2p_frame_size (struct ow_resampler *);

size_t ow_resampler_get_p2o_frame_size (struct ow_resampler *);

float *ow_resampler_get_o2p_audio_buffer (struct ow_resampler *);

float *ow_resampler_get_p2o_audio_buffer (struct ow_resampler *);

#endif
