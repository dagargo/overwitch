/*
 * engine.c
 * Copyright (C) 2019 Stefan Rehm <droelfdroelf@gmail.com>
 * Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
 * Copyright (C) 2026 Ladislav Rauch <ladislavrauch@gmail.com>
 * Copyright (C) 2026 Zach Denton <z@chdenton.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "overwitch.h"
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <endian.h>
#include <unistd.h>
#include <pthread.h>
#include "engine.h"

#define OBX_USB_CONFIGURATION 1

#define OB1_USB_AUDIO_OUT_EP 0x03
#define OB1_USB_AUDIO_OUT_INTERFACE 1

#define OB1_USB_AUDIO_IN_EP (OB1_USB_AUDIO_OUT_EP | 0x80)
#define OB1_USB_AUDIO_IN_INTERFACE 0

#define OB1_USB_REQ_SET_DEPTH 0
#define OB1_USB_REQ_SET_MAP 1
#define OB1_USB_REQ_GET_DEPTH 2
#define OB1_USB_REQ_GET_MAP 3
//These use the vendor and interface request type for output.
#define OB1_USB_REQ_SET_STORED_DEPTH 2
#define OB1_USB_REQ_SET_STORED_MAP 3
#define OB1_USB_REQ_GET_PROTOCOL 4

#define OB1_USB_PROTOCOL_LEN 4
#define OB1_USB_CONTROL_TIMEOUT 1000

//Only one in every these errors is reported.
#define OB1_ERROR_REPORT_PERIOD 1000

#define OB2_USB_CONTROL_INTERFACE 4
#define OB2_USB_MIDI_INTERFACE 5

// Overbridge 2 interrupt endpoints
#define OB2_USB_AUDIO_OUT_EP 0x03
#define OB2_USB_AUDIO_OUT_INTERFACE 2
#define OB2_USB_AUDIO_OUT_ALT_SETTING 3

#define OB2_USB_AUDIO_IN_EP (OB2_USB_AUDIO_OUT_EP | 0x80)
#define OB2_USB_AUDIO_IN_INTERFACE 1
#define OB2_USB_AUDIO_IN_ALT_SETTING 3

/*
 * Overbridge V3 / Tonverk framing, derived from USBPcap captures.
 *
 * O2H (EP 0x83): 24 packets per nominal 3 ms transfer. Each packet starts
 * with a little-endian frame count followed by 36 channels of signed LE PCM.
 * Six frames (868 B) is nominal; seven-frame packets (1012 B) are also valid.
 *
 * H2O (EP 0x03): 3 packets per 3 ms transfer. Each 396-byte packet is:
 *   4 B header (0xff00 | frame_count + 16-bit device-clock counter)
 *   storage for up to 49 stereo signed LE int32 PCM frames (392 B)
 *
 * Captures show 0xff30 packets carrying 48 frames and 0xff31 packets
 * carrying 49. The apparent 8-byte "tail" of a 48-frame packet is the
 * storage occupied by frame 49 when the packet is marked 0xff31.
 */

// Overbridge 3 isochronous endpoints
#define OB3_USB_AUDIO_IN_INTERFACE 3
#define OB3_USB_AUDIO_IN_ALT_SETTING 1
#define OB3_USB_AUDIO_IN_EP 0x83

#define OB3_USB_AUDIO_OUT_INTERFACE 4
#define OB3_USB_AUDIO_OUT_ALT_SETTING 1
#define OB3_USB_AUDIO_OUT_EP 0x03

#define OB3_IN_FRAMES_PER_PACKET 6U
#define OB3_IN_MAX_FRAMES_PER_PACKET 7U
#define OB3_OUT_FRAMES_PER_PACKET 48U
#define OB3_OUT_MAX_FRAMES_PER_PACKET 49U
#define OB3_IN_WIRE_CHANNELS 36U
#define OB3_OUT_WIRE_CHANNELS 2U
#define OB3_INPUT_HEADER_LEN 4U
#define OB3_OUTPUT_HEADER_LEN 4U
#define OB3_OUTPUT_AUDIO_OFFSET OB3_OUTPUT_HEADER_LEN
#define OB3_INPUT_MAX_PACKET_LEN \
  (OB3_INPUT_HEADER_LEN + \
   OB3_IN_MAX_FRAMES_PER_PACKET * \
   OB3_IN_WIRE_CHANNELS * sizeof (int32_t))
#define OB3_OUTPUT_PACKET_LEN \
  (OB3_OUTPUT_HEADER_LEN + \
   OB3_OUT_MAX_FRAMES_PER_PACKET * \
   OB3_OUT_WIRE_CHANNELS * sizeof (int32_t))
#define OB3_OUTPUT_HEADER_BASE 0xff00U

/*
 * Official Overbridge capture: an O2H transfer's 144/145-frame cadence is
 * reflected on H2O six wire transfers later.  Three OUT URBs are already
 * queued, so keep only three additional completed-transfer slots here.
 */
#define OB3_CLOCK_DELAY_TRANSFERS 3U

#define USB_CONTROL_LEN (sizeof (struct libusb_control_setup) + OB_NAME_MAX_LEN)

//Protocol 113, async header 2
static const uint8_t OB1_USB_PROTOCOL[OB1_USB_PROTOCOL_LEN] = {
  0x00, 0x71, 0x00, 0x02
};

static const char *OB1_DIRECTION_NAMES[] = { "o2h", "h2o" };

/* Clock reflection delay line for V3 synchronization. */
static uint16_t v3_clock_fifo[256];
static uint8_t v3_fifo_head = 0;
static uint8_t v3_fifo_tail = 0;
static pthread_mutex_t v3_fifo_lock = PTHREAD_MUTEX_INITIALIZER;

static void prepare_cycle_in_v2 (struct ow_engine *engine);
static void prepare_cycle_out_v2 (struct ow_engine *engine);
static void cb_xfr_audio_in_v1 (struct libusb_transfer *xfr);
static void cb_xfr_audio_out_v1 (struct libusb_transfer *xfr);
static void ow_engine_load_overbridge_name (struct ow_engine *engine);
static int ow_engine_init_control_v3 (struct ow_engine *engine);

static void LIBUSB_CALL cb_xfr_audio_in_v3 (struct libusb_transfer *xfr);
static void LIBUSB_CALL cb_xfr_audio_out_v3 (struct libusb_transfer *xfr);
static void prepare_cycle_in_v3 (struct ow_engine *engine);
static void prepare_cycle_out_v3 (struct ow_engine *engine);
static int prepare_iso_transfers (struct ow_engine *engine);
static unsigned int read_usb_input_xfr_v3 (struct ow_engine *engine,
					   struct libusb_transfer *xfr);
static void set_usb_input_data_xfr_v3 (struct ow_engine *engine,
				       struct libusb_transfer *xfr);
static void write_usb_output_xfr_v3 (struct ow_engine *engine,
				     struct libusb_transfer *xfr,
				     unsigned int total_frames);
static unsigned int get_usb_output_frames_v3 (struct ow_engine *engine);
static void set_usb_output_data_xfr_v3 (struct ow_engine *engine,
					struct libusb_transfer *xfr);

unsigned int
ow_engine_get_blocks_per_transfer (struct ow_engine *engine)
{
  return engine->blocks_per_transfer;
}

unsigned int
ow_engine_get_frames_per_block (struct ow_engine *engine)
{
  return engine->frames_per_block;
}

static void
ow_engine_init_name (struct ow_engine *engine)
{
  snprintf (engine->name, OW_ENGINE_NAME_MAX_LEN, "%s @ %03d,%03d",
	    engine->device->desc.name, engine->device->bus,
	    engine->device->address);
  if (engine->device->desc.version != OW_DEVICE_VERSION_3)
    {
      ow_engine_load_overbridge_name (engine);
    }
  else if (engine->overbridge_name[0] == '\0')
    {
      snprintf (engine->overbridge_name, OB_NAME_MAX_LEN, "%s",
		engine->device->desc.name);
    }
}

static int
prepare_transfers_v2 (struct ow_engine *engine)
{
  engine->usb.xfr_audio_in = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_audio_in)
    {
      return OW_GENERIC_ERROR;
    }

  engine->usb.xfr_audio_out = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_audio_out)
    {
      return OW_GENERIC_ERROR;
    }

  return LIBUSB_SUCCESS;
}

static int
prepare_control_transfers (struct ow_engine *engine)
{
  engine->usb.xfr_control_in = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_control_in)
    {
      return OW_GENERIC_ERROR;
    }

  engine->usb.xfr_control_out = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_control_out)
    {
      return OW_GENERIC_ERROR;
    }

  return LIBUSB_SUCCESS;
}

static unsigned int
ow_engine_v3_out_packets (const struct ow_engine *engine)
{
  return engine->frames_per_transfer / OB3_OUT_FRAMES_PER_PACKET;
}

static int
prepare_iso_transfers (struct ow_engine *engine)
{
  const unsigned int in_packets = engine->blocks_per_transfer;
  const unsigned int out_packets = ow_engine_v3_out_packets (engine);

  for (int i = 0; i < OW_ISO_URBS; i++)
    {
      engine->usb.xfr_audio_in_q[i] = libusb_alloc_transfer (in_packets);
      if (!engine->usb.xfr_audio_in_q[i])
	return OW_GENERIC_ERROR;

      engine->usb.xfr_audio_out_q[i] = libusb_alloc_transfer (out_packets);
      if (!engine->usb.xfr_audio_out_q[i])
	return OW_GENERIC_ERROR;
    }

  return LIBUSB_SUCCESS;
}

inline void
ow_engine_read_usb_input_blocks_v2 (struct ow_engine *engine)
{
  int32_t hv;
  uint8_t *s;
  struct ow_engine_usb_blk_v2 *blk;
  float *f = engine->o2h_transfer_buf;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {

      blk = GET_NTH_INPUT_USB_BLK_V2 (engine, i);
      s = (uint8_t *) blk->data;
      for (int j = 0; j < engine->frames_per_block; j++)
	{
	  for (int k = 0; k < engine->device->desc.outputs; k++)
	    {
	      int size = engine->device->desc.output_tracks[k].size;

	      memcpy (&hv, s, size);

	      if (engine->device->desc.version == OW_DEVICE_VERSION_2_1
		  && size == 4)
		{
		  hv >>= 8;
		}

	      hv = be32toh (hv);

	      *f = hv / (float) INT32_MAX;
	      f++;
	      s += size;
	    }
	}
    }
}

static ow_engine_status_t
update_o2h_dll (struct ow_engine *engine, uint32_t xfr_frames)
{
  ow_engine_status_t status;

  pthread_spin_lock (&engine->lock);
  if (engine->context->dll)
    {
      engine->context->dll_overbridge_update (engine->context->dll,
					      xfr_frames,
					      engine->context->get_time ());
    }
  status = engine->status;
  pthread_spin_unlock (&engine->lock);

  return status;
}

static void
write_o2h_audio (struct ow_engine *engine, ow_engine_status_t status,
		 uint32_t xfr_frames)
{
  size_t wso2h;
  size_t size = xfr_frames * engine->o2h_audio_frame_size;

  if (status < OW_ENGINE_STATUS_RUN)
    {
      return;
    }

  wso2h = engine->context->write_space (engine->context->o2h_audio);
  if (size <= wso2h)
    {
      engine->context->write (engine->context->o2h_audio,
			      (void *) engine->o2h_transfer_buf, size);
    }
  else
    {
      error_print ("o2h: Audio ring buffer overflow. Discarding data...");
    }

  pthread_spin_lock (&engine->lock);
  engine->latency_o2h =
    engine->context->read_space (engine->context->o2h_audio) /
    engine->o2h_audio_frame_size;
  if (engine->latency_o2h > engine->latency_o2h_max)
    {
      engine->latency_o2h_max = engine->latency_o2h;
    }
  pthread_spin_unlock (&engine->lock);
}

static void
set_usb_input_data_blks_v2 (struct ow_engine *engine)
{
  ow_engine_status_t status;

  status = update_o2h_dll (engine, engine->frames_per_transfer);
  ow_engine_read_usb_input_blocks_v2 (engine);
  write_o2h_audio (engine, status, engine->frames_per_transfer);
}

inline void
ow_engine_write_usb_output_blocks_v2 (struct ow_engine *engine)
{
  int32_t ov;
  uint8_t *s;
  struct ow_engine_usb_blk_v2 *blk;
  float *f = engine->h2o_transfer_buf;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {

      blk = GET_NTH_OUTPUT_USB_BLK_V2 (engine, i);
      blk->frames = htobe16 (engine->usb.audio_frames_counter);
      engine->usb.audio_frames_counter += engine->frames_per_block;
      s = (uint8_t *) blk->data;
      for (int j = 0; j < engine->frames_per_block; j++)
	{
	  for (int k = 0; k < engine->device->desc.inputs; k++)
	    {
	      int size = engine->device->desc.input_tracks[k].size;
	      ov = (int32_t) (*f * INT32_MAX);

	      if (engine->device->desc.version == OW_DEVICE_VERSION_2_1
		  && size == 4)
		{
		  ov >>= 8;
		}

	      ov = htobe32 (ov);

	      memcpy (s, &ov, size);

	      f++;
	      s += size;
	    }
	}
    }
}

//Fills h2o_transfer_buf with xfr_frames frames. Returns 0 if the USB blocks
//do not need to be updated.
static int
read_h2o_audio (struct ow_engine *engine, uint32_t xfr_frames)
{
  size_t rsh2o;
  size_t bytes;
  long frames;
  int res;
  size_t size = xfr_frames * engine->h2o_audio_frame_size;
  int h2o_enabled = ow_engine_is_option (engine, OW_ENGINE_OPTION_H2O_AUDIO);

  if (h2o_enabled)
    {
      rsh2o = engine->context->read_space (engine->context->h2o_audio);
      if (!engine->reading_at_h2o_end)
	{
	  if (rsh2o >= size &&
	      ow_engine_get_status (engine) == OW_ENGINE_STATUS_RUN)
	    {
	      bytes = ow_bytes_to_frame_bytes (rsh2o,
					       engine->h2o_audio_frame_size);
	      debug_print (3, "h2o: Emptying buffer (%zu B) and running...",
			   bytes);
	      engine->context->read (engine->context->h2o_audio, NULL, bytes);
	      engine->reading_at_h2o_end = 1;
	    }
	  return 1;
	}
    }
  else
    {
      if (engine->reading_at_h2o_end)
	{
	  debug_print (3, "h2o: Clearing buffer and stopping reading...");
	  memset (engine->h2o_transfer_buf, 0, engine->h2o_transfer_size);
	  engine->reading_at_h2o_end = 0;
	  engine->latency_h2o_max = engine->latency_h2o_min;
	  return 1;
	}
      return 0;
    }

  pthread_spin_lock (&engine->lock);
  engine->latency_h2o = rsh2o / engine->h2o_audio_frame_size;
  if (engine->latency_h2o > engine->latency_h2o_max)
    {
      engine->latency_h2o_max = engine->latency_h2o;
    }
  pthread_spin_unlock (&engine->lock);

  if (rsh2o >= size)
    {
      engine->context->read (engine->context->h2o_audio,
			     (void *) engine->h2o_transfer_buf, size);
    }
  else if (rsh2o > engine->h2o_audio_frame_size)	//At least 2 frames to apply resampling to
    {
      debug_print (3,
		   "h2o: Audio ring buffer underflow (%zu B < %zu B). Fixed by resampling.",
		   rsh2o, size);
      frames = rsh2o / engine->h2o_audio_frame_size;
      bytes = frames * engine->h2o_audio_frame_size;
      engine->context->read (engine->context->h2o_audio,
			     (void *) engine->h2o_resampler_buf, bytes);
      engine->h2o_data.input_frames = frames;
      engine->h2o_data.output_frames = xfr_frames;
      engine->h2o_data.src_ratio = (double) xfr_frames / frames;
      //We should NOT use the simple API but since this only happens very occasionally and mostly at startup, this has very low impact on audio quality.
      res = src_simple (&engine->h2o_data, SRC_SINC_FASTEST,
			engine->device->desc.inputs);
      if (res)
	{
	  error_print
	    ("h2o: Error while resampling %zu frames (%zu B, ratio %f): %s",
	     frames, bytes, engine->h2o_data.src_ratio, src_strerror (res));
	}
      else if (engine->h2o_data.output_frames_gen != xfr_frames)
	{
	  error_print
	    ("h2o: Unexpected frames with ratio %f (output %ld, expected %u)",
	     engine->h2o_data.src_ratio, engine->h2o_data.output_frames_gen,
	     xfr_frames);
	}

      // Any maximum value is invalid at this point
      pthread_spin_lock (&engine->lock);
      engine->latency_o2h_max = engine->latency_o2h_min;
      pthread_spin_unlock (&engine->lock);
    }
  else
    {
      debug_print (3, "h2o: Not enough data (%zu B). Waiting...", rsh2o);
      memset (engine->h2o_transfer_buf, 0, size);
    }

  return 1;
}

static void
set_usb_output_data_blks_v2 (struct ow_engine *engine)
{
  if (read_h2o_audio (engine, engine->frames_per_transfer))
    {
      ow_engine_write_usb_output_blocks_v2 (engine);
    }
}

inline void
ow_engine_print_usb_block_v2 (struct ow_engine *engine, int blk_idx, int o2h,
			      uint16_t *debug_counter)
{
  uint8_t *s;
  size_t frame_size;
  struct ow_engine_usb_blk_v2 *blk;

  if (debug_level >= 3)
    {
      if (*debug_counter == 0)
	{
	  blk = o2h ? GET_NTH_INPUT_USB_BLK_V2 (engine, blk_idx)
	    : GET_NTH_OUTPUT_USB_BLK_V2 (engine, blk_idx);
	  s = (uint8_t *) blk->data;
	  frame_size = o2h ? engine->o2h_frame_size : engine->h2o_frame_size;

	  fprintf (stderr, "%s block: header: 0x%04x; frames: 0x%04x\n",
		   o2h ? "O2H" : "H2O", be16toh (blk->header),
		   be16toh (blk->frames));

	  for (int j = 0; j < engine->frames_per_block; j++)
	    {
	      fprintf (stderr, "  Frame %d:", j);

	      for (int k = 0; k < frame_size; k++, s++)
		{
		  fprintf (stderr, " %02x", *s);
		}

	      fprintf (stderr, "\n");
	    }
	}
      *debug_counter += engine->frames_per_transfer;
      if (*debug_counter >= OB_SAMPLE_RATE)
	{
	  *debug_counter = 0;
	}
    }
}

static void LIBUSB_CALL
cb_xfr_audio_in_v2 (struct libusb_transfer *xfr)
{
  static uint16_t debug_counter = 0;
  struct ow_engine *engine = xfr->user_data;

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      if (xfr->actual_length < xfr->length)
	{
	  error_print ("o2h: incomplete USB audio transfer (%d B < %d B)",
		       xfr->actual_length, xfr->length);
	}

      struct ow_engine *engine = xfr->user_data;
      if (engine->context->options & OW_ENGINE_OPTION_O2H_AUDIO)
	{
	  ow_engine_print_usb_block_v2 (engine, 0, 1, &debug_counter);
	  set_usb_input_data_blks_v2 (engine);
	}
    }
  else
    {
      error_print ("o2h: Error on USB audio transfer (%d B): %s",
		   xfr->actual_length, libusb_error_name (xfr->status));
    }

  if (ow_engine_get_status (engine) > OW_ENGINE_STATUS_STOP)
    {
      // start new cycle even if this one did not succeed
      prepare_cycle_in_v2 (xfr->user_data);
    }
}

static void LIBUSB_CALL
cb_xfr_audio_out_v2 (struct libusb_transfer *xfr)
{
  static uint16_t debug_counter = 0;
  struct ow_engine *engine = xfr->user_data;

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      if (xfr->actual_length < xfr->length)
	{
	  error_print ("h2o: incomplete USB audio transfer (%d B < %d B)",
		       xfr->actual_length, xfr->length);
	}
    }
  else
    {
      error_print ("h2o: Error on USB audio transfer (%d B): %s",
		   xfr->actual_length, libusb_error_name (xfr->status));
    }

  set_usb_output_data_blks_v2 (xfr->user_data);
  ow_engine_print_usb_block_v2 (engine, 0, 0, &debug_counter);

  if (ow_engine_get_status (engine) > OW_ENGINE_STATUS_STOP)
    {
      // We have to make sure that the out cycle is always started after its callback
      // Race condition on slower systems!
      prepare_cycle_out_v2 (xfr->user_data);
    }
}

static void
prepare_cycle_out_v2 (struct ow_engine *engine)
{
  libusb_fill_interrupt_transfer (engine->usb.xfr_audio_out,
				  engine->usb.device_handle,
				  OB2_USB_AUDIO_OUT_EP,
				  engine->usb.xfr_audio_out_data,
				  engine->usb.xfr_audio_out_data_len,
				  cb_xfr_audio_out_v2, engine,
				  engine->usb.xfr_timeout);

  int err = libusb_submit_transfer (engine->usb.xfr_audio_out);
  if (err)
    {
      error_print ("h2o: Error when submitting USB audio out transfer: %s",
		   libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
    }
}

static void
prepare_cycle_in_v2 (struct ow_engine *engine)
{
  libusb_fill_interrupt_transfer (engine->usb.xfr_audio_in,
				  engine->usb.device_handle,
				  OB2_USB_AUDIO_IN_EP,
				  engine->usb.xfr_audio_in_data,
				  engine->usb.xfr_audio_in_data_len,
				  cb_xfr_audio_in_v2, engine,
				  engine->usb.xfr_timeout);

  int err = libusb_submit_transfer (engine->usb.xfr_audio_in);
  if (err)
    {
      error_print ("o2h: Error when submitting USB audio in transfer: %s",
		   libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
    }
}

static inline float
get_sample_v1 (const uint8_t *s, int size)
{
  int32_t v;

  if (size == 2)
    {
      v = (int32_t) ((uint32_t) s[0] << 16 | (uint32_t) s[1] << 24);
    }
  else
    {
      v = (int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 |
		     (uint32_t) s[2] << 24);
    }

  return v / (float) INT32_MAX;
}

static inline void
set_sample_v1 (uint8_t *s, int size, float f)
{
  int32_t v;

  if (f >= 1.0f)
    {
      v = INT32_MAX;
    }
  else if (f <= -1.0f)
    {
      v = INT32_MIN;
    }
  else
    {
      v = (int32_t) (f * INT32_MAX);
    }

  if (size == 2)
    {
      s[0] = v >> 16;
      s[1] = v >> 24;
    }
  else
    {
      s[0] = v >> 8;
      s[1] = v >> 16;
      s[2] = v >> 24;
    }
}

static inline struct ow_engine_usb_blk_v1 *
get_usb_blk_v1 (struct libusb_transfer *xfr, int n)
{
  return (struct ow_engine_usb_blk_v1 *)
    libusb_get_iso_packet_buffer_simple (xfr, n);
}

static inline uint32_t
get_usb_blk_counter_v1 (struct ow_engine_usb_blk_v1 *blk)
{
  return (uint32_t) blk->counter[0] << 24 | (uint32_t) blk->counter[1] << 16
    | (uint32_t) blk->counter[2] << 8 | blk->counter[3];
}

static inline void
set_usb_blk_counter_v1 (struct ow_engine_usb_blk_v1 *blk, uint32_t counter)
{
  blk->counter[0] = counter >> 24;
  blk->counter[1] = counter >> 16;
  blk->counter[2] = counter >> 8;
  blk->counter[3] = counter;
}

static void
push_audio_in_frames_v1 (struct ow_engine *engine, uint8_t frames)
{
  unsigned int used = engine->usb.audio_in_frames_written -
    engine->usb.audio_in_frames_read;

  if (used == OB1_FRAMES_RING_LEN)
    {
      error_print ("h2o: Too many received blocks. Discarding one...");
      engine->usb.audio_in_frames_read++;
    }

  engine->usb.audio_in_frames[engine->usb.audio_in_frames_written %
			      OB1_FRAMES_RING_LEN] = frames;
  engine->usb.audio_in_frames_written++;
}

static uint8_t
pop_audio_in_frames_v1 (struct ow_engine *engine)
{
  uint8_t frames;

  if (engine->usb.audio_in_frames_read == engine->usb.audio_in_frames_written)
    {
      if (engine->usb.audio_out_frames_missing % OB1_ERROR_REPORT_PERIOD == 0)
	{
	  error_print
	    ("h2o: No received block to take the frames from. Using %d frames... (%u times)",
	     OB1_FRAMES_PER_BLOCK, engine->usb.audio_out_frames_missing + 1);
	}
      engine->usb.audio_out_frames_missing++;
      return OB1_FRAMES_PER_BLOCK;
    }

  frames = engine->usb.audio_in_frames[engine->usb.audio_in_frames_read %
				       OB1_FRAMES_RING_LEN];
  engine->usb.audio_in_frames_read++;
  return frames;
}

//As the frames of the received blocks are sent back to the device, this sets
//the lag between both directions. The blocks of all the queued transfers are
//filled before receiving any block and an additional transfer is needed to
//not depend on the order of the callbacks.
static void
reset_audio_in_frames_v1 (struct ow_engine *engine)
{
  engine->usb.audio_in_frames_read = 0;
  engine->usb.audio_in_frames_written = 0;
  for (int i = 0; i < (OB1_TRANSFERS + 1) * engine->blocks_per_transfer; i++)
    {
      push_audio_in_frames_v1 (engine, OB1_FRAMES_PER_BLOCK);
    }
}

uint32_t
ow_engine_read_usb_input_blocks_v1 (struct ow_engine *engine,
				    struct libusb_transfer *xfr)
{
  uint8_t *s;
  uint32_t frames, counter, total = 0;
  struct ow_engine_usb_blk_v1 *blk;
  struct libusb_iso_packet_descriptor *desc;
  float *f = engine->o2h_transfer_buf;
  int outputs = engine->device->desc.outputs;
  int size = engine->device->desc.output_tracks[0].size;

  for (int i = 0; i < xfr->num_iso_packets; i++)
    {
      desc = &xfr->iso_packet_desc[i];
      blk = get_usb_blk_v1 (xfr, i);

      //The first blocks of a stream are empty. These are replaced by silence
      //and Overbridge sends them back as blocks of 48 frames.
      if (desc->status == LIBUSB_TRANSFER_COMPLETED && !desc->actual_length)
	{
	  debug_print (2, "o2h: Empty USB block. Using silence...");
	  engine->usb.audio_in_frames_counter_valid = 0;

	  frames = OB1_FRAMES_PER_BLOCK;
	  memset (f, 0, frames * engine->o2h_audio_frame_size);
	  f += frames * outputs;
	}
      else if (desc->status != LIBUSB_TRANSFER_COMPLETED ||
	       desc->actual_length < sizeof (struct ow_engine_usb_blk_v1) ||
	       blk->frames > OB1_MAX_FRAMES_PER_BLOCK ||
	       desc->actual_length != sizeof (struct ow_engine_usb_blk_v1) +
	       blk->frames * engine->o2h_frame_size)
	{
	  if (engine->usb.audio_in_errors % OB1_ERROR_REPORT_PERIOD == 0)
	    {
	      error_print
		("o2h: Invalid USB block (%s, %u B). Using silence... (%u errors)",
		 libusb_error_name (desc->status), desc->actual_length,
		 engine->usb.audio_in_errors + 1);
	      if (desc->status == LIBUSB_TRANSFER_OVERFLOW)
		{
		  error_print
		    ("o2h: The device sent more data than expected. The active layout might not have been set.");
		}
	    }
	  engine->usb.audio_in_errors++;
	  engine->usb.audio_in_frames_counter_valid = 0;

	  frames = OB1_FRAMES_PER_BLOCK;
	  memset (f, 0, frames * engine->o2h_audio_frame_size);
	  f += frames * outputs;
	}
      else
	{
	  frames = blk->frames;

	  counter = get_usb_blk_counter_v1 (blk);
	  if (engine->usb.audio_in_frames_counter_valid &&
	      counter - engine->usb.audio_in_frames_counter_v1 != frames)
	    {
	      debug_print (2,
			   "o2h: Unexpected frame counter (0x%08x after 0x%08x with %u frames)",
			   counter, engine->usb.audio_in_frames_counter_v1,
			   frames);
	    }
	  engine->usb.audio_in_frames_counter_v1 = counter;
	  engine->usb.audio_in_frames_counter_valid = 1;

	  s = blk->data;
	  for (int j = 0; j < frames * outputs; j++, f++, s += size)
	    {
	      *f = get_sample_v1 (s, size);
	    }
	}

      push_audio_in_frames_v1 (engine, frames);
      total += frames;
    }

  return total;
}

uint32_t
ow_engine_set_usb_output_frames_v1 (struct ow_engine *engine,
				    struct libusb_transfer *xfr)
{
  uint32_t total = 0;
  struct ow_engine_usb_blk_v1 *blk;

  for (int i = 0; i < xfr->num_iso_packets; i++)
    {
      blk = get_usb_blk_v1 (xfr, i);
      blk->frames = pop_audio_in_frames_v1 (engine);
      total += blk->frames;
    }

  return total;
}

void
ow_engine_write_usb_output_blocks_v1 (struct ow_engine *engine,
				      struct libusb_transfer *xfr)
{
  uint8_t *s, *end;
  struct ow_engine_usb_blk_v1 *blk;
  float *f = engine->h2o_transfer_buf;
  int inputs = engine->device->desc.inputs;
  int size = engine->device->desc.input_tracks[0].size;

  for (int i = 0; i < xfr->num_iso_packets; i++)
    {
      blk = get_usb_blk_v1 (xfr, i);
      end = (uint8_t *) blk + xfr->iso_packet_desc[i].length;

      engine->usb.audio_frames_counter_v1 += blk->frames;
      blk->msg_offset = OB1_OUT_MSG_NONE;
      set_usb_blk_counter_v1 (blk, engine->usb.audio_frames_counter_v1);

      s = blk->data;
      memset (s, 0, OB1_OUT_MSG_LEN);
      s += OB1_OUT_MSG_LEN;

      for (int j = 0; j < blk->frames * inputs; j++, f++, s += size)
	{
	  set_sample_v1 (s, size, *f);
	}

      memset (s, 0, end - s);
    }
}

void
ow_engine_print_usb_block_v1 (struct ow_engine *engine,
			      struct libusb_transfer *xfr, int o2h,
			      uint32_t *debug_counter)
{
  uint8_t *s;
  int frames;
  size_t frame_size;
  struct ow_engine_usb_blk_v1 *blk;

  if (debug_level >= 3)
    {
      if (*debug_counter == 0)
	{
	  blk = get_usb_blk_v1 (xfr, 0);
	  frames = blk->frames > OB1_MAX_FRAMES_PER_BLOCK ?
	    OB1_MAX_FRAMES_PER_BLOCK : blk->frames;
	  frame_size = o2h ? engine->o2h_frame_size : engine->h2o_frame_size;
	  s = o2h ? blk->data : blk->data + OB1_OUT_MSG_LEN;

	  fprintf (stderr,
		   "%s block: frames: %d; message offset: 0x%02x; counter: 0x%08x\n",
		   o2h ? "O2H" : "H2O", blk->frames, blk->msg_offset,
		   get_usb_blk_counter_v1 (blk));

	  for (int j = 0; j < frames; j++)
	    {
	      fprintf (stderr, "  Frame %d:", j);

	      for (int k = 0; k < frame_size; k++, s++)
		{
		  fprintf (stderr, " %02x", *s);
		}

	      fprintf (stderr, "\n");
	    }
	}
      *debug_counter += engine->frames_per_transfer;
      if (*debug_counter >= OB_SAMPLE_RATE)
	{
	  *debug_counter = 0;
	}
    }
}

static int
submit_transfer_v1 (struct ow_engine *engine, struct libusb_transfer *xfr,
		    int direction)
{
  int err = libusb_submit_transfer (xfr);
  if (err)
    {
      error_print ("%s: Error when submitting USB audio transfer: %s",
		   OB1_DIRECTION_NAMES[direction], libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
      return err;
    }

  engine->usb.xfrs_in_flight++;
  return 0;
}

static void
set_usb_output_data_blks_v1 (struct ow_engine *engine,
			     struct libusb_transfer *xfr)
{
  uint32_t frames = ow_engine_set_usb_output_frames_v1 (engine, xfr);
  //The blocks are always written as the frame counter needs to be updated.
  read_h2o_audio (engine, frames);
  ow_engine_write_usb_output_blocks_v1 (engine, xfr);
}

static void LIBUSB_CALL
cb_xfr_audio_in_v1 (struct libusb_transfer *xfr)
{
  static uint32_t debug_counter = 0;
  uint32_t frames;
  ow_engine_status_t status;
  struct ow_engine *engine = xfr->user_data;

  engine->usb.xfrs_in_flight--;

  if (xfr->status != LIBUSB_TRANSFER_CANCELLED)
    {
      //The blocks of a failed transfer are handled as invalid blocks, so
      //their frames are still taken into account by the DLL and sent back to
      //the device.
      if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
	{
	  error_print ("o2h: Error on USB audio transfer: %s",
		       libusb_error_name (xfr->status));
	  for (int i = 0; i < xfr->num_iso_packets; i++)
	    {
	      xfr->iso_packet_desc[i].status = LIBUSB_TRANSFER_ERROR;
	      xfr->iso_packet_desc[i].actual_length = 0;
	    }
	}

      frames = ow_engine_read_usb_input_blocks_v1 (engine, xfr);
      if (ow_engine_is_option (engine, OW_ENGINE_OPTION_O2H_AUDIO))
	{
	  ow_engine_print_usb_block_v1 (engine, xfr, 1, &debug_counter);
	  status = update_o2h_dll (engine, frames);
	  write_o2h_audio (engine, status, frames);
	  engine->usb.audio_in_dll_updates++;
	}
    }

  if (ow_engine_get_status (engine) > OW_ENGINE_STATUS_STOP)
    {
      submit_transfer_v1 (engine, xfr, OB1_O2H);
    }
}

static void LIBUSB_CALL
cb_xfr_audio_out_v1 (struct libusb_transfer *xfr)
{
  static uint32_t debug_counter = 0;
  struct ow_engine *engine = xfr->user_data;

  engine->usb.xfrs_in_flight--;

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      for (int i = 0; i < xfr->num_iso_packets; i++)
	{
	  enum libusb_transfer_status s = xfr->iso_packet_desc[i].status;
	  if (s != LIBUSB_TRANSFER_COMPLETED)
	    {
	      if (engine->usb.audio_out_errors % OB1_ERROR_REPORT_PERIOD == 0)
		{
		  error_print ("h2o: Error on USB block: %s (%u errors)",
			       libusb_error_name (s),
			       engine->usb.audio_out_errors + 1);
		}
	      engine->usb.audio_out_errors++;
	    }
	}
    }
  else if (xfr->status != LIBUSB_TRANSFER_CANCELLED)
    {
      error_print ("h2o: Error on USB audio transfer: %s",
		   libusb_error_name (xfr->status));
    }

  if (ow_engine_get_status (engine) > OW_ENGINE_STATUS_STOP)
    {
      set_usb_output_data_blks_v1 (engine, xfr);
      ow_engine_print_usb_block_v1 (engine, xfr, 0, &debug_counter);
      submit_transfer_v1 (engine, xfr, OB1_H2O);
    }
}

static const char *
get_track_code_name_v1 (uint8_t code, char *buf, size_t len)
{
  if (code == 0xa0)
    {
      return "Main L";
    }
  if (code == 0xa1)
    {
      return "Main R";
    }
  if (code == 0x20)
    {
      return "Main L Input";
    }
  if (code == 0x21)
    {
      return "Main R Input";
    }
  if (code == 0xc0)
    {
      return "Input L";
    }
  if (code == 0xc1)
    {
      return "Input R";
    }
  if (code >= 0x80 && code < 0xa0)
    {
      snprintf (buf, len, "Synth Track %d", code - 0x80 + 1);
      return buf;
    }
  if (code < 0x20)
    {
      snprintf (buf, len, "Synth Track %d Input", code + 1);
      return buf;
    }
  return "Unknown";
}

static int
get_layout_tracks_v1 (struct ow_engine *engine, int direction)
{
  int tracks = 0;
  for (int i = 0; i < OB1_MAP_LEN; i++)
    {
      if (engine->usb.layout_map[direction][i] != OB1_MAP_EMPTY)
	{
	  tracks++;
	}
    }
  return tracks;
}

static void
print_layout_v1 (struct ow_engine *engine, int direction, int error)
{
  char buf[OW_LABEL_MAX_LEN];
  uint8_t *map = engine->usb.layout_map[direction];

  for (int i = 0; i < OB1_MAP_LEN; i++)
    {
      if (map[i] == OB1_MAP_EMPTY)
	{
	  continue;
	}
      //The code is printed too as the names are the Analog Four ones.
      if (error)
	{
	  error_print ("%s: Device track %d: %s (0x%02x)",
		       OB1_DIRECTION_NAMES[direction], i + 1,
		       get_track_code_name_v1 (map[i], buf,
					       OW_LABEL_MAX_LEN), map[i]);
	}
      else
	{
	  debug_print (1, "%s: Device track %d: %s (0x%02x)",
		       OB1_DIRECTION_NAMES[direction], i + 1,
		       get_track_code_name_v1 (map[i], buf,
					       OW_LABEL_MAX_LEN), map[i]);
	}
    }
}

int
ow_engine_check_layout_v1 (struct ow_engine *engine)
{
  int err = 0;
  struct ow_device_desc *desc = &engine->device->desc;
  int tracks[] = { desc->outputs, desc->inputs };
  int sizes[] = { desc->output_tracks[0].size, desc->input_tracks[0].size };

  for (int i = 0; i < 2; i++)
    {
      int layout_tracks = get_layout_tracks_v1 (engine, i);
      int layout_depth = engine->usb.layout_depth[i];

      debug_print (1, "%s: Device layout: %d tracks of %d bits",
		   OB1_DIRECTION_NAMES[i], layout_tracks, layout_depth);

      if (layout_tracks != tracks[i] || layout_depth != sizes[i] * 8)
	{
	  error_print
	    ("%s: The device is set to %d tracks of %d bits but its definition has %d tracks of %d bits. Change the layout in the Overbridge Control Panel or the device definition.",
	     OB1_DIRECTION_NAMES[i], layout_tracks, layout_depth, tracks[i],
	     sizes[i] * 8);
	  print_layout_v1 (engine, i, 1);
	  err = -EINVAL;
	}
      else
	{
	  print_layout_v1 (engine, i, 0);
	}
    }

  return err;
}

static int
read_layout_v1 (struct ow_engine *engine)
{
  int res;
  int interfaces[] = { engine->usb.audio_in_interface,
    engine->usb.audio_out_interface
  };

  for (int i = 0; i < 2; i++)
    {
      res = libusb_control_transfer (engine->usb.device_handle,
				     LIBUSB_ENDPOINT_IN |
				     LIBUSB_REQUEST_TYPE_VENDOR |
				     LIBUSB_RECIPIENT_INTERFACE,
				     OB1_USB_REQ_GET_DEPTH, 0, interfaces[i],
				     &engine->usb.layout_depth[i], 1,
				     OB1_USB_CONTROL_TIMEOUT);
      if (res != 1)
	{
	  error_print ("%s: Error while reading the bit depth: %s",
		       OB1_DIRECTION_NAMES[i],
		       res < 0 ? libusb_error_name (res) : "short read");
	  return res < 0 ? res : LIBUSB_ERROR_IO;
	}

      res = libusb_control_transfer (engine->usb.device_handle,
				     LIBUSB_ENDPOINT_IN |
				     LIBUSB_REQUEST_TYPE_VENDOR |
				     LIBUSB_RECIPIENT_INTERFACE,
				     OB1_USB_REQ_GET_MAP, 0, interfaces[i],
				     engine->usb.layout_map[i], OB1_MAP_LEN,
				     OB1_USB_CONTROL_TIMEOUT);
      if (res != OB1_MAP_LEN)
	{
	  error_print ("%s: Error while reading the channel map: %s",
		       OB1_DIRECTION_NAMES[i],
		       res < 0 ? libusb_error_name (res) : "short read");
	  return res < 0 ? res : LIBUSB_ERROR_IO;
	}
    }

  return 0;
}

//Like the Overbridge Control Panel, this stores the layout in the device.
static int
write_stored_layout_v1 (struct ow_engine *engine,
			uint8_t map[2][OB1_MAP_LEN], uint8_t depth[2])
{
  int res;
  int interfaces[] = { engine->usb.audio_in_interface,
    engine->usb.audio_out_interface
  };

  for (int i = 0; i < 2; i++)
    {
      res = libusb_control_transfer (engine->usb.device_handle,
				     LIBUSB_ENDPOINT_OUT |
				     LIBUSB_REQUEST_TYPE_VENDOR |
				     LIBUSB_RECIPIENT_INTERFACE,
				     OB1_USB_REQ_SET_STORED_MAP, 0,
				     interfaces[i], map[i], OB1_MAP_LEN,
				     OB1_USB_CONTROL_TIMEOUT);
      if (res != OB1_MAP_LEN)
	{
	  error_print ("%s: Error while storing the channel map: %s",
		       OB1_DIRECTION_NAMES[i],
		       res < 0 ? libusb_error_name (res) : "short write");
	  return res < 0 ? res : LIBUSB_ERROR_IO;
	}

      res = libusb_control_transfer (engine->usb.device_handle,
				     LIBUSB_ENDPOINT_OUT |
				     LIBUSB_REQUEST_TYPE_VENDOR |
				     LIBUSB_RECIPIENT_INTERFACE,
				     OB1_USB_REQ_SET_STORED_DEPTH, depth[i],
				     interfaces[i], NULL, 0,
				     OB1_USB_CONTROL_TIMEOUT);
      if (res)
	{
	  error_print ("%s: Error while storing the bit depth: %s",
		       OB1_DIRECTION_NAMES[i], libusb_error_name (res));
	  return res;
	}
    }

  return 0;
}

//Returns 1 and the layout when the device description defines it with track
//codes, 0 when it does not and -EINVAL if it is not valid.
int
ow_engine_get_desc_layout_v1 (const struct ow_device_desc *desc,
			      uint8_t map[2][OB1_MAP_LEN], uint8_t depth[2])
{
  int tracks[] = { desc->outputs, desc->inputs };
  const struct ow_device_track *track[] = { desc->output_tracks,
    desc->input_tracks
  };

  //Codes are either set for all the tracks or for none.
  if (track[OB1_O2H][0].code == OW_TRACK_CODE_NONE)
    {
      return 0;
    }

  for (int i = 0; i < 2; i++)
    {
      if (tracks[i] > OB1_MAP_LEN)
	{
	  error_print ("%s: Too many tracks (%d > %d)",
		       OB1_DIRECTION_NAMES[i], tracks[i], OB1_MAP_LEN);
	  return -EINVAL;
	}

      memset (map[i], OB1_MAP_EMPTY, OB1_MAP_LEN);
      for (int j = 0; j < tracks[i]; j++)
	{
	  map[i][j] = track[i][j].code;
	}
      depth[i] = track[i][0].size * 8;
    }

  return 1;
}

//The stored layout is written as the active layout.
static int
write_layout_v1 (struct ow_engine *engine)
{
  int res;
  int interfaces[] = { engine->usb.audio_in_interface,
    engine->usb.audio_out_interface
  };

  for (int i = 0; i < 2; i++)
    {
      res = libusb_control_transfer (engine->usb.device_handle,
				     LIBUSB_ENDPOINT_OUT |
				     LIBUSB_REQUEST_TYPE_VENDOR |
				     LIBUSB_RECIPIENT_INTERFACE,
				     OB1_USB_REQ_SET_MAP, 0, interfaces[i],
				     engine->usb.layout_map[i], OB1_MAP_LEN,
				     OB1_USB_CONTROL_TIMEOUT);
      if (res != OB1_MAP_LEN)
	{
	  error_print ("%s: Error while writing the channel map: %s",
		       OB1_DIRECTION_NAMES[i],
		       res < 0 ? libusb_error_name (res) : "short write");
	  return res < 0 ? res : LIBUSB_ERROR_IO;
	}
    }

  for (int i = 0; i < 2; i++)
    {
      res = libusb_control_transfer (engine->usb.device_handle,
				     LIBUSB_ENDPOINT_OUT |
				     LIBUSB_REQUEST_TYPE_VENDOR |
				     LIBUSB_RECIPIENT_INTERFACE,
				     OB1_USB_REQ_SET_DEPTH,
				     engine->usb.layout_depth[i],
				     interfaces[i], NULL, 0,
				     OB1_USB_CONTROL_TIMEOUT);
      if (res)
	{
	  error_print ("%s: Error while writing the bit depth: %s",
		       OB1_DIRECTION_NAMES[i], libusb_error_name (res));
	  return res;
	}
    }

  return 0;
}

int
ow_engine_get_alt_setting_v1 (const struct libusb_config_descriptor *config,
			      int interface, uint8_t endpoint, size_t size,
			      int *max_packet_size)
{
  int alt_setting = -1;

  *max_packet_size = 0;

  for (int i = 0; i < config->bNumInterfaces; i++)
    {
      const struct libusb_interface *iface = &config->interface[i];
      for (int j = 0; j < iface->num_altsetting; j++)
	{
	  const struct libusb_interface_descriptor *alt =
	    &iface->altsetting[j];
	  if (alt->bInterfaceNumber != interface)
	    {
	      continue;
	    }

	  for (int k = 0; k < alt->bNumEndpoints; k++)
	    {
	      const struct libusb_endpoint_descriptor *ep = &alt->endpoint[k];
	      int max = ep->wMaxPacketSize & 0x7ff;
	      if (ep->bEndpointAddress == endpoint && max >= size &&
		  (alt_setting < 0 || max < *max_packet_size))
		{
		  alt_setting = alt->bAlternateSetting;
		  *max_packet_size = max;
		}
	    }
	}
    }

  return alt_setting;
}

//Writing the active layout is needed on every stream start and Overbridge
//restarts the frame counter too. The stream is started right before handling
//the USB events to not leave the isochronous transfers unattended.
static int
start_stream_v1 (struct ow_engine *engine)
{
  int err;

  err = write_layout_v1 (engine);
  if (err)
    {
      return err;
    }

  engine->usb.audio_frames_counter_v1 = 0;
  engine->usb.audio_in_frames_counter_valid = 0;
  engine->usb.audio_in_dll_updates = 0;
  reset_audio_in_frames_v1 (engine);

  err = libusb_set_interface_alt_setting (engine->usb.device_handle,
					  engine->usb.audio_in_interface,
					  engine->usb.audio_in_alt_setting);
  if (err)
    {
      error_print ("o2h: Error while setting the alternate setting: %s",
		   libusb_error_name (err));
      return err;
    }

  err = libusb_set_interface_alt_setting (engine->usb.device_handle,
					  engine->usb.audio_out_interface,
					  engine->usb.audio_out_alt_setting);
  if (err)
    {
      error_print ("h2o: Error while setting the alternate setting: %s",
		   libusb_error_name (err));
      return err;
    }

  debug_print (1, "Starting stream (alternate settings %d and %d)...",
	       engine->usb.audio_in_alt_setting,
	       engine->usb.audio_out_alt_setting);

  for (int i = 0; i < OB1_TRANSFERS; i++)
    {
      set_usb_output_data_blks_v1 (engine, engine->usb.xfr_audio_out_v1[i]);
      err = submit_transfer_v1 (engine, engine->usb.xfr_audio_out_v1[i],
				OB1_H2O);
      if (err)
	{
	  return err;
	}
      err = submit_transfer_v1 (engine, engine->usb.xfr_audio_in_v1[i],
				OB1_O2H);
      if (err)
	{
	  return err;
	}
    }

  return 0;
}

static void
stop_stream_v1 (struct ow_engine *engine)
{
  struct timeval tv = { 0, 100000 };

  //No new transfers will be submitted due to the status but the queued ones
  //are cancelled and all their callbacks must run before freeing them.
  debug_print (2, "Cancelling the queued transfers...");
  for (int i = 0; i < OB1_TRANSFERS; i++)
    {
      libusb_cancel_transfer (engine->usb.xfr_audio_in_v1[i]);
      libusb_cancel_transfer (engine->usb.xfr_audio_out_v1[i]);
    }

  for (int i = 0; i < 10 && engine->usb.xfrs_in_flight > 0; i++)
    {
      libusb_handle_events_timeout_completed (engine->usb.context, &tv, NULL);
    }

  if (engine->usb.xfrs_in_flight > 0)
    {
      error_print ("%d USB transfers could not be cancelled",
		   engine->usb.xfrs_in_flight);
    }

  debug_print (1, "Stopping stream...");
  libusb_set_interface_alt_setting (engine->usb.device_handle,
				    engine->usb.audio_in_interface, 0);
  libusb_set_interface_alt_setting (engine->usb.device_handle,
				    engine->usb.audio_out_interface, 0);
}

static struct libusb_transfer *
alloc_iso_transfer_v1 (struct ow_engine *engine, unsigned char endpoint,
		       size_t blk_len, libusb_transfer_cb_fn cb)
{
  uint8_t *buf;
  struct libusb_transfer *xfr =
    libusb_alloc_transfer (engine->blocks_per_transfer);

  if (!xfr)
    {
      return NULL;
    }

  buf = calloc (engine->blocks_per_transfer, blk_len);
  if (!buf)
    {
      libusb_free_transfer (xfr);
      return NULL;
    }

  //Isochronous transfers do not need a timeout.
  libusb_fill_iso_transfer (xfr, engine->usb.device_handle, endpoint, buf,
			    engine->blocks_per_transfer * blk_len,
			    engine->blocks_per_transfer, cb, engine, 0);
  xfr->flags = LIBUSB_TRANSFER_FREE_BUFFER;
  libusb_set_iso_packet_lengths (xfr, blk_len);

  return xfr;
}

//The common part of the memory initialization of every version. All the
//pointers are reset so ow_engine_free_mem can always be used on failure.
static void
init_mem_start (struct ow_engine *engine, unsigned int blocks_per_transfer,
		unsigned int min_blocks, unsigned int max_blocks,
		unsigned int default_blocks, unsigned int frames_per_block)
{
  engine->context = NULL;
  engine->h2o_transfer_buf = NULL;
  engine->o2h_transfer_buf = NULL;
  engine->h2o_resampler_buf = NULL;
  engine->usb.xfr_audio_in_data = NULL;
  engine->usb.xfr_audio_out_data = NULL;
  engine->usb.xfr_control_out_data = NULL;
  engine->usb.xfr_control_in_data = NULL;
  for (int i = 0; i < OB1_TRANSFERS; i++)
    {
      engine->usb.xfr_audio_in_v1[i] = NULL;
      engine->usb.xfr_audio_out_v1[i] = NULL;
    }

  pthread_spin_init (&engine->lock, PTHREAD_PROCESS_SHARED);

  engine->blocks_per_transfer =
    ow_engine_get_valid_blocks_per_transfer (blocks_per_transfer, min_blocks,
					     max_blocks, default_blocks);
  engine->frames_per_block = frames_per_block;

  engine->frames_per_transfer =
    engine->frames_per_block * engine->blocks_per_transfer;

  engine->o2h_frame_size =
    ow_get_frame_size_from_desc_tracks (engine->device->desc.outputs,
					engine->device->desc.output_tracks);

  engine->h2o_frame_size =
    ow_get_frame_size_from_desc_tracks (engine->device->desc.inputs,
					engine->device->desc.input_tracks);

  debug_print (2, "o2h: USB in frame size: %zu B", engine->o2h_frame_size);
  debug_print (2, "h2o: USB out frame size: %zu B", engine->h2o_frame_size);

  engine->o2h_audio_frame_size =
    engine->device->desc.outputs * OW_BYTES_PER_SAMPLE;
  engine->h2o_audio_frame_size =
    engine->device->desc.inputs * OW_BYTES_PER_SAMPLE;
}

//The audio transfer buffers, which need the transfer sizes, the h2o resampler
//and the control buffers.
static ow_err_t
init_mem_audio (struct ow_engine *engine)
{
  debug_print (2, "o2h: audio transfer size: %zu B",
	       engine->o2h_transfer_size);
  debug_print (2, "h2o: audio transfer size: %zu B",
	       engine->h2o_transfer_size);

  engine->latency_o2h_min = engine->frames_per_transfer;
  engine->latency_h2o_min = engine->frames_per_transfer;

  engine->reading_at_h2o_end = 0;

  engine->h2o_transfer_buf = calloc (1, engine->h2o_transfer_size);
  engine->o2h_transfer_buf = calloc (1, engine->o2h_transfer_size);
  engine->h2o_resampler_buf = calloc (1, engine->h2o_transfer_size);
  engine->usb.xfr_control_out_data = malloc (USB_CONTROL_LEN);
  engine->usb.xfr_control_in_data = malloc (OB_NAME_MAX_LEN);
  if (!engine->h2o_transfer_buf || !engine->o2h_transfer_buf ||
      !engine->h2o_resampler_buf || !engine->usb.xfr_control_out_data ||
      !engine->usb.xfr_control_in_data)
    {
      error_print ("Error while allocating memory");
      return OW_GENERIC_ERROR;
    }

  //h2o resampler
  engine->h2o_data.data_in = engine->h2o_resampler_buf;
  engine->h2o_data.data_out = engine->h2o_transfer_buf;
  engine->h2o_data.end_of_input = 1;
  engine->h2o_data.input_frames = engine->frames_per_transfer;
  engine->h2o_data.output_frames = engine->frames_per_transfer;

  return OW_OK;
}

// The common part of the USB initialization of every version
static ow_err_t
ow_engine_init_usb (struct ow_engine *engine, struct ow_device *device,
		    const struct ow_engine_ops *ops, int audio_in_interface,
		    int audio_out_interface, unsigned int blocks_per_transfer,
		    unsigned int xfr_timeout)
{
  engine->ops = ops;
  engine->status = OW_ENGINE_STATUS_STOP;
  engine->device = device;
  engine->overbridge_name[0] = '\0';
  engine->usb.audio_in_interface = audio_in_interface;
  engine->usb.audio_out_interface = audio_out_interface;

  engine->usb.xfr_control_in = NULL;
  engine->usb.xfr_control_out = NULL;
  engine->usb.xfr_control_in_data = NULL;
  engine->usb.xfr_control_out_data = NULL;

  engine->usb.xfr_timeout = xfr_timeout;
  debug_print (1, "USB transfer timeout: %u", engine->usb.xfr_timeout);

  // V1
  for (int i = 0; i < OB1_TRANSFERS; i++)
    {
      engine->usb.xfr_audio_in_v1[i] = NULL;
      engine->usb.xfr_audio_out_v1[i] = NULL;
    }
  engine->usb.xfrs_in_flight = 0;
  engine->usb.detached_count = 0;

  // V2
  engine->usb.xfr_audio_in = NULL;
  engine->usb.xfr_audio_out = NULL;
  engine->usb.xfr_audio_in_data = NULL;
  engine->usb.xfr_audio_out_data = NULL;

  // V3
  for (int i = 0; i < OW_ISO_URBS; i++)
    {
      engine->usb.xfr_audio_in_q[i] = NULL;
      engine->usb.xfr_audio_out_q[i] = NULL;
      engine->usb.xfr_audio_in_data_q[i] = NULL;
      engine->usb.xfr_audio_out_data_q[i] = NULL;
    }

  return ops->init (engine, device, blocks_per_transfer);
}

//The kernel drivers are attached again on every path, failures included, to
//not leave the device without them (e.g. MIDI). The drivers of the audio
//interfaces can only be attached once these are released.
static void
attach_kernel_drivers (struct ow_engine *engine, int audio_interfaces)
{
  int remaining = 0;

  for (int i = 0; i < engine->usb.detached_count; i++)
    {
      int err, interface = engine->usb.detached[i];

      if (!audio_interfaces &&
	  (interface == engine->usb.audio_in_interface ||
	   interface == engine->usb.audio_out_interface))
	{
	  engine->usb.detached[remaining++] = interface;
	  continue;
	}

      //A driver might take several interfaces when attached to one of them.
      if (libusb_kernel_driver_active (engine->usb.device_handle,
				       interface) == 1)
	{
	  continue;
	}

      err = libusb_attach_kernel_driver (engine->usb.device_handle,
					 interface);
      if (err)
	{
	  error_print
	    ("Error while attaching the kernel driver to interface %d: %s",
	     interface, libusb_error_name (err));
	}
    }

  engine->usb.detached_count = remaining;
}

int
ow_engine_init_mem_v1 (struct ow_engine *engine,
		       unsigned int blocks_per_transfer)
{
  size_t size;
  ow_err_t ret;

  init_mem_start (engine, blocks_per_transfer, OW1_MIN_BLOCKS, OW1_MAX_BLOCKS,
		  OW1_DEFAULT_BLOCKS, OB1_FRAMES_PER_BLOCK);

  size = OB1_USB_AUDIO_IN_BLK_LEN (engine->o2h_frame_size);
  if (engine->usb.audio_in_blk_len && engine->usb.audio_in_blk_len < size)
    {
      error_print ("Unexpected audio block size (%zu < %zu)",
		   engine->usb.audio_in_blk_len, size);
      ret = OW_USB_UNEXPECTED_PACKET_SIZE;
      goto error;
    }
  else if (!engine->usb.audio_in_blk_len)
    {
      engine->usb.audio_in_blk_len = size;
    }

  size = OB1_USB_AUDIO_OUT_BLK_LEN (engine->h2o_frame_size);
  if (engine->usb.audio_out_blk_len && engine->usb.audio_out_blk_len < size)
    {
      error_print ("Unexpected audio block size (%zu < %zu)",
		   engine->usb.audio_out_blk_len, size);
      ret = OW_USB_UNEXPECTED_PACKET_SIZE;
      goto error;
    }
  else if (!engine->usb.audio_out_blk_len)
    {
      engine->usb.audio_out_blk_len = size;
    }

  debug_print (2, "o2h: USB in block size: %zu B",
	       engine->usb.audio_in_blk_len);
  debug_print (2, "h2o: USB out block size: %zu B",
	       engine->usb.audio_out_blk_len);

  //The transfer buffers have space for the maximum amount of frames.
  engine->o2h_transfer_size = engine->blocks_per_transfer *
    OB1_MAX_FRAMES_PER_BLOCK * engine->o2h_audio_frame_size;
  engine->h2o_transfer_size = engine->blocks_per_transfer *
    OB1_MAX_FRAMES_PER_BLOCK * engine->h2o_audio_frame_size;

  engine->usb.xfrs_in_flight = 0;
  engine->usb.audio_frames_counter_v1 = 0;
  engine->usb.audio_in_frames_counter_valid = 0;
  engine->usb.audio_in_errors = 0;
  engine->usb.audio_out_errors = 0;
  engine->usb.audio_out_frames_missing = 0;
  reset_audio_in_frames_v1 (engine);

  ret = init_mem_audio (engine);
  if (ret)
    {
      goto error;
    }

  for (int i = 0; i < OB1_TRANSFERS; i++)
    {
      engine->usb.xfr_audio_in_v1[i] =
	alloc_iso_transfer_v1 (engine, OB1_USB_AUDIO_IN_EP,
			       engine->usb.audio_in_blk_len,
			       cb_xfr_audio_in_v1);
      engine->usb.xfr_audio_out_v1[i] =
	alloc_iso_transfer_v1 (engine, OB1_USB_AUDIO_OUT_EP,
			       engine->usb.audio_out_blk_len,
			       cb_xfr_audio_out_v1);
      if (!engine->usb.xfr_audio_in_v1[i] || !engine->usb.xfr_audio_out_v1[i])
	{
	  ret = OW_USB_ERROR_CANT_PREPARE_TRANSFER;
	  goto error;
	}
    }

  return OW_OK;

error:
  for (int i = 0; i < OB1_TRANSFERS; i++)
    {
      libusb_free_transfer (engine->usb.xfr_audio_in_v1[i]);
      libusb_free_transfer (engine->usb.xfr_audio_out_v1[i]);
      engine->usb.xfr_audio_in_v1[i] = NULL;
      engine->usb.xfr_audio_out_v1[i] = NULL;
    }
  ow_engine_free_mem (engine);
  return ret;
}

static void
free_transfers_v1 (struct ow_engine *engine, int closed)
{
  for (int i = 0; i < OB1_TRANSFERS; i++)
    {
      struct libusb_transfer *xfrs[] = { engine->usb.xfr_audio_in_v1[i],
	engine->usb.xfr_audio_out_v1[i]
      };

      for (int j = 0; j < 2; j++)
	{
	  //After closing the device, the transfers can not reference it.
	  if (closed && xfrs[j])
	    {
	      xfrs[j]->dev_handle = NULL;
	    }
	  libusb_free_transfer (xfrs[j]);
	}

      engine->usb.xfr_audio_in_v1[i] = NULL;
      engine->usb.xfr_audio_out_v1[i] = NULL;
    }
}

static void
usb_shutdown_v1 (struct ow_engine *engine)
{
  //Closing the device detaches the transfers that are still queued so these
  //can only be freed after it.
  int queued = engine->usb.xfrs_in_flight > 0;

  if (!queued)
    {
      free_transfers_v1 (engine, 0);
    }
  libusb_release_interface (engine->usb.device_handle,
			    engine->usb.audio_in_interface);
  libusb_release_interface (engine->usb.device_handle,
			    engine->usb.audio_out_interface);
  attach_kernel_drivers (engine, 1);
  libusb_close (engine->usb.device_handle);
  if (queued)
    {
      free_transfers_v1 (engine, 1);
    }
  libusb_unref_device (engine->usb.device);
  libusb_free_transfer (engine->usb.xfr_control_in);
  libusb_free_transfer (engine->usb.xfr_control_out);
  libusb_exit (engine->usb.context);
}

static void
usb_shutdown_v2 (struct ow_engine *engine)
{
  libusb_release_interface (engine->usb.device_handle,
			    OB2_USB_AUDIO_IN_INTERFACE);
  libusb_release_interface (engine->usb.device_handle,
			    OB2_USB_AUDIO_OUT_INTERFACE);

  if (engine->usb.xfr_audio_in)
    libusb_free_transfer (engine->usb.xfr_audio_in);
  if (engine->usb.xfr_audio_out)
    libusb_free_transfer (engine->usb.xfr_audio_out);

  if (engine->usb.xfr_control_in)
    libusb_free_transfer (engine->usb.xfr_control_in);
  if (engine->usb.xfr_control_out)
    libusb_free_transfer (engine->usb.xfr_control_out);

  if (engine->usb.device_handle)
    libusb_close (engine->usb.device_handle);
  if (engine->usb.device)
    libusb_unref_device (engine->usb.device);
  if (engine->usb.context)
    libusb_exit (engine->usb.context);
}

static void
usb_shutdown_v3 (struct ow_engine *engine)
{
  libusb_release_interface (engine->usb.device_handle,
			    OB3_USB_AUDIO_IN_INTERFACE);
  libusb_release_interface (engine->usb.device_handle,
			    OB3_USB_AUDIO_IN_INTERFACE);

  for (int i = 0; i < OW_ISO_URBS; i++)
    {
      if (engine->usb.xfr_audio_in_q[i])
	libusb_free_transfer (engine->usb.xfr_audio_in_q[i]);
      if (engine->usb.xfr_audio_out_q[i])
	libusb_free_transfer (engine->usb.xfr_audio_out_q[i]);
      engine->usb.xfr_audio_in_q[i] = NULL;
      engine->usb.xfr_audio_out_q[i] = NULL;
    }

  if (engine->usb.xfr_control_in)
    libusb_free_transfer (engine->usb.xfr_control_in);
  if (engine->usb.xfr_control_out)
    libusb_free_transfer (engine->usb.xfr_control_out);

  if (engine->usb.device_handle)
    libusb_close (engine->usb.device_handle);
  if (engine->usb.device)
    libusb_unref_device (engine->usb.device);
  if (engine->usb.context)
    libusb_exit (engine->usb.context);
}

static void
usb_shutdown (struct ow_engine *engine)
{
  engine->ops->shutdown (engine);
}

unsigned int
ow_engine_get_valid_blocks_per_transfer (unsigned int blocks_per_transfer,
					 unsigned int min, unsigned int max,
					 unsigned int def)
{
  unsigned int v = blocks_per_transfer;
  if (v == 0 || v < min || v > max)
    {
      if (v != 0)
	{
	  error_print
	    ("Blocks per transfer must be in [%u..%u] for this device. Using %u...",
	     min, max, def);
	}
      v = def;
    }

  return v;
}

int
ow_engine_init_mem_v2 (struct ow_engine *engine,
		       unsigned int blocks_per_transfer)
{
  size_t size;
  ow_err_t ret;
  struct ow_engine_usb_blk_v2 *blk;

  init_mem_start (engine, blocks_per_transfer, OW2_MIN_BLOCKS, OW2_MAX_BLOCKS,
		  OW2_DEFAULT_BLOCKS, OB2_FRAMES_PER_BLOCK);

  size = sizeof (struct ow_engine_usb_blk_v2) + engine->frames_per_block *
    engine->o2h_frame_size;
  if (engine->usb.audio_in_blk_len && engine->usb.audio_in_blk_len != size)
    {
      error_print ("Unexpected audio block size (%lu != %zu)",
		   engine->usb.audio_in_blk_len, size);
      ret = OW_USB_UNEXPECTED_PACKET_SIZE;
      goto error;
    }
  else
    {
      engine->usb.audio_in_blk_len = size;
    }

  size = sizeof (struct ow_engine_usb_blk_v2) + engine->frames_per_block *
    engine->h2o_frame_size;
  if (engine->usb.audio_out_blk_len && engine->usb.audio_out_blk_len != size)
    {
      error_print ("Unexpected audio block size (%lu != %zu)",
		   engine->usb.audio_out_blk_len, size);
      ret = OW_USB_UNEXPECTED_PACKET_SIZE;
      goto error;
    }
  else
    {
      engine->usb.audio_out_blk_len = size;
    }

  debug_print (2, "o2h: USB in block size: %zu B",
	       engine->usb.audio_in_blk_len);
  debug_print (2, "h2o: USB out block size: %zu B",
	       engine->usb.audio_out_blk_len);

  engine->o2h_transfer_size = engine->frames_per_transfer *
    engine->o2h_audio_frame_size;
  engine->h2o_transfer_size = engine->frames_per_transfer *
    engine->h2o_audio_frame_size;

  engine->usb.audio_frames_counter = 0;
  engine->usb.xfr_audio_in_data_len =
    engine->usb.audio_in_blk_len * engine->blocks_per_transfer;
  engine->usb.xfr_audio_out_data_len =
    engine->usb.audio_out_blk_len * engine->blocks_per_transfer;
  engine->usb.xfr_audio_in_data =
    calloc (1, engine->usb.xfr_audio_in_data_len);
  engine->usb.xfr_audio_out_data =
    calloc (1, engine->usb.xfr_audio_out_data_len);
  if (!engine->usb.xfr_audio_in_data || !engine->usb.xfr_audio_out_data)
    {
      error_print ("Error while allocating memory");
      ret = OW_GENERIC_ERROR;
      goto error;
    }

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_OUTPUT_USB_BLK_V2 (engine, i);
      blk->header = htobe16 (0x07ff);
    }

  ret = init_mem_audio (engine);
  if (ret)
    {
      goto error;
    }

  return OW_OK;

error:
  ow_engine_free_mem (engine);
  return ret;
}

// These calls are needed to initialize the Overbridge side before the host side.
static void
prepare_v2 (struct ow_engine *engine)
{
  prepare_cycle_in_v2 (engine);
  prepare_cycle_out_v2 (engine);
}

static void
stop_v2_v3 (struct ow_engine *engine)
{
  struct timeval tv = { 1, 0UL };

  //Handle completed events but not actually processed.
  //No new transfers will be submitted due to the status.
  debug_print (2, "Processing remaining events...");
  libusb_handle_events_timeout_completed (engine->usb.context, &tv, NULL);
}

//The resampler waits while booting, so the Overbridge side of the DLL is
//updated before the host side uses it.
static int
start_v1 (struct ow_engine *engine)
{
  int err = start_stream_v1 (engine);
  if (err)
    {
      return err;
    }

  if (engine->context->dll &&
      ow_engine_is_option (engine, OW_ENGINE_OPTION_O2H_AUDIO))
    {
      while (!engine->usb.audio_in_dll_updates &&
	     ow_engine_get_status (engine) == OW_ENGINE_STATUS_BOOT)
	{
	  err = libusb_handle_events_completed (engine->usb.context, NULL);
	  if (err)
	    {
	      error_print ("USB error: %s", libusb_error_name (err));
	    }
	}
    }

  return 0;
}

int
ow_engine_init_mem_v3 (struct ow_engine *engine,
		       unsigned int blocks_per_transfer)
{
  engine->context = NULL;
  pthread_spin_init (&engine->lock, PTHREAD_PROCESS_SHARED);

  engine->blocks_per_transfer = blocks_per_transfer;
  engine->frames_per_block = OB3_IN_FRAMES_PER_PACKET;
  engine->frames_per_transfer =
    engine->frames_per_block * engine->blocks_per_transfer;
  engine->v3_streaming_started = 0;
  if (engine->frames_per_transfer == 0 ||
      engine->frames_per_transfer % OB3_OUT_FRAMES_PER_PACKET != 0)
    {
      error_print
	("v3: transfer must contain a non-zero multiple of %u frames "
	 "(got %u frames from %u IN packets)", OB3_OUT_FRAMES_PER_PACKET,
	 engine->frames_per_transfer, engine->blocks_per_transfer);
      return OW_USB_UNEXPECTED_PACKET_SIZE;
    }

  engine->o2h_frame_size =
    ow_get_frame_size_from_desc_tracks (engine->device->desc.outputs,
					engine->device->desc.output_tracks);
  engine->h2o_frame_size =
    ow_get_frame_size_from_desc_tracks (engine->device->desc.inputs,
					engine->device->desc.input_tracks);

  if (engine->device->desc.outputs > OB3_IN_WIRE_CHANNELS)
    {
      error_print ("v3: descriptor exposes %d O2H channels, wire has %u",
		   engine->device->desc.outputs, OB3_IN_WIRE_CHANNELS);
      return OW_USB_UNEXPECTED_PACKET_SIZE;
    }

  for (int i = 0; i < engine->device->desc.outputs; i++)
    if (engine->device->desc.output_tracks[i].size != 4)
      {
	error_print ("v3: O2H track %d has unsupported sample size %d", i,
		     engine->device->desc.output_tracks[i].size);
	return OW_USB_UNEXPECTED_PACKET_SIZE;
      }

  for (int i = 0; i < engine->device->desc.inputs; i++)
    if (engine->device->desc.input_tracks[i].size != 4)
      {
	error_print ("v3: H2O track %d has unsupported sample size %d", i,
		     engine->device->desc.input_tracks[i].size);
	return OW_USB_UNEXPECTED_PACKET_SIZE;
      }

  engine->usb.audio_in_blk_len = OB3_INPUT_MAX_PACKET_LEN;
  engine->usb.audio_out_blk_len = OB3_OUTPUT_PACKET_LEN;

  engine->o2h_transfer_size =
    engine->frames_per_transfer * engine->device->desc.outputs *
    OW_BYTES_PER_SAMPLE;
  engine->h2o_transfer_size =
    engine->frames_per_transfer * engine->device->desc.inputs *
    OW_BYTES_PER_SAMPLE;

  engine->latency_o2h_min = engine->frames_per_transfer;
  engine->latency_h2o_min = engine->frames_per_transfer;

  /* Counter is the end position of the previously emitted packet. */
  engine->usb.audio_frames_counter = 0;

  /* Three software-delay transfers + three queued OUT URBs = the
   * six-transfer O2H -> H2O lag seen in the official capture. */
  v3_fifo_head = OB3_CLOCK_DELAY_TRANSFERS;
  v3_fifo_tail = 0;
  for (int i = 0; i < 256; i++)
    v3_clock_fifo[i] = engine->frames_per_transfer;

  engine->usb.xfr_audio_in_data_len =
    engine->usb.audio_in_blk_len * engine->blocks_per_transfer;
  engine->usb.xfr_audio_out_data_len =
    engine->usb.audio_out_blk_len * ow_engine_v3_out_packets (engine);

  for (int i = 0; i < OW_ISO_URBS; i++)
    {
      engine->usb.xfr_audio_in_data_q[i] =
	malloc (engine->usb.xfr_audio_in_data_len);
      engine->usb.xfr_audio_out_data_q[i] =
	malloc (engine->usb.xfr_audio_out_data_len);

      if (!engine->usb.xfr_audio_in_data_q[i] ||
	  !engine->usb.xfr_audio_out_data_q[i])
	return OW_GENERIC_ERROR;

      memset (engine->usb.xfr_audio_in_data_q[i], 0,
	      engine->usb.xfr_audio_in_data_len);
      memset (engine->usb.xfr_audio_out_data_q[i], 0,
	      engine->usb.xfr_audio_out_data_len);
    }

  /* Legacy aliases are kept for code which inspects these pointers.
   * V3 audio processing itself always uses the completed xfr->buffer. */
  engine->usb.xfr_audio_in_data = engine->usb.xfr_audio_in_data_q[0];
  engine->usb.xfr_audio_out_data = engine->usb.xfr_audio_out_data_q[0];

  size_t o2h_buffer_size =
    (size_t) engine->blocks_per_transfer *
    OB3_IN_MAX_FRAMES_PER_PACKET * engine->o2h_frame_size;

  const size_t h2o_usb_buffer_size =
    (size_t) ow_engine_v3_out_packets (engine) *
    OB3_OUT_MAX_FRAMES_PER_PACKET * engine->h2o_frame_size;

  engine->h2o_transfer_buf = malloc (h2o_usb_buffer_size);
  engine->o2h_transfer_buf = malloc (o2h_buffer_size);
  engine->h2o_resampler_buf = malloc (engine->h2o_transfer_size);

  if (!engine->h2o_transfer_buf || !engine->o2h_transfer_buf ||
      !engine->h2o_resampler_buf)
    return OW_GENERIC_ERROR;

  memset (engine->h2o_transfer_buf, 0, h2o_usb_buffer_size);
  memset (engine->o2h_transfer_buf, 0, o2h_buffer_size);
  memset (engine->h2o_resampler_buf, 0, engine->h2o_transfer_size);

  engine->h2o_data.data_in = engine->h2o_resampler_buf;
  engine->h2o_data.data_out = engine->h2o_transfer_buf;
  engine->h2o_data.end_of_input = 1;
  engine->h2o_data.input_frames = engine->frames_per_transfer;
  engine->h2o_data.output_frames =
    ow_engine_v3_out_packets (engine) * OB3_OUT_MAX_FRAMES_PER_PACKET;

  engine->usb.xfr_control_out_data = malloc (USB_CONTROL_LEN);
  engine->usb.xfr_control_in_data = malloc (OB_NAME_MAX_LEN);
  if (!engine->usb.xfr_control_out_data || !engine->usb.xfr_control_in_data)
    return OW_GENERIC_ERROR;

  debug_print (1,
	       "v3: %u IN packets x nominal %u frames (max %u), "
	       "%u OUT packets x %u frames (%u nominal frames/transfer)",
	       engine->blocks_per_transfer, OB3_IN_FRAMES_PER_PACKET,
	       OB3_IN_MAX_FRAMES_PER_PACKET,
	       ow_engine_v3_out_packets (engine), OB3_OUT_FRAMES_PER_PACKET,
	       engine->frames_per_transfer);

  return OW_OK;
}

// initialization taken from sniffed session

static ow_err_t
usb_init_v2 (struct ow_engine *engine, struct ow_device *device,
	     unsigned int blocks_per_transfer)
{
  int err;
  ow_err_t ret = OW_OK;

  libusb_detach_kernel_driver (engine->usb.device_handle,
			       OB2_USB_CONTROL_INTERFACE);
  libusb_detach_kernel_driver (engine->usb.device_handle,
			       OB2_USB_MIDI_INTERFACE);

  err = libusb_set_configuration (engine->usb.device_handle,
				  OBX_USB_CONFIGURATION);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_USB_CONFIG;
      goto end;
    }

  err = libusb_claim_interface (engine->usb.device_handle,
				OB2_USB_AUDIO_IN_INTERFACE);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (engine->usb.device_handle,
					  OB2_USB_AUDIO_IN_INTERFACE,
					  OB2_USB_AUDIO_IN_ALT_SETTING);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (engine->usb.device_handle,
				OB2_USB_AUDIO_OUT_INTERFACE);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (engine->usb.device_handle,
					  OB2_USB_AUDIO_OUT_INTERFACE,
					  OB2_USB_AUDIO_OUT_ALT_SETTING);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }

  err = libusb_clear_halt (engine->usb.device_handle, OB2_USB_AUDIO_IN_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (engine->usb.device_handle, OB2_USB_AUDIO_OUT_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }

  err = prepare_transfers_v2 (engine);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_PREPARE_TRANSFER;
      goto end;
    }

  err = prepare_control_transfers (engine);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_PREPARE_TRANSFER;
      goto end;
    }

  libusb_attach_kernel_driver (engine->usb.device_handle,
			       OB2_USB_CONTROL_INTERFACE);
  libusb_attach_kernel_driver (engine->usb.device_handle,
			       OB2_USB_MIDI_INTERFACE);

#if LIBUSB_API_VERSION >= 0x0100010A
  engine->usb.audio_in_blk_len =
    libusb_get_max_alt_packet_size (engine->usb.device,
				    OB2_USB_AUDIO_IN_INTERFACE,
				    OB2_USB_AUDIO_IN_ALT_SETTING,
				    OB2_USB_AUDIO_IN_EP);

  engine->usb.audio_out_blk_len =
    libusb_get_max_alt_packet_size (engine->usb.device,
				    OB2_USB_AUDIO_OUT_INTERFACE,
				    OB2_USB_AUDIO_OUT_ALT_SETTING,
				    OB2_USB_AUDIO_OUT_EP);
#else
  engine->usb.audio_in_blk_len = 0;
  engine->usb.audio_out_blk_len = 0;
#endif

  err = LIBUSB_SUCCESS;
  ret = ow_engine_init_mem_v2 (engine, blocks_per_transfer);

end:
  if (ret != OW_OK)
    {
      usb_shutdown (engine);
      free (engine);
      error_print ("%s (%s)", ow_get_err_str (ret), libusb_error_name (err));
    }

  return ret;
}

static ow_err_t
usb_init_v1 (struct ow_engine *engine, struct ow_device *device,
	     unsigned int blocks_per_transfer)
{
  int err = LIBUSB_SUCCESS;
  ow_err_t ret = OW_OK;
  int res, in_max_packet_size, out_max_packet_size;
  uint8_t map[2][OB1_MAP_LEN];
  uint8_t depth[2];
  uint8_t protocol[OB1_USB_PROTOCOL_LEN];
  struct libusb_config_descriptor *config = NULL;

  err = libusb_get_active_config_descriptor (engine->usb.device, &config);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_USB_CONFIG;
      goto end;
    }

  //Depending on the OS, the kernel drivers might be using some interfaces.
  for (int i = 0; i < config->bNumInterfaces && i < OW_ENGINE_MAX_INTERFACES;
       i++)
    {
      int interface = config->interface[i].altsetting[0].bInterfaceNumber;
      if (libusb_kernel_driver_active (engine->usb.device_handle,
				       interface) == 1 &&
	  !libusb_detach_kernel_driver (engine->usb.device_handle, interface))
	{
	  debug_print (2, "Kernel driver detached from interface %d",
		       interface);
	  engine->usb.detached[engine->usb.detached_count++] = interface;
	}
    }

  err = libusb_set_configuration (engine->usb.device_handle,
				  OBX_USB_CONFIGURATION);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_USB_CONFIG;
      goto end;
    }

  err = libusb_claim_interface (engine->usb.device_handle,
				engine->usb.audio_in_interface);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_claim_interface (engine->usb.device_handle,
				engine->usb.audio_out_interface);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }

  //The drivers of the claimed interfaces are attached when these are released.
  attach_kernel_drivers (engine, 0);

  err = libusb_control_transfer (engine->usb.device_handle,
				 LIBUSB_ENDPOINT_IN |
				 LIBUSB_REQUEST_TYPE_VENDOR |
				 LIBUSB_RECIPIENT_DEVICE,
				 OB1_USB_REQ_GET_PROTOCOL, 0, 0, protocol,
				 OB1_USB_PROTOCOL_LEN,
				 OB1_USB_CONTROL_TIMEOUT);
  if (err != OB1_USB_PROTOCOL_LEN ||
      memcmp (protocol, OB1_USB_PROTOCOL, OB1_USB_PROTOCOL_LEN))
    {
      char data[OB1_USB_PROTOCOL_LEN * 3 + 1] = "";

      //The received data is needed to support other protocols.
      for (int i = 0; i < err; i++)
	{
	  snprintf (data + i * 3, 4, " %02x", protocol[i]);
	}
      error_print
	("Unsupported Overbridge protocol (%d B:%s). Update the device OS to the latest version.",
	 err > 0 ? err : 0, data);
      ret = OW_USB_ERROR_UNSUPPORTED_PROTOCOL;
      goto end;
    }

  //The layout must fit in the endpoints before it is stored in the device.
  engine->usb.audio_in_alt_setting =
    ow_engine_get_alt_setting_v1 (config, engine->usb.audio_in_interface,
				  OB1_USB_AUDIO_IN_EP,
				  OB1_USB_AUDIO_IN_BLK_LEN
				  (ow_get_frame_size_from_desc_tracks
				   (device->desc.outputs,
				    device->desc.output_tracks)),
				  &in_max_packet_size);
  engine->usb.audio_out_alt_setting =
    ow_engine_get_alt_setting_v1 (config, engine->usb.audio_out_interface,
				  OB1_USB_AUDIO_OUT_EP,
				  OB1_USB_AUDIO_OUT_BLK_LEN
				  (ow_get_frame_size_from_desc_tracks
				   (device->desc.inputs,
				    device->desc.input_tracks)),
				  &out_max_packet_size);
  if (engine->usb.audio_in_alt_setting < 0 ||
      engine->usb.audio_out_alt_setting < 0)
    {
      error_print ("The track layout exceeds the USB bandwidth");
      err = LIBUSB_SUCCESS;
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }
  engine->usb.audio_in_blk_len = in_max_packet_size;
  engine->usb.audio_out_blk_len = out_max_packet_size;

  err = read_layout_v1 (engine);
  if (err)
    {
      ret = OW_GENERIC_ERROR;
      goto end;
    }

  //If the device description defines the layout, it is stored in the device.
  //Otherwise, the description must match the layout stored in the device.
  res = ow_engine_get_desc_layout_v1 (&device->desc, map, depth);
  if (res < 0)
    {
      err = LIBUSB_SUCCESS;
      ret = OW_USB_ERROR_LAYOUT_MISMATCH;
      goto end;
    }
  else if (res &&
	   (memcmp (map, engine->usb.layout_map, sizeof (map)) ||
	    memcmp (depth, engine->usb.layout_depth, sizeof (depth))))
    {
      debug_print (1, "Storing the track layout in the device...");
      err = write_stored_layout_v1 (engine, map, depth);
      if (!err)
	{
	  err = read_layout_v1 (engine);
	}
      if (err)
	{
	  ret = OW_GENERIC_ERROR;
	  goto end;
	}
    }

  if (ow_engine_check_layout_v1 (engine))
    {
      err = LIBUSB_SUCCESS;
      ret = OW_USB_ERROR_LAYOUT_MISMATCH;
      goto end;
    }

  err = prepare_control_transfers (engine);
  if (LIBUSB_SUCCESS != err)
    {
      err = LIBUSB_ERROR_NO_MEM;
      ret = OW_USB_ERROR_CANT_PREPARE_TRANSFER;
      goto end;
    }

  err = LIBUSB_SUCCESS;
  ret = ow_engine_init_mem_v1 (engine, blocks_per_transfer);

end:
  if (config)
    {
      libusb_free_config_descriptor (config);
    }

  if (ret != OW_OK)
    {
      usb_shutdown (engine);
      free (engine);
      if (err < 0)
	{
	  error_print ("%s (%s)", ow_get_err_str (ret),
		       libusb_error_name (err));
	}
      else
	{
	  error_print ("%s", ow_get_err_str (ret));
	}
    }

  return ret;
}

static void
prepare_v3 (struct ow_engine *engine)
{
  prepare_cycle_in_v3 (engine);
  prepare_cycle_out_v3 (engine);
}

static ow_err_t
usb_init_v3 (struct ow_engine *engine, struct ow_device *device,
	     unsigned int blocks_per_transfer)
{
  int err;
  int mem_initialized = 0;
  ow_err_t ret = OW_OK;

/* Tonverk is a composite USB device.  On Linux, other interfaces may
   * already be bound to kernel drivers (for example storage/MIDI).
   * Calling libusb_set_configuration() again while any interface is claimed
   * can return LIBUSB_ERROR_BUSY even when configuration 1 is already active.
   * Only change the configuration when it is actually different. */
  int current_config = 0;
  err = libusb_get_configuration (engine->usb.device_handle, &current_config);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_USB_CONFIG;
      goto end;
    }

  debug_print (1, "USB V3 current configuration: %d", current_config);

  if (current_config != OBX_USB_CONFIGURATION)
    {
      err = libusb_set_configuration (engine->usb.device_handle,
				      OBX_USB_CONFIGURATION);
      if (LIBUSB_SUCCESS != err)
	{
	  ret = OW_USB_ERROR_CANT_SET_USB_CONFIG;
	  goto end;
	}
    }

  /* We only need ownership of the two Overbridge audio interfaces. */
  err = libusb_detach_kernel_driver (engine->usb.device_handle,
				     OB3_USB_AUDIO_IN_INTERFACE);
  if (err != LIBUSB_SUCCESS && err != LIBUSB_ERROR_NOT_FOUND &&
      err != LIBUSB_ERROR_NOT_SUPPORTED)
    {
      error_print
	("v3: could not detach kernel driver from IN interface %d: %s",
	 OB3_USB_AUDIO_IN_INTERFACE, libusb_error_name (err));
    }

  err = libusb_detach_kernel_driver (engine->usb.device_handle,
				     OB3_USB_AUDIO_OUT_INTERFACE);
  if (err != LIBUSB_SUCCESS && err != LIBUSB_ERROR_NOT_FOUND &&
      err != LIBUSB_ERROR_NOT_SUPPORTED)
    {
      error_print
	("v3: could not detach kernel driver from OUT interface %d: %s",
	 OB3_USB_AUDIO_OUT_INTERFACE, libusb_error_name (err));
    }

  /* USBPcap startup capture: vendor IN 0, 0, 0, 2, 1.
   * Request 0 returns an 8-byte device token; request 2 returns the
   * V3 protocol/version string; request 1 returns the Overbridge name. */
  err = ow_engine_init_control_v3 (engine);
  if (err < 0)
    {
      ret = OW_GENERIC_ERROR;
      goto end;
    }

  err = libusb_claim_interface (engine->usb.device_handle,
				OB3_USB_AUDIO_IN_INTERFACE);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }

  err = libusb_set_interface_alt_setting (engine->usb.device_handle,
					  OB3_USB_AUDIO_IN_INTERFACE,
					  OB3_USB_AUDIO_IN_ALT_SETTING);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }

  err = libusb_claim_interface (engine->usb.device_handle,
				OB3_USB_AUDIO_OUT_INTERFACE);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }

  err = libusb_set_interface_alt_setting (engine->usb.device_handle,
					  OB3_USB_AUDIO_OUT_INTERFACE,
					  OB3_USB_AUDIO_OUT_ALT_SETTING);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }

  ret = ow_engine_init_mem_v3 (engine, blocks_per_transfer);
  if (ret != OW_OK)
    goto end;
  mem_initialized = 1;

  err = prepare_iso_transfers (engine);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_PREPARE_TRANSFER;
      goto end;
    }

  err = prepare_control_transfers (engine);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_PREPARE_TRANSFER;
      goto end;
    }

  err = LIBUSB_SUCCESS;

end:
  if (ret != OW_OK)
    {
      usb_shutdown (engine);
      if (mem_initialized)
	ow_engine_free_mem (engine);
      free (engine);
      error_print ("%s (%s)", ow_get_err_str (ret), libusb_error_name (err));
    }

  return ret;
}

//Overbridge 1 streams start later as the isochronous transfers must not be
//left unattended.
static const struct ow_engine_ops OW_ENGINE_OPS_V1 = {
  .init = usb_init_v1,
  .prepare = NULL,
  .start = start_v1,
  .stop = stop_stream_v1,
  .shutdown = usb_shutdown_v1
};

static const struct ow_engine_ops OW_ENGINE_OPS_V2 = {
  .init = usb_init_v2,
  .prepare = prepare_v2,
  .start = NULL,
  .stop = stop_v2_v3,
  .shutdown = usb_shutdown_v2
};

static const struct ow_engine_ops OW_ENGINE_OPS_V3 = {
  .init = usb_init_v3,
  .prepare = prepare_v3,
  .start = NULL,
  .stop = stop_v2_v3,
  .shutdown = usb_shutdown_v3
};

static ow_err_t
ow_engine_init (struct ow_engine *engine, struct ow_device *device,
		unsigned int blocks_per_transfer, unsigned int xfr_timeout)
{
  int usb_audio_in_interface;
  int usb_audio_out_interface;
  const struct ow_engine_ops *ops;

  switch (device->desc.version)
    {
    case OW_DEVICE_VERSION_1:
      usb_audio_in_interface = OB1_USB_AUDIO_IN_INTERFACE;
      usb_audio_out_interface = OB1_USB_AUDIO_OUT_INTERFACE;
      ops = &OW_ENGINE_OPS_V1;
      break;
    case OW_DEVICE_VERSION_2:
    case OW_DEVICE_VERSION_2_1:
      usb_audio_in_interface = OB3_USB_AUDIO_IN_INTERFACE;
      usb_audio_out_interface = OB3_USB_AUDIO_OUT_INTERFACE;
      ops = &OW_ENGINE_OPS_V2;
      break;
    case OW_DEVICE_VERSION_3:
      usb_audio_in_interface = OB3_USB_AUDIO_IN_INTERFACE;
      usb_audio_out_interface = OB3_USB_AUDIO_OUT_INTERFACE;
      ops = &OW_ENGINE_OPS_V3;
      break;
    default:
      return OW_GENERIC_ERROR;
    }

  return ow_engine_init_usb (engine, device, ops, usb_audio_in_interface,
			     usb_audio_out_interface, blocks_per_transfer,
			     xfr_timeout);
}

ow_err_t
ow_engine_init_from_device (struct ow_engine **engine_,
			    struct ow_device *ow_device,
			    unsigned int blocks_per_transfer,
			    unsigned int xfr_timeout)
{
  int err;
  ow_err_t ret;
  ssize_t total = 0;
  libusb_device **devices;
  libusb_device **device;
  struct ow_engine *engine;
  struct libusb_device_descriptor desc;

  engine = malloc (sizeof (struct ow_engine));

  if (libusb_init (&engine->usb.context) != LIBUSB_SUCCESS)
    {
      ret = OW_USB_ERROR_LIBUSB_INIT_FAILED;
      goto error;
    }

  engine->usb.device_handle = NULL;
  total = libusb_get_device_list (engine->usb.context, &devices);
  device = devices;
  for (int i = 0; i < total; i++, device++)
    {
      err = libusb_get_device_descriptor (*device, &desc);
      if (err)
	{
	  error_print ("Error while getting device description: %s",
		       libusb_error_name (err));
	  continue;
	}

      if (libusb_get_bus_number (*device) == ow_device->bus &&
	  libusb_get_device_address (*device) == ow_device->address)
	{
	  err = libusb_open (*device, &engine->usb.device_handle);
	  if (err)
	    {
	      error_print ("Error while opening device: %s",
			   libusb_error_name (err));
	      continue;
	    }

	  libusb_ref_device (*device);
	  engine->usb.device = *device;
	  break;
	}
    }

  libusb_free_device_list (devices, 1);

  if (!engine->usb.device_handle)
    {
      ret = OW_USB_ERROR_CANT_FIND_DEV;
      goto error;
    }

  *engine_ = engine;
  ret = ow_engine_init (engine, ow_device, blocks_per_transfer, xfr_timeout);
  if (!ret)
    {
      ow_engine_init_name (engine);
    }
  return ret;

error:
  free (engine);
  return ret;
}

static const char *ob_err_strgs[] = {
  "ok",
  "generic error",
  "libusb init failed",
  "can't open device",
  "can't set usb config",
  "can't claim usb interface",
  "can't set usb alt setting",
  "can't cleat endpoint",
  "can't prepare transfer",
  "can't find a matching device",
  "unexpected USB transfer size",
  "'read_space' not set in context",
  "'write_space' not set in context",
  "'read' not set in context",
  "'write' not set in context",
  "'o2h_audio' not set in context",
  "'h2o_audio' not set in context",
  "'get_time' not set in context",
  "'dll' not set in context",
  "unsupported Overbridge protocol",
  "device track layout does not match the device definition"
};

static void
prepare_cycle_in_v3 (struct ow_engine *engine)
{
  const int num_packets = engine->blocks_per_transfer;

  for (int i = 0; i < OW_ISO_URBS; i++)
    {
      libusb_fill_iso_transfer (engine->usb.xfr_audio_in_q[i],
				engine->usb.device_handle,
				OB3_USB_AUDIO_IN_EP,
				engine->usb.xfr_audio_in_data_q[i],
				engine->usb.xfr_audio_in_data_len,
				num_packets, cb_xfr_audio_in_v3, engine, 0);

      libusb_set_iso_packet_lengths (engine->usb.xfr_audio_in_q[i],
				     OB3_INPUT_MAX_PACKET_LEN);

      int err = libusb_submit_transfer (engine->usb.xfr_audio_in_q[i]);
      if (err)
	{
	  error_print ("o2h (v3): Error submitting IN queue %d: %s",
		       i, libusb_strerror (err));
	  ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
	}
    }
}

static void
prepare_cycle_out_v3 (struct ow_engine *engine)
{
  const int num_packets = ow_engine_v3_out_packets (engine);

  for (int i = 0; i < OW_ISO_URBS; i++)
    {
      libusb_fill_iso_transfer (engine->usb.xfr_audio_out_q[i],
				engine->usb.device_handle,
				OB3_USB_AUDIO_OUT_EP,
				engine->usb.xfr_audio_out_data_q[i],
				engine->usb.xfr_audio_out_data_len,
				num_packets, cb_xfr_audio_out_v3, engine, 0);

      libusb_set_iso_packet_lengths (engine->usb.xfr_audio_out_q[i],
				     OB3_OUTPUT_PACKET_LEN);

      /* Tonverk expects valid H2O framing even before JACK starts producing
       * audio.  The transfer buffer is initially zero, so this creates
       * correctly framed silence with a continuously advancing counter. */
      write_usb_output_xfr_v3 (engine, engine->usb.xfr_audio_out_q[i],
			       engine->frames_per_transfer);

      int err = libusb_submit_transfer (engine->usb.xfr_audio_out_q[i]);
      if (err)
	{
	  error_print ("h2o (v3): Error submitting OUT queue %d: %s",
		       i, libusb_strerror (err));
	  ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
	}
    }
}

static void *
run_audio (void *data)
{
  int err;
  size_t rsh2o, bytes;
  struct ow_engine *engine = data;

  // This needs to be set before the host side. We ensure this by changing the state after.
  // The state is monitored at ow_engine_start and only returns after this transition.
  if (engine->context->dll)
    {
      engine->context->dll_overbridge_init (engine->context->dll,
					    OB_SAMPLE_RATE,
					    engine->frames_per_transfer);
    }

  // These calls are needed to initialize the Overbridge side before the host side.

  if (engine->ops->prepare)
    {
      engine->ops->prepare (engine);
    }

  // status == OW_ENGINE_STATUS_STOP

  // This can NOT use ow_engine_set_status as the transition is not allowed from OW_ENGINE_STATUS_STOP.
  pthread_spin_lock (&engine->lock);
  engine->status = OW_ENGINE_STATUS_READY;
  pthread_spin_unlock (&engine->lock);

  // status == OW_ENGINE_STATUS_READY

  if (engine->context->dll)
    {
      // This needs to be fast to ensure the lowest latency.
      while (ow_engine_get_status (engine) != OW_ENGINE_STATUS_STEADY)
	{
	}

      debug_print (1, "Notification of readiness received from resampler");
    }
  else
    {
      ow_engine_set_status (engine, OW_ENGINE_STATUS_STEADY);
    }

  // status == OW_ENGINE_STATUS_STEADY

  pthread_spin_lock (&engine->lock);
  if (engine->status <= OW_ENGINE_STATUS_STOP)
    {
      pthread_spin_unlock (&engine->lock);
      goto end;
    }
  engine->status = OW_ENGINE_STATUS_BOOT;
  pthread_spin_unlock (&engine->lock);

  if (engine->ops->start && engine->ops->start (engine))
    {
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
      goto end;
    }

  while (1)
    {
      // status == OW_ENGINE_STATUS_BOOT || status == OW_ENGINE_STATUS_CLEAR

      debug_print (1, "Booting or clearing engine...");

      engine->latency_h2o = engine->latency_h2o_min;
      engine->latency_h2o_max = engine->latency_h2o_min;
      engine->latency_o2h = engine->latency_o2h_min;
      engine->latency_o2h_max = engine->latency_o2h_min;

      engine->reading_at_h2o_end = engine->context->dll ? 0 : 1;

      pthread_spin_lock (&engine->lock);
      if (engine->status <= OW_ENGINE_STATUS_STOP)
	{
	  pthread_spin_unlock (&engine->lock);
	  goto end;
	}

      if (engine->status == OW_ENGINE_STATUS_CLEAR)
	{
	  engine->status = OW_ENGINE_STATUS_RUN;
	}

      if (engine->context->dll)
	{
	  if (engine->status == OW_ENGINE_STATUS_BOOT)
	    {
	      engine->status = OW_ENGINE_STATUS_WAIT;
	    }
	}
      else
	{
	  engine->status = OW_ENGINE_STATUS_RUN;
	}
      pthread_spin_unlock (&engine->lock);

      while (ow_engine_get_status (engine) >= OW_ENGINE_STATUS_WAIT)
	{
	  err = libusb_handle_events_completed (engine->usb.context, NULL);
	  if (err)
	    {
	      error_print ("USB error: %s", libusb_error_name (err));
	    }
	}

      if (ow_engine_get_status (engine) < OW_ENGINE_STATUS_BOOT)
	{
	  break;
	}

      // status == OW_ENGINE_STATUS_BOOT || status == OW_ENGINE_STATUS_CLEAR

      debug_print (1, "Clearing buffers...");

      rsh2o = engine->context->read_space (engine->context->h2o_audio);
      bytes = ow_bytes_to_frame_bytes (rsh2o, engine->h2o_audio_frame_size);
      engine->context->read (engine->context->h2o_audio, NULL, bytes);
      memset (engine->h2o_transfer_buf, 0, engine->h2o_transfer_size);
    }

end:
  // status == OW_ENGINE_STATUS_STOP || status == OW_ENGINE_STATUS_ERROR

  engine->ops->stop (engine);

  return NULL;
}

void
ow_engine_clear_buffers (struct ow_engine *engine)
{
  pthread_spin_lock (&engine->lock);
  if (engine->status == OW_ENGINE_STATUS_RUN)
    {
      engine->status = OW_ENGINE_STATUS_CLEAR;
    }
  pthread_spin_unlock (&engine->lock);
}

extern int pthread_setname_np (pthread_t thread, const char *name);

static void
ow_engine_set_thread_name (struct ow_engine *engine, const char *name)
{
  char buf[OW_LABEL_MAX_LEN];
  snprintf (buf, OW_LABEL_MAX_LEN, "engine-%.8s", name);
  pthread_setname_np (engine->thread, buf);
}

ow_err_t
ow_engine_start (struct ow_engine *engine, struct ow_context *context)
{
  engine->context = context;

  if (context->options & OW_ENGINE_OPTION_O2H_AUDIO)
    {
      if (!context->read_space)
	{
	  return OW_INIT_ERROR_NO_READ_SPACE;
	}
      if (!context->write_space)
	{
	  return OW_INIT_ERROR_NO_WRITE_SPACE;
	}
      if (!context->write)
	{
	  return OW_INIT_ERROR_NO_WRITE;
	}
      if (!context->o2h_audio)
	{
	  return OW_INIT_ERROR_NO_O2H_AUDIO_BUF;
	}
      if (!context->set_rt_priority)
	{
	  context->set_rt_priority = ow_set_thread_rt_priority;
	  context->priority = OW_DEFAULT_RT_PRIORITY;
	}
    }

  if (context->options & OW_ENGINE_OPTION_H2O_AUDIO)
    {
      if (!context->read_space)
	{
	  return OW_INIT_ERROR_NO_READ_SPACE;
	}
      if (!context->read)
	{
	  return OW_INIT_ERROR_NO_READ;
	}
      if (!context->h2o_audio)
	{
	  return OW_INIT_ERROR_NO_H2O_AUDIO_BUF;
	}
      if (!context->set_rt_priority)
	{
	  context->set_rt_priority = ow_set_thread_rt_priority;
	  context->priority = OW_DEFAULT_RT_PRIORITY;
	}
    }

  if (engine->context->dll)
    {
      if (!context->get_time)
	{
	  return OW_INIT_ERROR_NO_GET_TIME;
	}
    }

  debug_print (1, "Starting thread...");
  if (pthread_create (&engine->thread, NULL, run_audio, engine))
    {
      error_print ("Could not start thread");
      return OW_GENERIC_ERROR;
    }
  ow_engine_set_thread_name (engine, engine->overbridge_name);
  if (context->set_rt_priority)
    {
      context->set_rt_priority (engine->thread,
				engine->context->priority + 1);
    }

  //status == OW_ENGINE_STATUS_STOP

  //Wait till the thread has started
  while (ow_engine_get_status (engine) == OW_ENGINE_STATUS_STOP);

  //status == OW_ENGINE_STATUS_READY

  return OW_OK;
}

inline void
ow_engine_wait (struct ow_engine *engine)
{
  pthread_join (engine->thread, NULL);
}

const char *
ow_get_err_str (ow_err_t errcode)
{
  return ob_err_strgs[errcode];
}

void
ow_engine_destroy (struct ow_engine *engine)
{
  usb_shutdown (engine);
  ow_engine_free_mem (engine);
  free (engine->device);
  free (engine);
}

void
ow_engine_free_mem (struct ow_engine *engine)
{
  free (engine->h2o_transfer_buf);
  free (engine->h2o_resampler_buf);
  free (engine->o2h_transfer_buf);

  if (engine->device && engine->device->desc.version == OW_DEVICE_VERSION_3)
    {
      for (int i = 0; i < OW_ISO_URBS; i++)
	{
	  free (engine->usb.xfr_audio_in_data_q[i]);
	  free (engine->usb.xfr_audio_out_data_q[i]);
	  engine->usb.xfr_audio_in_data_q[i] = NULL;
	  engine->usb.xfr_audio_out_data_q[i] = NULL;
	}
      engine->usb.xfr_audio_in_data = NULL;
      engine->usb.xfr_audio_out_data = NULL;
    }
  else
    {
      free (engine->usb.xfr_audio_in_data);
      free (engine->usb.xfr_audio_out_data);
    }

  free (engine->usb.xfr_control_out_data);
  free (engine->usb.xfr_control_in_data);
  pthread_spin_destroy (&engine->lock);
}

inline ow_engine_status_t
ow_engine_get_status (struct ow_engine *engine)
{
  ow_engine_status_t status;
  pthread_spin_lock (&engine->lock);
  status = engine->status;
  pthread_spin_unlock (&engine->lock);
  return status;
}

inline void
ow_engine_set_status (struct ow_engine *engine, ow_engine_status_t status)
{
  pthread_spin_lock (&engine->lock);
  if (engine->status > OW_ENGINE_STATUS_STOP)
    {
      engine->status = status;
    }
  pthread_spin_unlock (&engine->lock);
}

inline int
ow_engine_is_option (struct ow_engine *engine, ow_engine_option_t option)
{
  int enabled;
  pthread_spin_lock (&engine->lock);
  enabled = (engine->context->options & option) != 0;
  pthread_spin_unlock (&engine->lock);
  return enabled;
}

inline void
ow_engine_set_option (struct ow_engine *engine, ow_engine_option_t option,
		      int enabled)
{
  int last = ow_engine_is_option (engine, option);
  if (last != enabled)
    {
      pthread_spin_lock (&engine->lock);
      if (enabled)
	{
	  engine->context->options |= option;
	}
      else
	{
	  engine->context->options &= ~option;
	}
      pthread_spin_unlock (&engine->lock);
      debug_print (1, "Setting option %d to %d...", option, enabled);
    }
}

inline int
ow_bytes_to_frame_bytes (int bytes, int bytes_per_frame)
{
  int frames = bytes / bytes_per_frame;
  return frames * bytes_per_frame;
}

const struct ow_device *
ow_engine_get_device (struct ow_engine *engine)
{
  return engine->device;
}

inline void
ow_engine_stop (struct ow_engine *engine)
{
  debug_print (1, "Stopping engine...");
  ow_engine_set_status (engine, OW_ENGINE_STATUS_STOP);
}

static int
ow_engine_control_read_v3 (struct ow_engine *engine, uint8_t request,
			   uint8_t *dst, uint16_t len)
{
  int res = libusb_control_transfer (engine->usb.device_handle,
				     LIBUSB_ENDPOINT_IN |
				     LIBUSB_REQUEST_TYPE_VENDOR |
				     LIBUSB_RECIPIENT_DEVICE,
				     request, 0, 0, dst, len, 0);

  if (res < 0)
    {
      error_print ("v3: vendor IN request %u failed: %s", request,
		   libusb_strerror (res));
      return res;
    }

  if (res != len)
    debug_print (1, "v3: vendor IN request %u returned %d/%u bytes",
		 request, res, len);

  return res;
}

static int
ow_engine_init_control_v3 (struct ow_engine *engine)
{
  uint8_t token[8] = { 0 };
  uint8_t version[16] = { 0 };
  uint8_t name[OB_NAME_MAX_LEN] = { 0 };
  int res;

  /* Exact sequence observed in the Windows startup capture.  Request 0 was
   * issued three times, with roughly 20 ms and 140 ms gaps, before requests
   * 2 and 1.  Replaying it is cheap and avoids assuming those reads were
   * merely redundant probes. */
  res = ow_engine_control_read_v3 (engine, 0, token, sizeof (token));
  if (res < 0)
    return res;
  usleep (20000);

  res = ow_engine_control_read_v3 (engine, 0, token, sizeof (token));
  if (res < 0)
    return res;
  usleep (140000);

  res = ow_engine_control_read_v3 (engine, 0, token, sizeof (token));
  if (res < 0)
    return res;

  res = ow_engine_control_read_v3 (engine, 2, version, sizeof (version));
  if (res < 0)
    return res;

  res = ow_engine_control_read_v3 (engine, 1, name, sizeof (name));
  if (res < 0)
    return res;

  memcpy (engine->overbridge_name, name, OB_NAME_MAX_LEN);
  engine->overbridge_name[OB_NAME_MAX_LEN - 1] = '\0';

  debug_print (1, "v3: protocol version: %.12s", (char *) version + 4);
  debug_print (1, "v3: Overbridge name: %s", engine->overbridge_name);

  return LIBUSB_SUCCESS;
}

static void
ow_engine_load_overbridge_name (struct ow_engine *engine)
{
  int res = libusb_control_transfer (engine->usb.device_handle,
				     LIBUSB_ENDPOINT_IN |
				     LIBUSB_REQUEST_TYPE_VENDOR |
				     LIBUSB_RECIPIENT_DEVICE, 1, 0, 0,
				     engine->usb.xfr_control_in_data,
				     OB_NAME_MAX_LEN, 0);

  if (res >= 0)
    {
      debug_print (1, "USB control in data (%d B): %s", res,
		   engine->usb.xfr_control_in_data);
      memcpy (engine->overbridge_name, engine->usb.xfr_control_in_data,
	      OB_NAME_MAX_LEN);
    }
  else
    {
      error_print ("Error on USB control in transfer: %s",
		   libusb_strerror (res));
    }

  res = libusb_control_transfer (engine->usb.device_handle,
				 LIBUSB_ENDPOINT_IN |
				 LIBUSB_REQUEST_TYPE_VENDOR |
				 LIBUSB_RECIPIENT_DEVICE, 2, 0, 0,
				 engine->usb.xfr_control_in_data,
				 OB_NAME_MAX_LEN, 0);

  if (res >= 0)
    {
      debug_print (1, "USB control in data (%d B): %s", res,
		   engine->usb.xfr_control_in_data);
    }
  else
    {
      error_print ("Error on USB control in transfer: %s",
		   libusb_strerror (res));
    }

  usleep (100000);		// This is required to not send the next packet immediately, which can make devices crash.
}

static void LIBUSB_CALL
cb_xfr_control_out (struct libusb_transfer *xfr)
{
  if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
    error_print ("Error on USB control out transfer (%d B): %s",
		 xfr->actual_length, libusb_error_name (xfr->status));
}

static inline int32_t
ow_v3_read_s32le (const uint8_t *src)
{
  uint32_t raw;
  memcpy (&raw, src, sizeof (raw));
  return (int32_t) le32toh (raw);
}

static inline void
ow_v3_write_s32le (uint8_t *dst, float value)
{
  if (value > 1.0f)
    value = 1.0f;
  else if (value < -1.0f)
    value = -1.0f;

  int32_t sample = (int32_t) (value * (float) INT32_MAX);
  uint32_t raw = htole32 ((uint32_t) sample);
  memcpy (dst, &raw, sizeof (raw));
}

static unsigned int
read_usb_input_xfr_v3 (struct ow_engine *engine, struct libusb_transfer *xfr)
{
  const unsigned int logical_channels = engine->device->desc.outputs;
  unsigned int valid_frames = 0;

  memset (engine->o2h_transfer_buf, 0, engine->o2h_transfer_size);

  for (int packet_no = 0; packet_no < xfr->num_iso_packets; packet_no++)
    {
      struct libusb_iso_packet_descriptor *p =
	&xfr->iso_packet_desc[packet_no];

      if (p->status != LIBUSB_TRANSFER_COMPLETED)
	continue;

      uint8_t *packet = libusb_get_iso_packet_buffer_simple (xfr, packet_no);

      if (!packet || p->actual_length < OB3_INPUT_HEADER_LEN)
	continue;

      uint32_t frames_le;
      memcpy (&frames_le, packet, sizeof (frames_le));
      unsigned int wire_frames = le32toh (frames_le);

      if (wire_frames == 0 || wire_frames > OB3_IN_MAX_FRAMES_PER_PACKET)
	continue;

      size_t expected_length =
	OB3_INPUT_HEADER_LEN +
	(size_t) wire_frames * OB3_IN_WIRE_CHANNELS * sizeof (int32_t);

      if (p->actual_length < expected_length)
	continue;

      float *dst =
	(float *) engine->o2h_transfer_buf +
	(size_t) valid_frames * logical_channels;

      for (unsigned int frame = 0; frame < wire_frames; frame++)
	{
	  const uint8_t *wire_frame =
	    packet +
	    OB3_INPUT_HEADER_LEN +
	    (size_t) frame * OB3_IN_WIRE_CHANNELS * sizeof (int32_t);

	  for (unsigned int channel = 0;
	       channel < logical_channels; channel++)
	    {
	      int32_t sample =
		ow_v3_read_s32le (wire_frame + channel * sizeof (int32_t));

	      *dst++ = sample / (float) INT32_MAX;
	    }
	}

      valid_frames += wire_frames;
    }

  return valid_frames;
}

static void
set_usb_input_data_xfr_v3 (struct ow_engine *engine,
			   struct libusb_transfer *xfr)
{
  size_t wso2h;
  ow_engine_status_t status;
  const unsigned int valid_frames = read_usb_input_xfr_v3 (engine, xfr);
  const uint64_t now = engine->context->get_time ();

  if (!engine->v3_streaming_started)
    {
      if (valid_frames < engine->frames_per_transfer - 1 ||
	  valid_frames > engine->frames_per_transfer + 1)
	return;

      engine->v3_streaming_started = 1;
      debug_print (1, "o2h (v3): stream synchronized");
    }

  /*
   * Once the V3 stream is synchronized, keep using the real device frame
   * count even if the device clock moves outside the usual 143..145 range.
   * Falling back to 144 here creates a one-sided feedback clamp: as soon as
   * Tonverk reports 143 or less, H2O stops following it and the device PLL
   * is driven farther away.
   *
   * Ignore only empty/corrupt transfers. The receive buffer can hold the
   * full 24 * 7 = 168 frames advertised by the endpoint.
   */
  const unsigned int max_input_frames =
    engine->blocks_per_transfer * OB3_IN_MAX_FRAMES_PER_PACKET;
  const int timing_valid =
    valid_frames > 0 && valid_frames <= max_input_frames;

  pthread_mutex_lock (&v3_fifo_lock);
  v3_clock_fifo[v3_fifo_head++] =
    timing_valid ? valid_frames : engine->frames_per_transfer;
  pthread_mutex_unlock (&v3_fifo_lock);

  const unsigned int timeline_frames =
    timing_valid ? valid_frames : engine->frames_per_transfer;

  pthread_spin_lock (&engine->lock);

  if (engine->context->dll)
    {
      engine->context->dll_overbridge_update (engine->context->dll,
					      timeline_frames, now);
    }

  status = engine->status;

  pthread_spin_unlock (&engine->lock);

  if (status < OW_ENGINE_STATUS_RUN)
    return;

  const size_t write_size = (size_t) timeline_frames * engine->o2h_frame_size;

  wso2h = engine->context->write_space (engine->context->o2h_audio);

  if (write_size <= wso2h)
    {
      engine->context->write (engine->context->o2h_audio,
			      (const char *) engine->o2h_transfer_buf,
			      write_size);
    }
  else
    {
      error_print
	("o2h (v3): Audio ring buffer overflow. Discarding data...");
    }

  pthread_spin_lock (&engine->lock);

  engine->latency_o2h =
    engine->context->read_space (engine->context->o2h_audio) /
    engine->o2h_frame_size;

  if (engine->latency_o2h > engine->latency_o2h_max)
    engine->latency_o2h_max = engine->latency_o2h;

  pthread_spin_unlock (&engine->lock);
}

static inline void
ow_v3_write_u16le (uint8_t *dst, uint16_t value)
{
  dst[0] = (uint8_t) (value & 0xff);
  dst[1] = (uint8_t) (value >> 8);
}

static unsigned int
get_usb_output_frames_v3 (struct ow_engine *engine)
{
  unsigned int frames = engine->frames_per_transfer;

  if (engine->v3_streaming_started)
    {
      pthread_mutex_lock (&v3_fifo_lock);
      const int depth = (uint8_t) (v3_fifo_head - v3_fifo_tail);

      if (depth == 0 || depth > 12)
	v3_fifo_tail = (uint8_t) (v3_fifo_head - OB3_CLOCK_DELAY_TRANSFERS);

      frames = v3_clock_fifo[v3_fifo_tail++];
      pthread_mutex_unlock (&v3_fifo_lock);
    }

  const unsigned int num_packets = ow_engine_v3_out_packets (engine);
  const unsigned int max_frames = num_packets * OB3_OUT_MAX_FRAMES_PER_PACKET;

  /*
   * The low byte of the V3 H2O header is the packet frame count. The
   * official capture only exercises 48/49 at the normal clock rate, but
   * once Tonverk moves below nominal we must preserve that feedback rather
   * than pinning H2O to 144 frames.
   */
  if (frames < num_packets)
    frames = num_packets;
  else if (frames > max_frames)
    frames = max_frames;

  return frames;
}

static void
write_usb_output_xfr_v3 (struct ow_engine *engine,
			 struct libusb_transfer *xfr,
			 unsigned int total_frames)
{
  const unsigned int logical_channels = engine->device->desc.inputs;
  const unsigned int num_packets = (unsigned int) xfr->num_iso_packets;
  unsigned int remaining = total_frames;
  unsigned int first_frame = 0;
  uint16_t counter = engine->usb.audio_frames_counter;

  for (unsigned int packet_no = 0; packet_no < num_packets; packet_no++)
    {
      uint8_t *packet = libusb_get_iso_packet_buffer_simple (xfr, packet_no);

      if (!packet)
	continue;

      const unsigned int packets_left = num_packets - packet_no;

      /*
       * Spread the remaining transfer frames as evenly as possible over the
       * remaining 1 ms packets. This keeps the captured normal-rate shapes:
       *
       *   144 -> [48,48,48]
       *   145 -> [49,48,48]
       *   146 -> [49,49,48]
       *
       * and gives the natural inverse forms below nominal:
       *
       *   143 -> [48,48,47]
       *   142 -> [48,47,47]
       *
       * The physical packet remains 396 bytes; the low header byte selects
       * how many sample slots are valid.
       */
      unsigned int packet_frames =
	(remaining + packets_left - 1) / packets_left;

      if (packet_frames > OB3_OUT_MAX_FRAMES_PER_PACKET)
	packet_frames = OB3_OUT_MAX_FRAMES_PER_PACKET;

      memset (packet, 0, OB3_OUTPUT_PACKET_LEN);

      counter = (uint16_t) (counter + packet_frames);
      ow_v3_write_u16le (packet + 0,
			 (uint16_t) (OB3_OUTPUT_HEADER_BASE | packet_frames));
      ow_v3_write_u16le (packet + 2, counter);

      for (unsigned int frame = 0; frame < packet_frames; frame++)
	{
	  uint8_t *wire_frame =
	    packet +
	    OB3_OUTPUT_AUDIO_OFFSET +
	    (size_t) frame * OB3_OUT_WIRE_CHANNELS * sizeof (int32_t);

	  const float *src_frame =
	    (const float *) engine->h2o_transfer_buf +
	    (size_t) (first_frame + frame) * logical_channels;

	  for (unsigned int channel = 0;
	       channel < OB3_OUT_WIRE_CHANNELS; channel++)
	    {
	      const float value =
		channel < logical_channels ? src_frame[channel] : 0.0f;

	      ow_v3_write_s32le (wire_frame + channel * sizeof (int32_t),
				 value);
	    }
	}

      first_frame += packet_frames;
      remaining -= packet_frames;
    }

  engine->usb.audio_frames_counter = counter;
}

static void
set_usb_output_data_xfr_v3 (struct ow_engine *engine,
			    struct libusb_transfer *xfr)
{
  const unsigned int usb_frames = get_usb_output_frames_v3 (engine);
  const unsigned int host_frames = engine->frames_per_transfer;
  const size_t host_bytes = (size_t) host_frames * engine->h2o_frame_size;
  const size_t usb_bytes = (size_t) usb_frames * engine->h2o_frame_size;
  size_t rsh2o;
  int h2o_enabled = ow_engine_is_option (engine, OW_ENGINE_OPTION_H2O_AUDIO);

  if (!h2o_enabled)
    {
      memset (engine->h2o_transfer_buf, 0, usb_bytes);
      if (engine->reading_at_h2o_end)
	{
	  engine->reading_at_h2o_end = 0;
	  engine->latency_h2o_max = engine->latency_h2o_min;
	}
      write_usb_output_xfr_v3 (engine, xfr, usb_frames);
      return;
    }

  rsh2o = engine->context->read_space (engine->context->h2o_audio);

  if (!engine->reading_at_h2o_end)
    {
      if (rsh2o >= host_bytes &&
	  ow_engine_get_status (engine) == OW_ENGINE_STATUS_RUN)
	{
	  const size_t bytes =
	    ow_bytes_to_frame_bytes (rsh2o, engine->h2o_frame_size);
	  engine->context->read (engine->context->h2o_audio, NULL, bytes);
	  engine->reading_at_h2o_end = 1;
	}

      memset (engine->h2o_transfer_buf, 0, usb_bytes);
      write_usb_output_xfr_v3 (engine, xfr, usb_frames);
      return;
    }

  pthread_spin_lock (&engine->lock);
  engine->latency_h2o = rsh2o / engine->h2o_frame_size;
  if (engine->latency_h2o > engine->latency_h2o_max)
    engine->latency_h2o_max = engine->latency_h2o;
  pthread_spin_unlock (&engine->lock);

  memset (engine->h2o_transfer_buf, 0, usb_bytes);

  if (rsh2o >= host_bytes)
    {
      if (usb_frames == host_frames)
	{
	  engine->context->read (engine->context->h2o_audio,
				 (char *) engine->h2o_transfer_buf,
				 host_bytes);
	}
      else
	{
	  engine->context->read (engine->context->h2o_audio,
				 (char *) engine->h2o_resampler_buf,
				 host_bytes);

	  engine->h2o_data.data_in = engine->h2o_resampler_buf;
	  engine->h2o_data.data_out = engine->h2o_transfer_buf;
	  engine->h2o_data.input_frames = host_frames;
	  engine->h2o_data.output_frames = usb_frames;
	  engine->h2o_data.src_ratio =
	    (double) usb_frames / (double) host_frames;
	  engine->h2o_data.end_of_input = 1;

	  const int res = src_simple (&engine->h2o_data, SRC_SINC_FASTEST,
				      engine->device->desc.inputs);
	  if (res)
	    {
	      error_print
		("h2o (v3): Error expanding %u host frames to %u USB frames "
		 "(ratio %f): %s", host_frames, usb_frames,
		 engine->h2o_data.src_ratio, src_strerror (res));
	      memset (engine->h2o_transfer_buf, 0, usb_bytes);
	    }
	  else if ((unsigned long) engine->h2o_data.output_frames_gen !=
		   usb_frames)
	    {
	      error_print ("h2o (v3): Unexpected expanded frame count "
			   "(output %ld, expected %u)",
			   engine->h2o_data.output_frames_gen, usb_frames);
	    }
	}
    }
  else if (rsh2o >= engine->h2o_frame_size)
    {
      const long input_frames = rsh2o / engine->h2o_frame_size;
      const size_t bytes = (size_t) input_frames * engine->h2o_frame_size;

      engine->context->read (engine->context->h2o_audio,
			     (char *) engine->h2o_resampler_buf, bytes);
      engine->h2o_data.data_in = engine->h2o_resampler_buf;
      engine->h2o_data.data_out = engine->h2o_transfer_buf;
      engine->h2o_data.input_frames = input_frames;
      engine->h2o_data.output_frames = usb_frames;
      engine->h2o_data.src_ratio =
	(double) usb_frames / (double) input_frames;
      engine->h2o_data.end_of_input = 1;

      const int res = src_simple (&engine->h2o_data, SRC_SINC_FASTEST,
				  engine->device->desc.inputs);
      if (res)
	{
	  error_print
	    ("h2o (v3): Error while resampling %ld frames (%zu B, ratio %f): %s",
	     input_frames, bytes, engine->h2o_data.src_ratio,
	     src_strerror (res));
	  memset (engine->h2o_transfer_buf, 0, usb_bytes);
	}
    }

  write_usb_output_xfr_v3 (engine, xfr, usb_frames);
}

static void LIBUSB_CALL
cb_xfr_audio_in_v3 (struct libusb_transfer *xfr)
{
  struct ow_engine *engine = xfr->user_data;

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      if (engine->context->options & OW_ENGINE_OPTION_O2H_AUDIO)
	set_usb_input_data_xfr_v3 (engine, xfr);
    }
  else if (xfr->status != LIBUSB_TRANSFER_CANCELLED)
    {
      error_print ("o2h (v3): transfer failed: %s",
		   libusb_error_name (xfr->status));
    }

  if (ow_engine_get_status (engine) > OW_ENGINE_STATUS_STOP)
    {
      int err = libusb_submit_transfer (xfr);
      if (err)
	{
	  error_print ("o2h (v3): resubmit failed: %s",
		       libusb_error_name (err));
	  ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
	}
    }
}

static void LIBUSB_CALL
cb_xfr_audio_out_v3 (struct libusb_transfer *xfr)
{
  struct ow_engine *engine = xfr->user_data;

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      /*
       * Clock reflection is part of the USB protocol, not JACK audio state.
       * Start following the O2H 144/145 cadence as soon as V3 synchronizes.
       * set_usb_output_data_xfr_v3() emits silence until the H2O ring is
       * actually running, but still keeps headers/counters correctly paced.
       *
       * The three queued OUT URBs provide the remaining three transfers of
       * the six-transfer delay measured in the official capture.
       */
      set_usb_output_data_xfr_v3 (engine, xfr);
    }
  else if (xfr->status != LIBUSB_TRANSFER_CANCELLED)
    {
      error_print ("h2o (v3): transfer failed: %s",
		   libusb_error_name (xfr->status));
    }

  if (ow_engine_get_status (engine) > OW_ENGINE_STATUS_STOP)
    {
      const int err = libusb_submit_transfer (xfr);

      if (err)
	{
	  error_print ("h2o (v3): resubmit failed: %s",
		       libusb_error_name (err));
	  ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
	}
    }
}

void
ow_engine_set_overbridge_name (struct ow_engine *engine, const char *name)
{
  int err;
  uint8_t *dst;

  libusb_fill_control_setup (engine->usb.xfr_control_out_data,
			     LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR
			     | LIBUSB_RECIPIENT_DEVICE, 1, 0, 0,
			     OB_NAME_MAX_LEN);

  dst =
    &engine->usb.xfr_control_out_data[sizeof (struct libusb_control_setup)];
  memcpy (dst, name, OB_NAME_MAX_LEN);

  libusb_fill_control_transfer (engine->usb.xfr_control_out,
				engine->usb.device_handle,
				engine->usb.xfr_control_out_data,
				cb_xfr_control_out, engine,
				engine->usb.xfr_timeout);

  err = libusb_submit_transfer (engine->usb.xfr_control_out);
  if (err)
    {
      error_print ("Error when submitting USB control transfer: %s",
		   libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
    }
}

const char *
ow_engine_get_overbridge_name (struct ow_engine *engine)
{
  return engine->overbridge_name;
}

static int
ow_hotplug_callback (struct libusb_context *ctx, struct libusb_device *device,
		     libusb_hotplug_event event, void *user_data)
{
  ow_hotplug_callback_t cb = user_data;
  static libusb_device_handle *dev_handle = NULL;
  struct libusb_device_descriptor desc;
  int rc;

  libusb_get_device_descriptor (device, &desc);

  if (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED == event)
    {
      debug_print (1, "USB hotplug: device arrived");
      rc = libusb_open (device, &dev_handle);
      if (rc == LIBUSB_SUCCESS)
	{
	  struct ow_device *ow_device;
	  if (!ow_get_device_from_device_attrs (-1, libusb_get_bus_number
						(device),
						libusb_get_device_address
						(device), &ow_device))
	    {
	      cb (ow_device);
	    }
	}
      else
	{
	  error_print ("Could not open USB device\n");
	}
    }
  else if (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT == event)
    {
      debug_print (1, "USB hotplug: device left");
    }
  else
    {
      debug_print (1, "Unhandled event %d", event);
    }

  return 0;
}

int
ow_hotplug_loop (int *running, pthread_spinlock_t *lock,
		 ow_hotplug_callback_t cb)
{
  libusb_hotplug_callback_handle callback_handle;
  int rc, end;

  struct timeval tv = { 1, 0UL };

#if LIBUSBX_API_VERSION >= 0x0100010A
  if (libusb_init_context (NULL, NULL, 0))
    {
      return OW_USB_ERROR_LIBUSB_INIT_FAILED;
    }
#else
  if (libusb_init (NULL) != LIBUSB_SUCCESS)
    {
      return OW_USB_ERROR_LIBUSB_INIT_FAILED;
    }
#endif

  debug_print (1, "Registering USB hotplug callback...");

  rc = libusb_hotplug_register_callback (NULL,
					 LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
					 LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0,
					 ELEKTRON_VID,
					 LIBUSB_HOTPLUG_MATCH_ANY,
					 LIBUSB_HOTPLUG_MATCH_ANY,
					 ow_hotplug_callback, cb,
					 &callback_handle);
  if (LIBUSB_SUCCESS != rc)
    {
      error_print ("Error creating a hotplug callback");
      libusb_exit (NULL);
      return OW_USB_ERROR_LIBUSB_INIT_FAILED;
    }

  while (1)
    {
      libusb_handle_events_timeout_completed (NULL, &tv, NULL);

      pthread_spin_lock (lock);
      end = !*running;
      pthread_spin_unlock (lock);

      if (end)
	{
	  break;
	}
    }

  debug_print (1, "Deregistering USB hotplug callback...");

  libusb_hotplug_deregister_callback (NULL, callback_handle);
  libusb_exit (NULL);

  return 0;
}
