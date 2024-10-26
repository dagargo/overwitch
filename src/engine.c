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

#define USB_CONTROL_LEN (sizeof (struct libusb_control_setup) + OB_NAME_MAX_LEN)

#define INT32_TO_FLOAT32_SCALE ((float) (1.0f / INT_MAX))

static void prepare_cycle_in_audio ();
static void prepare_cycle_out_audio ();
static void ow_engine_load_overbridge_name (struct ow_engine *);

static void
ow_engine_init_name (struct ow_engine *engine, uint8_t bus, uint8_t address)
{
  snprintf (engine->name, OW_LABEL_MAX_LEN, "%s @ %03d,%03d",
	    engine->device_desc.name, bus, address);
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
  int32_t *s;
  struct ow_engine_usb_blk *blk;
  float *f = engine->o2h_transfer_buf;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_INPUT_USB_BLK (engine, i);
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine->device_desc.outputs; k++)
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
  engine->o2h_latency =
    engine->context->read_space (engine->context->o2h_audio);
  if (engine->o2h_latency > engine->o2h_max_latency)
    {
      engine->o2h_max_latency = engine->o2h_latency;
    }
  pthread_spin_unlock (&engine->lock);
}

inline void
ow_engine_write_usb_output_blocks (struct ow_engine *engine)
{
  int32_t ov;
  int32_t *s;
  struct ow_engine_usb_blk *blk;
  float *f = engine->h2o_transfer_buf;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_OUTPUT_USB_BLK (engine, i);
      blk->frames = htobe16 (engine->usb.audio_frames_counter);
      engine->usb.audio_frames_counter += OB_FRAMES_PER_BLOCK;
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine->device_desc.inputs; k++)
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
  size_t rsh2o;
  size_t bytes;
  long frames;
  int res;
  int h2o_enabled = ow_engine_is_option (engine, OW_ENGINE_OPTION_P2O_AUDIO);

  if (h2o_enabled)
    {
      rsh2o = engine->context->read_space (engine->context->h2o_audio);
      if (!engine->reading_at_h2o_end)
	{
	  if (rsh2o >= engine->h2o_transfer_size &&
	      ow_engine_get_status (engine) == OW_ENGINE_STATUS_RUN)
	    {
	      bytes = ow_bytes_to_frame_bytes (rsh2o, engine->h2o_frame_size);
	      debug_print (2, "h2o: Emptying buffer (%zu B) and running...",
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
	  debug_print (2, "h2o: Clearing buffer and stopping...");
	  memset (engine->h2o_transfer_buf, 0, engine->h2o_transfer_size);
	  engine->reading_at_h2o_end = 0;
	  engine->h2o_max_latency = 0;
	  goto set_blocks;
	}
      return;
    }

  pthread_spin_lock (&engine->lock);
  engine->h2o_latency = rsh2o;
  if (engine->h2o_latency > engine->h2o_max_latency)
    {
      engine->h2o_max_latency = engine->h2o_latency;
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
      debug_print (2,
		   "h2o: Audio ring buffer underflow (%zu B < %zu B). Resampling...",
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
			engine->device_desc.inputs);
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
    }
  else
    {
      debug_print (2, "h2o: Not enough data (%zu B). Waiting...", rsh2o);
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
      if (engine->context->options & OW_ENGINE_OPTION_O2P_AUDIO)
	{
	  set_usb_input_data_blks (engine);
	}
    }
  else
    {
      error_print ("o2h: Error on USB audio transfer: %s",
		   libusb_error_name (xfr->status));
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
      error_print ("h2o: Error on USB audio transfer: %s",
		   libusb_error_name (xfr->status));
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
  libusb_free_transfer (engine->usb.xfr_audio_in);
  libusb_free_transfer (engine->usb.xfr_audio_out);
  libusb_free_transfer (engine->usb.xfr_control_in);
  libusb_free_transfer (engine->usb.xfr_control_out);
  libusb_exit (engine->usb.context);
}

void
ow_engine_init_mem (struct ow_engine *engine,
		    unsigned int blocks_per_transfer)
{
  struct ow_engine_usb_blk *blk;

  engine->context = NULL;

  pthread_spin_init (&engine->lock, PTHREAD_PROCESS_SHARED);

  engine->blocks_per_transfer = blocks_per_transfer;
  debug_print (1, "Blocks per transfer: %u", engine->blocks_per_transfer);

  engine->frames_per_transfer =
    OB_FRAMES_PER_BLOCK * engine->blocks_per_transfer;

  engine->o2h_frame_size = OB_BYTES_PER_SAMPLE * engine->device_desc.outputs;
  engine->h2o_frame_size = OB_BYTES_PER_SAMPLE * engine->device_desc.inputs;

  engine->o2h_min_latency =
    engine->frames_per_transfer * engine->o2h_frame_size;
  engine->h2o_min_latency =
    engine->frames_per_transfer * engine->h2o_frame_size;

  debug_print (2, "o2h: USB in frame size: %zu B", engine->o2h_frame_size);
  debug_print (2, "h2o: USB out frame size: %zu B", engine->h2o_frame_size);

  engine->usb.audio_in_blk_len =
    sizeof (struct ow_engine_usb_blk) +
    OB_FRAMES_PER_BLOCK * engine->o2h_frame_size;
  engine->usb.audio_out_blk_len =
    sizeof (struct ow_engine_usb_blk) +
    OB_FRAMES_PER_BLOCK * engine->h2o_frame_size;

  debug_print (2, "o2h: USB in block size: %zu B",
	       engine->usb.audio_in_blk_len);
  debug_print (2, "h2o: USB out block size: %zu B",
	       engine->usb.audio_out_blk_len);

  engine->o2h_transfer_size =
    engine->frames_per_transfer * engine->o2h_frame_size;
  engine->h2o_transfer_size =
    engine->frames_per_transfer * engine->h2o_frame_size;

  debug_print (2, "o2h: audio transfer size: %zu B",
	       engine->o2h_transfer_size);
  debug_print (2, "h2o: audio transfer size: %zu B",
	       engine->h2o_transfer_size);

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
}

// initialization taken from sniffed session

static ow_err_t
ow_engine_init (struct ow_engine *engine, unsigned int blocks_per_transfer,
		unsigned int xfr_timeout)
{
  int err;
  ow_err_t ret = OW_OK;

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
  err = libusb_set_interface_alt_setting (engine->usb.device_handle, 2, 3);
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
    }

  libusb_attach_kernel_driver (engine->usb.device_handle, 4);
  libusb_attach_kernel_driver (engine->usb.device_handle, 5);

end:
  if (ret == OW_OK)
    {
      ow_engine_init_mem (engine, blocks_per_transfer);
    }
  else
    {
      usb_shutdown (engine);
      free (engine);
      error_print ("Error while initializing device: %s",
		   libusb_error_name (err));
    }
  return ret;
}

ow_err_t
ow_engine_init_from_libusb_device_descriptor (struct ow_engine **engine_,
					      int libusb_device_descriptor,
					      unsigned int blks_per_transfer,
					      unsigned int xfr_timeout)
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
  if (ow_get_device_desc_from_vid_pid (desc.idVendor, desc.idProduct,
				       &engine->device_desc))
    {
      err = OW_USB_ERROR_LIBUSB_INIT_FAILED;
      goto error;
    }

  *engine_ = engine;
  err = ow_engine_init (engine, blks_per_transfer, xfr_timeout);
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
    ("The libusb version 0x%.8x does not support opening a device descriptor",
     LIBUSB_API_VERSION);
  return OW_GENERIC_ERROR;
#endif
}

ow_err_t
ow_engine_init_from_bus_address (struct ow_engine **engine_,
				 uint8_t bus, uint8_t address,
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

      if (!ow_get_device_desc_from_vid_pid (desc.idVendor, desc.idProduct,
					    &engine->device_desc)
	  && libusb_get_bus_number (*device) == bus
	  && libusb_get_device_address (*device) == address)
	{
	  err = libusb_open (*device, &engine->usb.device_handle);
	  if (err)
	    {
	      error_print ("Error while opening device: %s",
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
  ret = ow_engine_init (engine, blocks_per_transfer, xfr_timeout);
  if (!ret)
    {
      ow_engine_init_name (engine, bus, address);
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
  size_t rsh2o, bytes;
  struct ow_engine *engine = data;

  if (engine->context->dll)
    {
      engine->context->dll_overbridge_init (engine->context->dll,
					    OB_SAMPLE_RATE,
					    engine->frames_per_transfer);
    }

  //status == OW_ENGINE_STATUS_STEADY

  //These calls are needed to initialize the Overbridge side before the host side.
  prepare_cycle_in_audio (engine);
  prepare_cycle_out_audio (engine);

  if (engine->context->dll)
    {
      engine->context->dll_overbridge_update (engine->context->dll,
					      engine->frames_per_transfer,
					      engine->context->get_time ());
    }

  ow_engine_set_status (engine, OW_ENGINE_STATUS_BOOT);

  while (1)
    {
      engine->h2o_latency = 0;
      engine->h2o_max_latency = 0;
      engine->reading_at_h2o_end = engine->context->dll ? 0 : 1;
      engine->o2h_latency = 0;
      engine->o2h_max_latency = 0;

      //status == OW_ENGINE_STATUS_BOOT || status == OW_ENGINE_STATUS_CLEAR

      if (ow_engine_get_status (engine) == OW_ENGINE_STATUS_CLEAR)
	{
	  engine->status = OW_ENGINE_STATUS_RUN;
	}

      pthread_spin_lock (&engine->lock);
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
	  libusb_handle_events_completed (engine->usb.context, NULL);
	}

      if (ow_engine_get_status (engine) < OW_ENGINE_STATUS_BOOT)
	{
	  break;
	}

      //status == OW_ENGINE_STATUS_BOOT || status == OW_ENGINE_STATUS_CLEAR

      debug_print (1, "Clearing buffers...");

      rsh2o = engine->context->read_space (engine->context->h2o_audio);
      bytes = ow_bytes_to_frame_bytes (rsh2o, engine->h2o_frame_size);
      engine->context->read (engine->context->h2o_audio, NULL, bytes);
      memset (engine->h2o_transfer_buf, 0, engine->h2o_transfer_size);
    }

  //status == OW_ENGINE_STATUS_STOP || status == OW_ENGINE_STATUS_ERROR

  //Handle completed events but not actually processed.
  //No new transfers will be submitted due to the status.
  debug_print (2, "Processing remaining event...");
  libusb_handle_events_completed (engine->usb.context, NULL);

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

ow_err_t
ow_engine_start (struct ow_engine *engine, struct ow_context *context)
{
  int audio_thread = 0;

  engine->context = context;

  if (!context->options)
    {
      return OW_GENERIC_ERROR;
    }

  if (context->options & OW_ENGINE_OPTION_O2P_AUDIO)
    {
      audio_thread = 1;
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
	  return OW_INIT_ERROR_NO_O2P_AUDIO_BUF;
	}
    }

  if (context->options & OW_ENGINE_OPTION_P2O_AUDIO)
    {
      audio_thread = 1;
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
	  return OW_INIT_ERROR_NO_P2O_AUDIO_BUF;
	}
    }

  if (engine->context->dll)
    {
      if (!context->get_time)
	{
	  return OW_INIT_ERROR_NO_GET_TIME;
	}
      if (!engine->context->dll)
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

  if (audio_thread)
    {
      debug_print (1, "Starting audio thread...");
      if (pthread_create (&engine->audio_thread, NULL, run_audio, engine))
	{
	  error_print ("Could not start audio thread");
	  return OW_GENERIC_ERROR;
	}
      context->set_rt_priority (engine->audio_thread,
				engine->context->priority);
    }

  return OW_OK;
}

inline void
ow_engine_wait (struct ow_engine *engine)
{
  pthread_join (engine->audio_thread, NULL);
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
  ow_free_device_desc (&engine->device_desc);
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

struct ow_device_desc *
ow_engine_get_device_desc (struct ow_engine *engine)
{
  return &engine->device_desc;
}

inline void
ow_engine_stop (struct ow_engine *engine)
{
  ow_engine_set_status (engine, OW_ENGINE_STATUS_STOP);
}

//This function is for development purpouses only. It is not used but it is in the internal API.
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
	  for (int k = 0; k < engine->device_desc.outputs; k++)
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
}

static void LIBUSB_CALL
cb_xfr_control_out (struct libusb_transfer *xfr)
{
  if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
    {
      error_print ("Error on USB control out transfer: %s",
		   libusb_error_name (xfr->status));
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
				cb_xfr_control_out, engine,
				engine->usb.xfr_timeout);

  int err = libusb_submit_transfer (engine->usb.xfr_control_out);
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
