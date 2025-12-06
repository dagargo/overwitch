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

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <endian.h>
#include <time.h>
#include <unistd.h>
#include "engine.h"

#define AUDIO_OUT_EP 0x03
#define AUDIO_OUT_INTERFACE 2
#define AUDIO_OUT_ALT_SETTING 3

#define AUDIO_IN_EP  (AUDIO_OUT_EP | 0x80)
#define AUDIO_IN_INTERFACE 1
#define AUDIO_IN_ALT_SETTING 3

#define USB_CONTROL_LEN (sizeof (struct libusb_control_setup) + OB_NAME_MAX_LEN)

static void prepare_cycle_in_audio (struct ow_engine *engine);
static void prepare_cycle_out_audio (struct ow_engine *engine);
static void ow_engine_load_overbridge_name (struct ow_engine *engine);

static void
ow_engine_init_name (struct ow_engine *engine)
{
  snprintf (engine->name, OW_ENGINE_NAME_MAX_LEN, "%s @ %03d,%03d",
	    engine->device->desc.name, engine->device->bus,
	    engine->device->address);
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

inline void
ow_engine_read_usb_input_blocks (struct ow_engine *engine)
{
  int32_t hv;
  uint8_t *s;
  struct ow_engine_usb_blk *blk;
  float *f = engine->o2h_transfer_buf;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_INPUT_USB_BLK (engine, i);
      s = (uint8_t *) blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine->device->desc.outputs; k++)
	    {
	      int size = engine->device->desc.output_tracks[k].size;

	      memcpy (&hv, s, size);

	      if (engine->device->desc.type == OW_DEVICE_TYPE_3 && size == 4)
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

static void
set_usb_input_data_blks (struct ow_engine *engine)
{
  size_t wso2h;
  ow_engine_status_t status;

  pthread_spin_lock (&engine->lock);
  if (engine->context->dll)
    {
      engine->context->dll_overbridge_update (engine->context->dll,
					      engine->frames_per_transfer,
					      engine->context->get_time ());
    }
  status = engine->status;
  pthread_spin_unlock (&engine->lock);

  ow_engine_read_usb_input_blocks (engine);

  if (status < OW_ENGINE_STATUS_RUN)
    {
      return;
    }

  wso2h = engine->context->write_space (engine->context->o2h_audio);
  if (engine->o2h_transfer_size <= wso2h)
    {
      engine->context->write (engine->context->o2h_audio,
			      (void *) engine->o2h_transfer_buf,
			      engine->o2h_transfer_size);
    }
  else
    {
      error_print ("o2h: Audio ring buffer overflow. Discarding data...");
    }

  pthread_spin_lock (&engine->lock);
  engine->latency_o2h =
    engine->context->read_space (engine->context->o2h_audio) /
    engine->o2h_frame_size;
  if (engine->latency_o2h > engine->latency_o2h_max)
    {
      engine->latency_o2h_max = engine->latency_o2h;
    }
  pthread_spin_unlock (&engine->lock);
}

inline void
ow_engine_write_usb_output_blocks (struct ow_engine *engine)
{
  int32_t ov;
  uint8_t *s;
  struct ow_engine_usb_blk *blk;
  float *f = engine->h2o_transfer_buf;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_OUTPUT_USB_BLK (engine, i);
      blk->frames = htobe16 (engine->usb.audio_frames_counter);
      engine->usb.audio_frames_counter += OB_FRAMES_PER_BLOCK;
      s = (uint8_t *) blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine->device->desc.inputs; k++)
	    {
	      int size = engine->device->desc.input_tracks[k].size;
	      ov = (int32_t) (*f * INT32_MAX);

	      if (engine->device->desc.type == OW_DEVICE_TYPE_3 && size == 4)
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

static void
set_usb_output_data_blks (struct ow_engine *engine)
{
  size_t rsh2o;
  size_t bytes;
  long frames;
  int res;
  int h2o_enabled = ow_engine_is_option (engine, OW_ENGINE_OPTION_H2O_AUDIO);

  if (h2o_enabled)
    {
      rsh2o = engine->context->read_space (engine->context->h2o_audio);
      if (!engine->reading_at_h2o_end)
	{
	  if (rsh2o >= engine->h2o_transfer_size &&
	      ow_engine_get_status (engine) == OW_ENGINE_STATUS_RUN)
	    {
	      bytes = ow_bytes_to_frame_bytes (rsh2o, engine->h2o_frame_size);
	      debug_print (3, "h2o: Emptying buffer (%zu B) and running...",
			   bytes);
	      engine->context->read (engine->context->h2o_audio, NULL, bytes);
	      engine->reading_at_h2o_end = 1;
	    }
	  goto set_blocks;
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
	  goto set_blocks;
	}
      return;
    }

  pthread_spin_lock (&engine->lock);
  engine->latency_h2o = rsh2o / engine->h2o_frame_size;
  if (engine->latency_h2o > engine->latency_h2o_max)
    {
      engine->latency_h2o_max = engine->latency_h2o;
    }
  pthread_spin_unlock (&engine->lock);

  if (rsh2o >= engine->h2o_transfer_size)
    {
      engine->context->read (engine->context->h2o_audio,
			     (void *) engine->h2o_transfer_buf,
			     engine->h2o_transfer_size);
    }
  else if (rsh2o > engine->h2o_frame_size)	//At least 2 frames to apply resampling to
    {
      debug_print (3,
		   "h2o: Audio ring buffer underflow (%zu B < %zu B). Fixed by resampling.",
		   rsh2o, engine->h2o_transfer_size);
      frames = rsh2o / engine->h2o_frame_size;
      bytes = frames * engine->h2o_frame_size;
      engine->context->read (engine->context->h2o_audio,
			     (void *) engine->h2o_resampler_buf, bytes);
      engine->h2o_data.input_frames = frames;
      engine->h2o_data.src_ratio =
	(double) engine->frames_per_transfer / frames;
      //We should NOT use the simple API but since this only happens very occasionally and mostly at startup, this has very low impact on audio quality.
      res = src_simple (&engine->h2o_data, SRC_SINC_FASTEST,
			engine->device->desc.inputs);
      if (res)
	{
	  error_print
	    ("h2o: Error while resampling %zu frames (%zu B, ratio %f): %s",
	     frames, bytes, engine->h2o_data.src_ratio, src_strerror (res));
	}
      else if (engine->h2o_data.output_frames_gen !=
	       engine->frames_per_transfer)
	{
	  error_print
	    ("h2o: Unexpected frames with ratio %f (output %ld, expected %d)",
	     engine->h2o_data.src_ratio, engine->h2o_data.output_frames_gen,
	     engine->frames_per_transfer);
	}

      // Any maximum value is invalid at this point
      pthread_spin_lock (&engine->lock);
      engine->latency_o2h_max = engine->latency_o2h_min;
      pthread_spin_unlock (&engine->lock);
    }
  else
    {
      debug_print (3, "h2o: Not enough data (%zu B). Waiting...", rsh2o);
      memset (engine->h2o_transfer_buf, 0, engine->h2o_transfer_size);
    }

set_blocks:
  ow_engine_write_usb_output_blocks (engine);
}

static void LIBUSB_CALL
cb_xfr_audio_in (struct libusb_transfer *xfr)
{
  struct ow_engine *engine = xfr->user_data;

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      if (xfr->length < xfr->actual_length)
	{
	  error_print
	    ("o2h: incomplete USB audio transfer (%d B < %d B)", xfr->length,
	     xfr->actual_length);
	}

      struct ow_engine *engine = xfr->user_data;
      if (engine->context->options & OW_ENGINE_OPTION_O2H_AUDIO)
	{
	  set_usb_input_data_blks (engine);
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
      prepare_cycle_in_audio (xfr->user_data);
    }
}

static void LIBUSB_CALL
cb_xfr_audio_out (struct libusb_transfer *xfr)
{
  struct ow_engine *engine = xfr->user_data;

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      if (xfr->length < xfr->actual_length)
	{
	  error_print
	    ("h2o: incomplete USB audio transfer (%d B < %d B)", xfr->length,
	     xfr->actual_length);
	}
    }
  else
    {
      error_print ("h2o: Error on USB audio transfer (%d B): %s",
		   xfr->actual_length, libusb_error_name (xfr->status));
    }

  set_usb_output_data_blks (xfr->user_data);

  if (ow_engine_get_status (engine) > OW_ENGINE_STATUS_STOP)
    {
      // We have to make sure that the out cycle is always started after its callback
      // Race condition on slower systems!
      prepare_cycle_out_audio (xfr->user_data);
    }
}

static void
prepare_cycle_out_audio (struct ow_engine *engine)
{
  libusb_fill_interrupt_transfer (engine->usb.xfr_audio_out,
				  engine->usb.device_handle, AUDIO_OUT_EP,
				  engine->usb.xfr_audio_out_data,
				  engine->usb.xfr_audio_out_data_len,
				  cb_xfr_audio_out, engine,
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
prepare_cycle_in_audio (struct ow_engine *engine)
{
  libusb_fill_interrupt_transfer (engine->usb.xfr_audio_in,
				  engine->usb.device_handle, AUDIO_IN_EP,
				  engine->usb.xfr_audio_in_data,
				  engine->usb.xfr_audio_in_data_len,
				  cb_xfr_audio_in, engine,
				  engine->usb.xfr_timeout);

  int err = libusb_submit_transfer (engine->usb.xfr_audio_in);
  if (err)
    {
      error_print ("o2h: Error when submitting USB audio in transfer: %s",
		   libusb_strerror (err));
      ow_engine_set_status (engine, OW_ENGINE_STATUS_ERROR);
    }
}

static void
usb_shutdown (struct ow_engine *engine)
{
  libusb_release_interface (engine->usb.device_handle, 1);
  libusb_release_interface (engine->usb.device_handle, 3);
  libusb_close (engine->usb.device_handle);
  libusb_unref_device (engine->usb.device);
  libusb_free_transfer (engine->usb.xfr_audio_in);
  libusb_free_transfer (engine->usb.xfr_audio_out);
  libusb_free_transfer (engine->usb.xfr_control_in);
  libusb_free_transfer (engine->usb.xfr_control_out);
  libusb_exit (engine->usb.context);
}

int
ow_engine_init_mem (struct ow_engine *engine,
		    unsigned int blocks_per_transfer)
{
  size_t size;
  struct ow_engine_usb_blk *blk;

  engine->context = NULL;

  pthread_spin_init (&engine->lock, PTHREAD_PROCESS_SHARED);

  engine->blocks_per_transfer = blocks_per_transfer;
  debug_print (1, "Blocks per transfer: %u", engine->blocks_per_transfer);

  engine->frames_per_transfer =
    OB_FRAMES_PER_BLOCK * engine->blocks_per_transfer;

  engine->o2h_frame_size =
    ow_get_frame_size_from_desc_tracks (engine->device->desc.outputs,
					engine->device->desc.output_tracks);

  engine->h2o_frame_size =
    ow_get_frame_size_from_desc_tracks (engine->device->desc.inputs,
					engine->device->desc.input_tracks);


  debug_print (2, "o2h: USB in frame size: %zu B", engine->o2h_frame_size);
  debug_print (2, "h2o: USB out frame size: %zu B", engine->h2o_frame_size);

  size = sizeof (struct ow_engine_usb_blk) + OB_FRAMES_PER_BLOCK *
    engine->o2h_frame_size;
  if (engine->usb.audio_in_blk_len && engine->usb.audio_in_blk_len != size)
    {
      error_print ("Unexpected audio block size (%lu != %zu)",
		   engine->usb.audio_in_blk_len, size);
      return OW_USB_UNEXPECTED_PACKET_SIZE;
    }
  else
    {
      engine->usb.audio_in_blk_len = size;
    }

  size = sizeof (struct ow_engine_usb_blk) + OB_FRAMES_PER_BLOCK *
    engine->h2o_frame_size;
  if (engine->usb.audio_out_blk_len && engine->usb.audio_out_blk_len != size)
    {
      error_print ("Unexpected audio block size (%lu != %zu)",
		   engine->usb.audio_out_blk_len, size);
      return OW_USB_UNEXPECTED_PACKET_SIZE;
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
    engine->device->desc.outputs * OW_BYTES_PER_SAMPLE;
  engine->h2o_transfer_size = engine->frames_per_transfer *
    engine->device->desc.inputs * OW_BYTES_PER_SAMPLE;

  debug_print (2, "o2h: audio transfer size: %zu B",
	       engine->o2h_transfer_size);
  debug_print (2, "h2o: audio transfer size: %zu B",
	       engine->h2o_transfer_size);

  engine->latency_o2h_min = engine->frames_per_transfer;
  engine->latency_h2o_min = engine->frames_per_transfer;

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

  engine->h2o_transfer_buf = malloc (engine->h2o_transfer_size);
  engine->o2h_transfer_buf = malloc (engine->o2h_transfer_size);
  memset (engine->h2o_transfer_buf, 0, engine->h2o_transfer_size);
  memset (engine->o2h_transfer_buf, 0, engine->o2h_transfer_size);

  //o2h resampler
  engine->h2o_resampler_buf = malloc (engine->h2o_transfer_size);
  memset (engine->h2o_resampler_buf, 0, engine->h2o_transfer_size);
  engine->h2o_data.data_in = engine->h2o_resampler_buf;
  engine->h2o_data.data_out = engine->h2o_transfer_buf;
  engine->h2o_data.end_of_input = 1;
  engine->h2o_data.input_frames = engine->frames_per_transfer;
  engine->h2o_data.output_frames = engine->frames_per_transfer;

  //Control
  engine->usb.xfr_control_out_data = malloc (USB_CONTROL_LEN);
  engine->usb.xfr_control_in_data = malloc (OB_NAME_MAX_LEN);

  return OW_OK;
}

// initialization taken from sniffed session

static ow_err_t
ow_engine_init (struct ow_engine *engine, struct ow_device *device,
		unsigned int blocks_per_transfer, unsigned int xfr_timeout)
{
  int err;
  ow_err_t ret = OW_OK;

  engine->status = OW_ENGINE_STATUS_STOP;
  engine->device = device;
  engine->usb.xfr_audio_in = NULL;
  engine->usb.xfr_audio_out = NULL;
  engine->usb.xfr_control_in = NULL;
  engine->usb.xfr_control_out = NULL;

  engine->usb.xfr_timeout = xfr_timeout;
  debug_print (1, "USB transfer timeout: %u", engine->usb.xfr_timeout);

  libusb_detach_kernel_driver (engine->usb.device_handle, 4);
  libusb_detach_kernel_driver (engine->usb.device_handle, 5);

  err = libusb_set_configuration (engine->usb.device_handle, 1);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_USB_CONFIG;
      goto end;
    }

  err = libusb_claim_interface (engine->usb.device_handle,
				AUDIO_IN_INTERFACE);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (engine->usb.device_handle,
					  AUDIO_IN_INTERFACE,
					  AUDIO_IN_ALT_SETTING);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (engine->usb.device_handle,
				AUDIO_OUT_INTERFACE);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (engine->usb.device_handle,
					  AUDIO_OUT_INTERFACE,
					  AUDIO_OUT_ALT_SETTING);
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

  err = prepare_transfers (engine);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_PREPARE_TRANSFER;
      goto end;
    }

  libusb_attach_kernel_driver (engine->usb.device_handle, 4);
  libusb_attach_kernel_driver (engine->usb.device_handle, 5);

#if LIBUSB_API_VERSION >= 0x0100010A
  engine->usb.audio_in_blk_len =
    libusb_get_max_alt_packet_size (engine->usb.device, AUDIO_IN_INTERFACE,
				    AUDIO_IN_ALT_SETTING, AUDIO_IN_EP);

  engine->usb.audio_out_blk_len =
    libusb_get_max_alt_packet_size (engine->usb.device, AUDIO_OUT_INTERFACE,
				    AUDIO_OUT_ALT_SETTING, AUDIO_OUT_EP);
#else
  engine->usb.audio_in_blk_len = 0;
  engine->usb.audio_out_blk_len = 0;
#endif

  err = LIBUSB_SUCCESS;
  ret = ow_engine_init_mem (engine, blocks_per_transfer);

end:
  if (ret != OW_OK)
    {
      usb_shutdown (engine);
      free (engine);
      error_print ("%s (%s)", ow_get_err_str (ret), libusb_error_name (err));
    }

  return ret;
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
  "'dll' not set in context"
};

static void *
run_audio (void *data)
{
  int err;
  size_t rsh2o, bytes;
  struct timeval tv = { 1, 0UL };
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
  prepare_cycle_in_audio (engine);
  prepare_cycle_out_audio (engine);

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
      return NULL;
    }
  engine->status = OW_ENGINE_STATUS_BOOT;
  pthread_spin_unlock (&engine->lock);

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
	  return NULL;
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
      bytes = ow_bytes_to_frame_bytes (rsh2o, engine->h2o_frame_size);
      engine->context->read (engine->context->h2o_audio, NULL, bytes);
      memset (engine->h2o_transfer_buf, 0, engine->h2o_transfer_size);
    }

  // status == OW_ENGINE_STATUS_STOP || status == OW_ENGINE_STATUS_ERROR

  //Handle completed events but not actually processed.
  //No new transfers will be submitted due to the status.
  debug_print (2, "Processing remaining events...");
  libusb_handle_events_timeout_completed (engine->usb.context, &tv, NULL);

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
  free (engine->usb.xfr_audio_in_data);
  free (engine->usb.xfr_audio_out_data);
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
    {
      error_print ("Error on USB control out transfer (%d B): %s",
		   xfr->actual_length, libusb_error_name (xfr->status));
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
	  if (!ow_get_device_from_device_attrs (-1, NULL,
						libusb_get_bus_number
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
