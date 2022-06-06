/*
 * engine.c
 * Copyright (C) 2019 Stefan Rehm <droelfdroelf@gmail.com>
 * Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <endian.h>
#include <time.h>
#include <unistd.h>
#include "engine.h"

#define AUDIO_OUT_EP 0x03
#define AUDIO_IN_EP  (AUDIO_OUT_EP | 0x80)

#define MIDI_OUT_EP 0x01
#define MIDI_IN_EP  (MIDI_OUT_EP | 0x80)

#define XFR_TIMEOUT 5

#define MIDI_BUF_EVENTS 64
#define MIDI_BUF_LEN (MIDI_BUF_EVENTS * OB_MIDI_EVENT_SIZE)

#define USB_BULK_MIDI_LEN 512

#define USB_CONTROL_LEN (sizeof (struct libusb_control_setup) + OB_NAME_MAX_LEN)

#define SAMPLE_TIME_NS (1e9 / ((int)OB_SAMPLE_RATE))

#define INT32_TO_FLOAT32_SCALE ((float) (1.0f / INT_MAX))

static void prepare_cycle_in_audio ();
static void prepare_cycle_out_audio ();
static void prepare_cycle_in_midi ();
static void ow_engine_load_overbridge_name (struct ow_engine *);

static void
ow_engine_init_name (struct ow_engine *engine, uint8_t bus, uint8_t address)
{
  snprintf (engine->name, OW_LABEL_MAX_LEN, "%s@%03d,%03d",
	    engine->device_desc->name, bus, address);
  ow_engine_load_overbridge_name (engine);
}

static int
prepare_transfers (struct ow_engine *engine)
{
  engine->usb.xfr_audio_in = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_audio_in)
    {
      return -ENOMEM;
    }

  engine->usb.xfr_audio_out = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_audio_out)
    {
      return -ENOMEM;
    }

  engine->usb.xfr_midi_in = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_midi_in)
    {
      return -ENOMEM;
    }

  engine->usb.xfr_midi_out = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_midi_out)
    {
      return -ENOMEM;
    }

  engine->usb.xfr_control_in = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_control_in)
    {
      return -ENOMEM;
    }

  engine->usb.xfr_control_out = libusb_alloc_transfer (0);
  if (!engine->usb.xfr_control_out)
    {
      return -ENOMEM;
    }

  return LIBUSB_SUCCESS;
}

static void
free_transfers (struct ow_engine *engine)
{
  libusb_free_transfer (engine->usb.xfr_audio_in);
  libusb_free_transfer (engine->usb.xfr_audio_out);
  libusb_free_transfer (engine->usb.xfr_midi_in);
  libusb_free_transfer (engine->usb.xfr_midi_out);
  libusb_free_transfer (engine->usb.xfr_control_in);
  libusb_free_transfer (engine->usb.xfr_control_out);
}

inline void
ow_engine_read_usb_input_blocks (struct ow_engine *engine)
{
  int32_t hv;
  int32_t *s;
  struct ow_engine_usb_blk *blk;
  float *f = engine->o2p_transfer_buf;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_INPUT_USB_BLK (engine, i);
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine->device_desc->outputs; k++)
	    {
	      hv = be32toh (*s);
	      *f = INT32_TO_FLOAT32_SCALE * hv;
	      f++;
	      s++;
	    }
	}
    }
}

static void
set_usb_input_data_blks (struct ow_engine *engine)
{
  size_t wso2p;
  ow_engine_status_t status;

  pthread_spin_lock (&engine->lock);
  if (engine->context->dll)
    {
      ow_dll_overwitch_inc (engine->context->dll, engine->frames_per_transfer,
			    engine->context->get_time ());
    }
  status = engine->status;
  pthread_spin_unlock (&engine->lock);

  ow_engine_read_usb_input_blocks (engine);

  if (status < OW_ENGINE_STATUS_RUN)
    {
      return;
    }

  pthread_spin_lock (&engine->lock);
  engine->o2p_latency =
    engine->context->read_space (engine->context->o2p_audio);
  if (engine->o2p_latency > engine->o2p_max_latency)
    {
      engine->o2p_max_latency = engine->o2p_latency;
    }
  pthread_spin_unlock (&engine->lock);

  wso2p = engine->context->write_space (engine->context->o2p_audio);
  if (engine->o2p_transfer_size <= wso2p)
    {
      engine->context->write (engine->context->o2p_audio,
			      (void *) engine->o2p_transfer_buf,
			      engine->o2p_transfer_size);
    }
  else
    {
      error_print ("o2p: Audio ring buffer overflow. Discarding data...\n");
    }
}

inline void
ow_engine_write_usb_output_blocks (struct ow_engine *engine)
{
  int32_t ov;
  int32_t *s;
  struct ow_engine_usb_blk *blk;
  float *f = engine->p2o_transfer_buf;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_OUTPUT_USB_BLK (engine, i);
      blk->frames = htobe16 (engine->usb.audio_frames_counter);
      engine->usb.audio_frames_counter += OB_FRAMES_PER_BLOCK;
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine->device_desc->inputs; k++)
	    {
	      ov = htobe32 ((int32_t) (*f * INT_MAX));
	      *s = ov;
	      f++;
	      s++;
	    }
	}
    }
}

static void
set_usb_output_data_blks (struct ow_engine *engine)
{
  size_t rsp2o;
  size_t bytes;
  long frames;
  int res;

  rsp2o = engine->context->read_space (engine->context->p2o_audio);
  if (!engine->reading_at_p2o_end)
    {
      if (rsp2o >= engine->p2o_transfer_size &&
	  ow_engine_get_status (engine) == OW_ENGINE_STATUS_RUN)
	{
	  bytes = ow_bytes_to_frame_bytes (rsp2o, engine->p2o_frame_size);
	  debug_print (2, "p2o: Emptying buffer (%zu B) and running...\n",
		       bytes);
	  engine->context->read (engine->context->p2o_audio, NULL, bytes);
	  engine->reading_at_p2o_end = 1;
	}
      goto set_blocks;
    }

  pthread_spin_lock (&engine->lock);
  engine->p2o_latency = rsp2o;
  if (engine->p2o_latency > engine->p2o_max_latency)
    {
      engine->p2o_max_latency = engine->p2o_latency;
    }
  pthread_spin_unlock (&engine->lock);

  if (rsp2o >= engine->p2o_transfer_size)
    {
      engine->context->read (engine->context->p2o_audio,
			     (void *) engine->p2o_transfer_buf,
			     engine->p2o_transfer_size);
    }
  else if (rsp2o > engine->p2o_frame_size)	//At least 2 frames to apply resampling to
    {
      debug_print (2,
		   "p2o: Audio ring buffer underflow (%zu B < %zu B). Resampling...\n",
		   rsp2o, engine->p2o_transfer_size);
      frames = rsp2o / engine->p2o_frame_size;
      bytes = frames * engine->p2o_frame_size;
      engine->context->read (engine->context->p2o_audio,
			     (void *) engine->p2o_resampler_buf, bytes);
      engine->p2o_data.input_frames = frames;
      engine->p2o_data.src_ratio =
	(double) engine->frames_per_transfer / frames;
      //We should NOT use the simple API but since this only happens very occasionally and mostly at startup, this has very low impact on audio quality.
      res = src_simple (&engine->p2o_data, SRC_SINC_FASTEST,
			engine->device_desc->inputs);
      if (res)
	{
	  error_print
	    ("p2o: Error while resampling %zu frames (%zu B, ratio %f): %s\n",
	     frames, bytes, engine->p2o_data.src_ratio, src_strerror (res));
	}
      else if (engine->p2o_data.output_frames_gen !=
	       engine->frames_per_transfer)
	{
	  error_print
	    ("p2o: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	     engine->p2o_data.src_ratio, engine->p2o_data.output_frames_gen,
	     engine->frames_per_transfer);
	}
    }
  else
    {
      debug_print (2, "p2o: Not enough data (%zu B). Waiting...\n", rsp2o);
      memset (engine->p2o_transfer_buf, 0, engine->p2o_transfer_size);
    }

set_blocks:
  ow_engine_write_usb_output_blocks (engine);
}

static void LIBUSB_CALL
cb_xfr_audio_in (struct libusb_transfer *xfr)
{
  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      struct ow_engine *engine = xfr->user_data;
      if (engine->context->options & OW_ENGINE_OPTION_O2P_AUDIO)
	{
	  set_usb_input_data_blks (engine);
	}
    }
  else
    {
      error_print ("o2p: Error on USB audio transfer: %s\n",
		   libusb_strerror (xfr->status));
    }
  // start new cycle even if this one did not succeed
  prepare_cycle_in_audio (xfr->user_data);
}

static void LIBUSB_CALL
cb_xfr_audio_out (struct libusb_transfer *xfr)
{
  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      if (xfr->length < xfr->actual_length)
	{
	  error_print
	    ("p2o: incomplete USB audio transfer (%d B < %d B)\n",
	     xfr->length, xfr->actual_length);
	}
    }
  else
    {
      error_print ("p2o: Error on USB audio transfer: %s\n",
		   libusb_strerror (xfr->status));
    }

  struct ow_engine *engine = xfr->user_data;
  if (engine->context->options & OW_ENGINE_OPTION_P2O_AUDIO)
    {
      set_usb_output_data_blks (engine);
    }

  // We have to make sure that the out cycle is always started after its callback
  // Race condition on slower systems!
  prepare_cycle_out_audio (xfr->user_data);
}

static void LIBUSB_CALL
cb_xfr_midi_in (struct libusb_transfer *xfr)
{
  struct ow_midi_event event;
  int length;
  struct ow_engine *engine = xfr->user_data;

  if (ow_engine_get_status (engine) < OW_ENGINE_STATUS_RUN)
    {
      goto end;
    }

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      length = 0;
      event.time = engine->context->get_time ();

      while (length < xfr->actual_length)
	{
	  memcpy (event.bytes, &engine->usb.xfr_midi_in_data[length],
		  OB_MIDI_EVENT_SIZE);
	  //Note-off, Note-on, Poly-KeyPress, Control Change, Program Change, Channel Pressure, PitchBend Change, Single Byte
	  if (event.bytes[0] >= 0x08 && event.bytes[0] <= 0x0f)
	    {
	      debug_print (2, "o2p MIDI: %02x, %02x, %02x, %02x (%f)\n",
			   event.bytes[0], event.bytes[1], event.bytes[2],
			   event.bytes[3], event.time);

	      if (engine->context->write_space (engine->context->o2p_midi) >=
		  sizeof (struct ow_midi_event))
		{
		  engine->context->write (engine->context->o2p_midi,
					  (void *) &event,
					  sizeof (struct ow_midi_event));
		}
	      else
		{
		  error_print
		    ("o2p: MIDI ring buffer overflow. Discarding data...\n");
		}
	    }
	  length += OB_MIDI_EVENT_SIZE;
	}
    }
  else
    {
      if (xfr->status != LIBUSB_TRANSFER_TIMED_OUT)
	{
	  error_print ("Error on USB MIDI in transfer: %s\n",
		       libusb_strerror (xfr->status));
	}
    }

end:
  prepare_cycle_in_midi (engine);
}

static void LIBUSB_CALL
cb_xfr_midi_out (struct libusb_transfer *xfr)
{
  struct ow_engine *engine = xfr->user_data;

  pthread_spin_lock (&engine->p2o_midi_lock);
  engine->p2o_midi_ready = 1;
  pthread_spin_unlock (&engine->p2o_midi_lock);

  if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
    {
      error_print ("Error on USB MIDI out transfer: %s\n",
		   libusb_strerror (xfr->status));
    }
}

static void
prepare_cycle_out_audio (struct ow_engine *engine)
{
  libusb_fill_interrupt_transfer (engine->usb.xfr_audio_out,
				  engine->usb.device_handle, AUDIO_OUT_EP,
				  engine->usb.xfr_audio_out_data,
				  engine->usb.xfr_audio_out_data_len,
				  cb_xfr_audio_out, engine, XFR_TIMEOUT);

  int err = libusb_submit_transfer (engine->usb.xfr_audio_out);
  if (err)
    {
      error_print ("p2o: Error when submitting USB audio transfer: %s\n",
		   libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
    }
}

static void
prepare_cycle_in_audio (struct ow_engine *engine)
{
  libusb_fill_interrupt_transfer (engine->usb.xfr_audio_in,
				  engine->usb.device_handle, AUDIO_IN_EP,
				  engine->usb.xfr_audio_in_data,
				  engine->usb.xfr_audio_in_data_len,
				  cb_xfr_audio_in, engine, XFR_TIMEOUT);

  int err = libusb_submit_transfer (engine->usb.xfr_audio_in);
  if (err)
    {
      error_print ("o2p: Error when submitting USB audio in transfer: %s\n",
		   libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
    }
}

static void
prepare_cycle_in_midi (struct ow_engine *engine)
{
  libusb_fill_bulk_transfer (engine->usb.xfr_midi_in,
			     engine->usb.device_handle, MIDI_IN_EP,
			     engine->usb.xfr_midi_in_data,
			     USB_BULK_MIDI_LEN, cb_xfr_midi_in, engine,
			     XFR_TIMEOUT);

  int err = libusb_submit_transfer (engine->usb.xfr_midi_in);
  if (err)
    {
      error_print ("o2p: Error when submitting USB MIDI transfer: %s\n",
		   libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
    }
}

static void
prepare_cycle_out_midi (struct ow_engine *engine)
{
  libusb_fill_bulk_transfer (engine->usb.xfr_midi_out,
			     engine->usb.device_handle, MIDI_OUT_EP,
			     engine->usb.xfr_midi_out_data,
			     USB_BULK_MIDI_LEN, cb_xfr_midi_out, engine,
			     XFR_TIMEOUT);

  int err = libusb_submit_transfer (engine->usb.xfr_midi_out);
  if (err)
    {
      error_print ("p2o: Error when submitting USB MIDI transfer: %s\n",
		   libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
    }
}

static void
usb_shutdown (struct ow_engine *engine)
{
  libusb_release_interface (engine->usb.device_handle, 1);
  libusb_release_interface (engine->usb.device_handle, 2);
  libusb_release_interface (engine->usb.device_handle, 3);
  libusb_close (engine->usb.device_handle);
  libusb_exit (engine->usb.context);
}

void
ow_engine_init_mem (struct ow_engine *engine, int blocks_per_transfer)
{
  struct ow_engine_usb_blk *blk;

  engine->context = NULL;

  pthread_spin_init (&engine->lock, PTHREAD_PROCESS_SHARED);

  engine->blocks_per_transfer = blocks_per_transfer;
  engine->frames_per_transfer =
    OB_FRAMES_PER_BLOCK * engine->blocks_per_transfer;

  engine->o2p_frame_size = OB_BYTES_PER_SAMPLE * engine->device_desc->outputs;
  engine->p2o_frame_size = OB_BYTES_PER_SAMPLE * engine->device_desc->inputs;

  debug_print (2, "o2p: USB in frame size: %zu B\n", engine->o2p_frame_size);
  debug_print (2, "p2o: USB out frame size: %zu B\n", engine->p2o_frame_size);

  engine->usb.audio_in_blk_len =
    sizeof (struct ow_engine_usb_blk) +
    OB_FRAMES_PER_BLOCK * engine->o2p_frame_size;
  engine->usb.audio_out_blk_len =
    sizeof (struct ow_engine_usb_blk) +
    OB_FRAMES_PER_BLOCK * engine->p2o_frame_size;

  debug_print (2, "o2p: USB in block size: %zu B\n",
	       engine->usb.audio_in_blk_len);
  debug_print (2, "p2o: USB out block size: %zu B\n",
	       engine->usb.audio_out_blk_len);

  engine->o2p_transfer_size =
    engine->frames_per_transfer * engine->o2p_frame_size;
  engine->p2o_transfer_size =
    engine->frames_per_transfer * engine->p2o_frame_size;

  debug_print (2, "o2p: audio transfer size: %zu B\n",
	       engine->o2p_transfer_size);
  debug_print (2, "p2o: audio transfer size: %zu B\n",
	       engine->p2o_transfer_size);

  engine->usb.audio_frames_counter = 0;
  engine->usb.xfr_audio_in_data_len =
    engine->usb.audio_in_blk_len * engine->blocks_per_transfer;
  engine->usb.xfr_audio_out_data_len =
    engine->usb.audio_out_blk_len * engine->blocks_per_transfer;
  engine->usb.xfr_audio_in_data = malloc (engine->usb.xfr_audio_in_data_len);
  engine->usb.xfr_audio_out_data =
    malloc (engine->usb.xfr_audio_out_data_len);
  memset (engine->usb.xfr_audio_in_data, 0,
	  engine->usb.xfr_audio_in_data_len);
  memset (engine->usb.xfr_audio_out_data, 0,
	  engine->usb.xfr_audio_out_data_len);

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_OUTPUT_USB_BLK (engine, i);
      blk->header = htobe16 (0x07ff);
    }

  engine->p2o_transfer_buf = malloc (engine->p2o_transfer_size);
  engine->o2p_transfer_buf = malloc (engine->o2p_transfer_size);
  memset (engine->p2o_transfer_buf, 0, engine->p2o_transfer_size);
  memset (engine->o2p_transfer_buf, 0, engine->o2p_transfer_size);

  //o2p resampler
  engine->p2o_resampler_buf = malloc (engine->p2o_transfer_size);
  memset (engine->p2o_resampler_buf, 0, engine->p2o_transfer_size);
  engine->p2o_data.data_in = engine->p2o_resampler_buf;
  engine->p2o_data.data_out = engine->p2o_transfer_buf;
  engine->p2o_data.end_of_input = 1;
  engine->p2o_data.input_frames = engine->frames_per_transfer;
  engine->p2o_data.output_frames = engine->frames_per_transfer;

  //MIDI
  engine->usb.xfr_midi_out_data = malloc (USB_BULK_MIDI_LEN);
  engine->usb.xfr_midi_in_data = malloc (USB_BULK_MIDI_LEN);
  memset (engine->usb.xfr_midi_out_data, 0, USB_BULK_MIDI_LEN);
  memset (engine->usb.xfr_midi_in_data, 0, USB_BULK_MIDI_LEN);
  pthread_spin_init (&engine->p2o_midi_lock, PTHREAD_PROCESS_SHARED);

  //Control
  engine->usb.xfr_control_out_data = malloc (USB_CONTROL_LEN);
  engine->usb.xfr_control_in_data = malloc (OB_NAME_MAX_LEN);
}

// initialization taken from sniffed session

static ow_err_t
ow_engine_init (struct ow_engine *engine, int blocks_per_transfer)
{
  int err;
  ow_err_t ret = OW_OK;

  err = libusb_set_configuration (engine->usb.device_handle, 1);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_USB_CONFIG;
      goto end;
    }
  err = libusb_claim_interface (engine->usb.device_handle, 1);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (engine->usb.device_handle, 1, 3);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (engine->usb.device_handle, 2);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (engine->usb.device_handle, 2, 2);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (engine->usb.device_handle, 3);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (engine->usb.device_handle, 3, 0);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_clear_halt (engine->usb.device_handle, AUDIO_IN_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (engine->usb.device_handle, AUDIO_OUT_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (engine->usb.device_handle, MIDI_IN_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (engine->usb.device_handle, MIDI_OUT_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }
  err = prepare_transfers (engine);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_PREPARE_TRANSFER;
    }

end:
  if (ret == OW_OK)
    {
      ow_engine_init_mem (engine, blocks_per_transfer);
    }
  else
    {
      usb_shutdown (engine);
      free (engine);
      error_print ("Error while initializing device: %s\n",
		   libusb_error_name (ret));
    }
  return ret;
}

ow_err_t
ow_engine_init_from_libusb_device_descriptor (struct ow_engine **engine_,
					      int libusb_device_descriptor,
					      int blocks_per_transfer)
{
#ifdef LIBUSB_OPTION_WEAK_AUTHORITY
  ow_err_t err;
  uint8_t bus, address;
  struct ow_engine *engine;
  struct libusb_device *device;
  struct libusb_device_descriptor desc;

  if (libusb_set_option (NULL, LIBUSB_OPTION_WEAK_AUTHORITY, NULL) !=
      LIBUSB_SUCCESS)
    {
      return OW_USB_ERROR_LIBUSB_INIT_FAILED;
    }

  engine = malloc (sizeof (struct ow_engine));

  if (libusb_init (&engine->usb.context) != LIBUSB_SUCCESS)
    {
      err = OW_USB_ERROR_LIBUSB_INIT_FAILED;
      goto error;
    }

  if (libusb_wrap_sys_device (NULL, (intptr_t) libusb_device_descriptor,
			      &engine->usb.device_handle))
    {
      err = OW_USB_ERROR_LIBUSB_INIT_FAILED;
      goto error;
    }

  device = libusb_get_device (engine->usb.device_handle);
  libusb_get_device_descriptor (device, &desc);
  ow_get_device_desc_from_vid_pid (desc.idVendor, desc.idProduct,
				   &engine->device_desc);

  *engine_ = engine;
  err = ow_engine_init (engine, blocks_per_transfer);
  if (!err)
    {
      bus = libusb_get_bus_number (device);
      address = libusb_get_device_address (device);
      ow_engine_init_name (engine, bus, address);
      return err;
    }

error:
  free (engine);
  return err;
#else
  error_print
    ("The libusb version 0x%.8x does not support opening a device descriptor\n",
     LIBUSB_API_VERSION);
  return OW_GENERIC_ERROR;
#endif
}

ow_err_t
ow_engine_init_from_bus_address (struct ow_engine **engine_,
				 uint8_t bus, uint8_t address,
				 int blocks_per_transfer)
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

      if (ow_get_device_desc_from_vid_pid
	  (desc.idVendor, desc.idProduct, &engine->device_desc)
	  && libusb_get_bus_number (*device) == bus
	  && libusb_get_device_address (*device) == address)
	{
	  err = libusb_open (*device, &engine->usb.device_handle);
	  if (err)
	    {
	      error_print ("Error while opening device: %s\n",
			   libusb_error_name (err));
	    }

	  break;
	}
    }

  libusb_free_device_list (devices, total);

  if (!engine->usb.device_handle)
    {
      ret = OW_USB_ERROR_CANT_FIND_DEV;
      goto error;
    }

  *engine_ = engine;
  ret = ow_engine_init (engine, blocks_per_transfer);
  ow_engine_init_name (engine, bus, address);
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
  "'read_space' not set in context",
  "'write_space' not set in context",
  "'read' not set in context",
  "'write' not set in context",
  "'o2p_audio' not set in context",
  "'p2o_audio' not set in context",
  "'o2p_midi' not set in context",
  "'p2o_midi' not set in context",
  "'get_time' not set in context",
  "'dll' not set in context"
};

static void *
run_p2o_midi (void *data)
{
  int pos, p2o_midi_ready, event_read = 0;
  double last_time, diff;
  struct timespec sleep_time, smallest_sleep_time;
  struct ow_midi_event event;
  struct ow_engine *engine = data;

  smallest_sleep_time.tv_sec = 0;
  smallest_sleep_time.tv_nsec = SAMPLE_TIME_NS * 32 / 2;	//Average wait time for a 32 buffer sample

  pos = 0;
  diff = 0.0;
  last_time = engine->context->get_time ();
  engine->p2o_midi_ready = 1;
  while (1)
    {

      while (engine->context->read_space (engine->context->p2o_midi) >=
	     sizeof (struct ow_midi_event) && pos < USB_BULK_MIDI_LEN)
	{
	  if (!pos)
	    {
	      memset (engine->usb.xfr_midi_out_data, 0, USB_BULK_MIDI_LEN);
	      diff = 0;
	    }

	  if (!event_read)
	    {
	      engine->context->read (engine->context->p2o_midi,
				     (void *) &event,
				     sizeof (struct ow_midi_event));
	      event_read = 1;
	    }

	  if (event.time > last_time)
	    {
	      diff = event.time - last_time;
	      last_time = event.time;
	      break;
	    }

	  memcpy (&engine->usb.xfr_midi_out_data[pos], event.bytes,
		  OB_MIDI_EVENT_SIZE);
	  pos += OB_MIDI_EVENT_SIZE;
	  event_read = 0;
	}

      if (pos)
	{
	  debug_print (2, "Event frames: %f; diff: %f\n", event.time, diff);
	  engine->p2o_midi_ready = 0;
	  prepare_cycle_out_midi (engine);
	  pos = 0;
	}

      if (diff)
	{
	  sleep_time.tv_sec = diff;
	  sleep_time.tv_nsec = (diff - sleep_time.tv_sec) * 1.0e9;
	  nanosleep (&sleep_time, NULL);
	}
      else
	{
	  nanosleep (&smallest_sleep_time, NULL);
	}

      pthread_spin_lock (&engine->p2o_midi_lock);
      p2o_midi_ready = engine->p2o_midi_ready;
      pthread_spin_unlock (&engine->p2o_midi_lock);
      while (!p2o_midi_ready)
	{
	  nanosleep (&smallest_sleep_time, NULL);
	  pthread_spin_lock (&engine->p2o_midi_lock);
	  p2o_midi_ready = engine->p2o_midi_ready;
	  pthread_spin_unlock (&engine->p2o_midi_lock);
	};

      if (ow_engine_get_status (engine) <= OW_ENGINE_STATUS_STOP)
	{
	  break;
	}
    }

  return NULL;
}

static void *
run_audio_o2p_midi (void *data)
{
  size_t rsp2o, bytes;
  struct ow_engine *engine = data;

  while (ow_engine_get_status (engine) == OW_ENGINE_STATUS_READY);

  //status == OW_ENGINE_STATUS_BOOT

  //Both these calls always need to be called and can not be skipped.
  prepare_cycle_in_audio (engine);
  prepare_cycle_out_audio (engine);
  if (engine->context->options & OW_ENGINE_OPTION_O2P_MIDI)
    {
      prepare_cycle_in_midi (engine);
    }

  while (1)
    {
      engine->p2o_latency = 0;
      engine->p2o_max_latency = 0;
      engine->reading_at_p2o_end =
	engine->context->options & OW_ENGINE_OPTION_DLL ? 0 : 1;
      engine->o2p_latency = 0;
      engine->o2p_max_latency = 0;

      //status == OW_ENGINE_STATUS_BOOT

      pthread_spin_lock (&engine->lock);
      if (engine->context->dll)
	{
	  ow_dll_overwitch_init (engine->context->dll, OB_SAMPLE_RATE,
				 engine->frames_per_transfer,
				 engine->context->get_time ());
	  engine->status = OW_ENGINE_STATUS_WAIT;
	}
      else
	{
	  engine->status = OW_ENGINE_STATUS_RUN;
	}
      pthread_spin_unlock (&engine->lock);

      while (ow_engine_get_status (engine) >= OW_ENGINE_STATUS_WAIT)
	{
	  libusb_handle_events_completed (engine->usb.context, NULL);
	}

      if (ow_engine_get_status (engine) <= OW_ENGINE_STATUS_STOP)
	{
	  break;
	}

      debug_print (1, "Rebooting engine...\n");

      rsp2o = engine->context->read_space (engine->context->p2o_audio);
      bytes = ow_bytes_to_frame_bytes (rsp2o, engine->p2o_frame_size);
      engine->context->read (engine->context->p2o_audio, NULL, bytes);
      memset (engine->p2o_transfer_buf, 0, engine->p2o_transfer_size);
    }

  return NULL;
}

ow_err_t
ow_engine_start (struct ow_engine *engine, struct ow_context *context)
{
  int audio_o2p_midi_thread = 0;
  int p2o_midi_thread = 0;

  engine->context = context;

  if (!context->options)
    {
      return OW_GENERIC_ERROR;
    }

  if (context->options & OW_ENGINE_OPTION_O2P_AUDIO)
    {
      audio_o2p_midi_thread = 1;
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
      if (!context->o2p_audio)
	{
	  return OW_INIT_ERROR_NO_O2P_AUDIO_BUF;
	}
    }

  if (context->options & OW_ENGINE_OPTION_P2O_AUDIO)
    {
      audio_o2p_midi_thread = 1;
      if (!context->read_space)
	{
	  return OW_INIT_ERROR_NO_READ_SPACE;
	}
      if (!context->read)
	{
	  return OW_INIT_ERROR_NO_READ;
	}
      if (!context->p2o_audio)
	{
	  return OW_INIT_ERROR_NO_P2O_AUDIO_BUF;
	}
    }

  if (context->options & OW_ENGINE_OPTION_O2P_MIDI)
    {
      audio_o2p_midi_thread = 1;
      if (!context->get_time)
	{
	  return OW_INIT_ERROR_NO_GET_TIME;
	}
      if (!context->o2p_midi)
	{
	  return OW_INIT_ERROR_NO_O2P_MIDI_BUF;
	}
    }

  if (context->options & OW_ENGINE_OPTION_P2O_MIDI)
    {
      p2o_midi_thread = 1;
      if (!context->get_time)
	{
	  return OW_INIT_ERROR_NO_GET_TIME;
	}
      if (!context->p2o_midi)
	{
	  return OW_INIT_ERROR_NO_P2O_MIDI_BUF;
	}
    }

  if (context->options & OW_ENGINE_OPTION_DLL)
    {
      if (!context->get_time)
	{
	  return OW_INIT_ERROR_NO_GET_TIME;
	}
      if (!context->dll)
	{
	  return OW_INIT_ERROR_NO_DLL;
	}
      engine->status = OW_ENGINE_STATUS_READY;
    }

  if (!context->set_rt_priority)
    {
      context->set_rt_priority = ow_set_thread_rt_priority;
      context->priority = OW_DEFAULT_RT_PROPERTY;
    }

  if (p2o_midi_thread)
    {
      debug_print (1, "Starting p2o MIDI thread...\n");
      if (pthread_create (&engine->p2o_midi_thread, NULL, run_p2o_midi,
			  engine))
	{
	  error_print ("Could not start MIDI thread\n");
	  return OW_GENERIC_ERROR;
	}
      context->set_rt_priority (engine->p2o_midi_thread,
				engine->context->priority);
    }

  if (audio_o2p_midi_thread)
    {
      debug_print (1, "Starting audio and o2p MIDI thread...\n");
      if (pthread_create (&engine->audio_o2p_midi_thread, NULL,
			  run_audio_o2p_midi, engine))
	{
	  error_print ("Could not start device thread\n");
	  return OW_GENERIC_ERROR;
	}
      context->set_rt_priority (engine->audio_o2p_midi_thread,
				engine->context->priority);
    }

  return OW_OK;
}

inline void
ow_engine_wait (struct ow_engine *engine)
{
  pthread_join (engine->audio_o2p_midi_thread, NULL);
  if (engine->context->options & OW_ENGINE_OPTION_P2O_MIDI)
    {
      pthread_join (engine->p2o_midi_thread, NULL);
    }
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
  free_transfers (engine);
  ow_engine_free_mem (engine);
  free (engine);
}

void
ow_engine_free_mem (struct ow_engine *engine)
{
  free (engine->p2o_transfer_buf);
  free (engine->p2o_resampler_buf);
  free (engine->o2p_transfer_buf);
  free (engine->usb.xfr_audio_in_data);
  free (engine->usb.xfr_audio_out_data);
  free (engine->usb.xfr_midi_out_data);
  free (engine->usb.xfr_midi_in_data);
  free (engine->usb.xfr_control_out_data);
  free (engine->usb.xfr_control_in_data);
  pthread_spin_destroy (&engine->lock);
  pthread_spin_destroy (&engine->p2o_midi_lock);
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
  engine->status = status;
  pthread_spin_unlock (&engine->lock);
}

inline int
ow_bytes_to_frame_bytes (int bytes, int bytes_per_frame)
{
  int frames = bytes / bytes_per_frame;
  return frames * bytes_per_frame;
}

const struct ow_device_desc *
ow_engine_get_device_desc (struct ow_engine *engine)
{
  return engine->device_desc;
}

inline void
ow_engine_stop (struct ow_engine *engine)
{
  ow_engine_set_status (engine, OW_ENGINE_STATUS_STOP);
}

void
ow_engine_print_blocks (struct ow_engine *engine, char *blks, size_t blk_len)
{
  int32_t *s, v;
  struct ow_engine_usb_blk *blk;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_USB_BLK (blks, blk_len, i);
      printf ("Block %d\n", i);
      printf ("0x%04x | 0x%04x\n", be16toh (blk->header),
	      be16toh (blk->frames));
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine->device_desc->outputs; k++)
	    {
	      v = be32toh (*s);
	      printf ("Frame %2d, track %2d: %d\n", j, k, v);
	      s++;
	    }
	}
    }
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
      debug_print (1, "USB control in data (%d B): %s\n", res,
		   engine->usb.xfr_control_in_data);
      memcpy (engine->overbridge_name, engine->usb.xfr_control_in_data,
	      OB_NAME_MAX_LEN);
    }
  else
    {
      error_print ("Error on USB control in transfer: %s\n",
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
      debug_print (1, "USB control in data (%d B): %s\n", res,
		   engine->usb.xfr_control_in_data);
    }
  else
    {
      error_print ("Error on USB control in transfer: %s\n",
		   libusb_strerror (res));
    }
}

static void LIBUSB_CALL
cb_xfr_control_out (struct libusb_transfer *xfr)
{
  if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
    {
      error_print ("Error on USB control out transfer: %s\n",
		   libusb_strerror (xfr->status));
    }
}

void
ow_engine_set_overbridge_name (struct ow_engine *engine, const char *name)
{
  libusb_fill_control_setup (engine->usb.xfr_control_out_data,
			     LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR
			     | LIBUSB_RECIPIENT_DEVICE, 1, 0, 0,
			     OB_NAME_MAX_LEN);

  memcpy ((char *) &engine->
	  usb.xfr_control_out_data[sizeof (struct libusb_control_setup)],
	  name, OB_NAME_MAX_LEN);

  libusb_fill_control_transfer (engine->usb.xfr_control_out,
				engine->usb.device_handle,
				engine->usb.xfr_control_out_data,
				cb_xfr_control_out, engine, XFR_TIMEOUT);

  int err = libusb_submit_transfer (engine->usb.xfr_control_out);
  if (err)
    {
      error_print ("Error when submitting USB control transfer: %s\n",
		   libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
    }
}

const char *
ow_engine_get_overbridge_name (struct ow_engine *engine)
{
  return engine->overbridge_name;
}
