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

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#define OB_SAMPLE_RATE 48000.0
#define OB_FRAMES_PER_BLOCK 7
#define OB_BYTES_PER_SAMPLE sizeof(float)
#define OB_MAX_TRACKS 64

#define OW_DEFAULT_RT_PROPERTY 20

#define OW_LABEL_MAX_LEN 64

#define OW_DEFAULT_XFR_TIMEOUT 10

#define OW_DEFAULT_BLOCKS 24

typedef size_t (*ow_buffer_rw_space_t) (void *);
typedef size_t (*ow_buffer_read_t) (void *, char *, size_t);
typedef size_t (*ow_buffer_write_t) (void *, const char *, size_t);

typedef uint64_t (*ow_get_time_t) ();	//Time in us

struct ow_context;

typedef void (*ow_dll_overbridge_init_t) (void *, double, uint32_t);

typedef void (*ow_dll_overbridge_update_t) (void *, uint32_t, uint64_t);

typedef void (*ow_set_rt_priority_t) (pthread_t, int);

struct ow_resampler_latency
{
  double o2h;
  double o2h_min;
  double o2h_max;
  double h2o;
  double h2o_min;
  double h2o_max;
};

typedef void (*ow_resampler_report_t) (void *, struct ow_resampler_latency *,
				       double, double);

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
  OW_INIT_ERROR_NO_GET_TIME,
  OW_INIT_ERROR_NO_DLL
} ow_err_t;

typedef enum
{
  OW_ENGINE_STATUS_ERROR = -1,
  OW_ENGINE_STATUS_STOP,
  OW_ENGINE_STATUS_READY,
  OW_ENGINE_STATUS_STEADY,
  OW_ENGINE_STATUS_BOOT,
  OW_ENGINE_STATUS_CLEAR,
  OW_ENGINE_STATUS_WAIT,
  OW_ENGINE_STATUS_RUN
} ow_engine_status_t;

typedef enum
{
  OW_RESAMPLER_STATUS_ERROR = -1,
  OW_RESAMPLER_STATUS_STOP,
  OW_RESAMPLER_STATUS_READY,
  OW_RESAMPLER_STATUS_BOOT,
  OW_RESAMPLER_STATUS_TUNE,
  OW_RESAMPLER_STATUS_RUN
} ow_resampler_status_t;

typedef enum
{
  OW_ENGINE_OPTION_O2P_AUDIO = 1,
  OW_ENGINE_OPTION_P2O_AUDIO = 2
} ow_engine_option_t;

struct ow_context
{
  //Functions
  ow_buffer_rw_space_t write_space;
  ow_buffer_write_t write;
  ow_buffer_rw_space_t read_space;
  ow_buffer_read_t read;
  //Needed for the DLL
  ow_get_time_t get_time;
  //Data
  void *h2o_audio;
  void *o2h_audio;
  //DLL
  struct ow_dll *dll;
  ow_dll_overbridge_init_t dll_overbridge_init;
  ow_dll_overbridge_update_t dll_overbridge_update;
  //RT priority is always activated. If this is NULL, Overwitch will set itself with its default RT priority and policy.
  ow_set_rt_priority_t set_rt_priority;
  int priority;
  //Options
  int options;
};

struct ow_device_desc
{
  uint16_t pid;
  char *name;
  int inputs;
  int outputs;
  char **input_track_names;
  char **output_track_names;
};

struct ow_device_desc_static
{
  uint16_t pid;
  char *name;
  int inputs;
  int outputs;
  char *input_track_names[OB_MAX_TRACKS];
  char *output_track_names[OB_MAX_TRACKS];
};

struct ow_usb_device
{
  struct ow_device_desc desc;
  uint16_t vid;
  uint16_t pid;
  uint8_t bus;
  uint8_t address;
};

struct ow_resampler_reporter
{
  ow_resampler_report_t callback;
  int period;
  void *data;
};

struct ow_engine;
struct ow_resampler;

//Common
const char *ow_get_err_str (ow_err_t);

int ow_get_usb_device_list (struct ow_usb_device **, size_t *);

void ow_free_usb_device_list (struct ow_usb_device *, size_t);

void ow_free_device_desc (struct ow_device_desc *);

int ow_get_device_desc_from_vid_pid (uint16_t, uint16_t,
				     struct ow_device_desc *);

int ow_get_usb_device_from_device_attrs (int, const char *,
					 struct ow_usb_device **);

void ow_set_thread_rt_priority (pthread_t, int);

void ow_copy_device_desc_static (struct ow_device_desc *,
				 const struct ow_device_desc_static *);

//Engine
ow_err_t ow_engine_init_from_bus_address (struct ow_engine **, uint8_t,
					  uint8_t, unsigned int,
					  unsigned int);

ow_err_t ow_engine_init_from_libusb_device_descriptor (struct ow_engine **,
						       int, unsigned int,
						       unsigned int);

ow_err_t ow_engine_start (struct ow_engine *engine,
			  struct ow_context *context);

void ow_engine_clear_buffers (struct ow_engine *);

void ow_engine_destroy (struct ow_engine *);

void ow_engine_wait (struct ow_engine *);

ow_engine_status_t ow_engine_get_status (struct ow_engine *);

void ow_engine_set_status (struct ow_engine *, ow_engine_status_t);

int ow_engine_is_option (struct ow_engine *, ow_engine_option_t);

void ow_engine_set_option (struct ow_engine *, ow_engine_option_t, int);

struct ow_device_desc *ow_engine_get_device_desc (struct ow_engine *);

void ow_engine_stop (struct ow_engine *);

void ow_engine_set_overbridge_name (struct ow_engine *, const char *);

const char *ow_engine_get_overbridge_name (struct ow_engine *);

//Resampler
ow_err_t ow_resampler_init_from_bus_address (struct ow_resampler **, uint8_t,
					     uint8_t, unsigned int,
					     unsigned int, int);

ow_err_t ow_resampler_start (struct ow_resampler *, struct ow_context *);

void ow_resampler_wait (struct ow_resampler *);

void ow_resampler_destroy (struct ow_resampler *);

void ow_resampler_report_status (struct ow_resampler *);

void ow_resampler_clear_buffers (struct ow_resampler *);

void ow_resampler_reset (struct ow_resampler *);

void ow_resampler_read_audio (struct ow_resampler *);

void ow_resampler_write_audio (struct ow_resampler *);

int ow_resampler_compute_ratios (struct ow_resampler *, uint64_t,
				 void (*)(void *), void *);

void ow_resampler_inc_xruns (struct ow_resampler *);

ow_resampler_status_t ow_resampler_get_status (struct ow_resampler *);

struct ow_engine *ow_resampler_get_engine (struct ow_resampler *);

void ow_resampler_stop (struct ow_resampler *);

void ow_resampler_set_buffer_size (struct ow_resampler *, uint32_t);

void ow_resampler_set_samplerate (struct ow_resampler *, uint32_t);

size_t ow_resampler_get_o2h_frame_size (struct ow_resampler *);

size_t ow_resampler_get_h2o_frame_size (struct ow_resampler *);

float *ow_resampler_get_o2h_audio_buffer (struct ow_resampler *);

float *ow_resampler_get_h2o_audio_buffer (struct ow_resampler *);

struct ow_resampler_reporter *ow_resampler_get_reporter (struct ow_resampler
							 *);

void ow_resampler_get_h2o_latency (struct ow_resampler *, size_t *, size_t *,
				   size_t *);

void ow_resampler_get_o2h_latency (struct ow_resampler *, size_t *, size_t *,
				   size_t *);

double ow_resampler_get_target_delay_ms (struct ow_resampler *);
