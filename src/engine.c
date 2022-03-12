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

#include "engine.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <endian.h>
#include <time.h>
#include <unistd.h>

#define AUDIO_IN_EP  0x83
#define AUDIO_OUT_EP 0x03
#define MIDI_IN_EP   0x81
#define MIDI_OUT_EP  0x01

#define MIDI_BUF_EVENTS 64
#define MIDI_BUF_SIZE (MIDI_BUF_EVENTS * OB_MIDI_EVENT_SIZE)

#define USB_BULK_MIDI_SIZE 512

#define SAMPLE_TIME_NS (1000000000 / ((int)OB_SAMPLE_RATE))

struct ow_engine_usb_blk
{
  uint16_t header;
  uint16_t frames;
  uint8_t padding[OB_PADDING_SIZE];
  int32_t data[];
};

static void prepare_cycle_in ();
static void prepare_cycle_out ();
static void prepare_cycle_in_midi ();

static struct ow_engine_usb_blk *
get_nth_usb_in_blk (struct ow_engine *ow, int n)
{
  char *blk = &ow->usb_data_in[n * ow->usb_data_in_blk_len];
  return (struct ow_engine_usb_blk *) blk;
}

static struct ow_engine_usb_blk *
get_nth_usb_out_blk (struct ow_engine *ow, int n)
{
  char *blk = &ow->usb_data_out[n * ow->usb_data_out_blk_len];
  return (struct ow_engine_usb_blk *) blk;
}

static int
prepare_transfers (struct ow_engine *ow)
{
  ow->xfr_in = libusb_alloc_transfer (0);
  if (!ow->xfr_in)
    {
      return -ENOMEM;
    }

  ow->xfr_out = libusb_alloc_transfer (0);
  if (!ow->xfr_out)
    {
      return -ENOMEM;
    }

  ow->xfr_in_midi = libusb_alloc_transfer (0);
  if (!ow->xfr_in_midi)
    {
      return -ENOMEM;
    }

  ow->xfr_out_midi = libusb_alloc_transfer (0);
  if (!ow->xfr_out_midi)
    {
      return -ENOMEM;
    }

  return LIBUSB_SUCCESS;
}

static void
free_transfers (struct ow_engine *ow)
{
  libusb_free_transfer (ow->xfr_in);
  libusb_free_transfer (ow->xfr_out);
  libusb_free_transfer (ow->xfr_in_midi);
  libusb_free_transfer (ow->xfr_out_midi);
}

static void
set_usb_input_data_blks (struct ow_engine *ow)
{
  struct ow_engine_usb_blk *blk;
  size_t wso2j;
  int32_t hv;
  float *f;
  int32_t *s;
  ow_engine_status_t status;

  pthread_spin_lock (&ow->lock);
  if (ow->dll_ow)
    {
      dll_overwitch_inc (ow->dll_ow, ow->frames_per_transfer,
			 ow->get_time ());
    }
  status = ow->status;
  pthread_spin_unlock (&ow->lock);

  f = ow->o2p_transfer_buf;
  for (int i = 0; i < ow->blocks_per_transfer; i++)
    {
      blk = get_nth_usb_in_blk (ow, i);
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < ow->device_desc->outputs; k++)
	    {
	      hv = be32toh (*s);
	      *f = hv / (float) INT_MAX;
	      f++;
	      s++;
	    }
	}
    }

  if (status < OW_STATUS_RUN)
    {
      return;
    }

  wso2j = ow->buffer_write_space (ow->o2p_audio_buf);
  if (ow->o2p_transfer_size <= wso2j)
    {
      ow->buffer_write (ow->o2p_audio_buf, (void *) ow->o2p_transfer_buf,
			ow->o2p_transfer_size);
    }
  else
    {
      error_print ("o2j: Audio ring buffer overflow. Discarding data...\n");
    }
}

static void
set_usb_output_data_blks (struct ow_engine *ow)
{
  struct ow_engine_usb_blk *blk;
  size_t rsj2o;
  int32_t hv;
  size_t bytes;
  long frames;
  float *f;
  int res;
  int32_t *s;
  int enabled = ow_engine_is_p2o_audio_enable (ow);

  rsj2o = ow->buffer_read_space (ow->p2o_audio_buf);
  if (!ow->reading_at_p2o_end)
    {
      if (enabled && rsj2o >= ow->p2o_transfer_size)
	{
	  debug_print (2, "j2o: Emptying buffer and running...\n");
	  bytes = ow_bytes_to_frame_bytes (rsj2o, ow->p2o_frame_size);
	  ow->buffer_read (ow->p2o_audio_buf, NULL, bytes);
	  ow->reading_at_p2o_end = 1;
	}
      goto set_blocks;
    }

  if (!enabled)
    {
      ow->reading_at_p2o_end = 0;
      debug_print (2, "j2o: Clearing buffer and stopping...\n");
      memset (ow->p2o_transfer_buf, 0, ow->p2o_transfer_size);
      goto set_blocks;
    }

  pthread_spin_lock (&ow->lock);
  ow->p2o_latency = rsj2o;
  if (ow->p2o_latency > ow->p2o_max_latency)
    {
      ow->p2o_max_latency = ow->p2o_latency;
    }
  pthread_spin_unlock (&ow->lock);

  if (rsj2o >= ow->p2o_transfer_size)
    {
      ow->buffer_read (ow->p2o_audio_buf, (void *) ow->p2o_transfer_buf,
		       ow->p2o_transfer_size);
    }
  else
    {
      debug_print (2,
		   "j2o: Audio ring buffer underflow (%zu < %zu). Resampling...\n",
		   rsj2o, ow->p2o_transfer_size);
      frames = rsj2o / ow->p2o_frame_size;
      bytes = frames * ow->p2o_frame_size;
      ow->buffer_read (ow->p2o_audio_buf, (void *) ow->p2o_resampler_buf,
		       bytes);
      ow->p2o_data.input_frames = frames;
      ow->p2o_data.src_ratio = (double) ow->frames_per_transfer / frames;
      //We should NOT use the simple API but since this only happens very occasionally and mostly at startup, this has very low impact on audio quality.
      res = src_simple (&ow->p2o_data, SRC_SINC_FASTEST,
			ow->device_desc->inputs);
      if (res)
	{
	  debug_print (2, "j2o: Error while resampling: %s\n",
		       src_strerror (res));
	}
      else if (ow->p2o_data.output_frames_gen != ow->frames_per_transfer)
	{
	  error_print
	    ("j2o: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	     ow->p2o_data.src_ratio, ow->p2o_data.output_frames_gen,
	     ow->frames_per_transfer);
	}
    }

set_blocks:
  f = ow->p2o_transfer_buf;
  for (int i = 0; i < ow->blocks_per_transfer; i++)
    {
      blk = get_nth_usb_out_blk (ow, i);
      ow->frames += OB_FRAMES_PER_BLOCK;
      blk->frames = htobe16 (ow->frames);
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < ow->device_desc->inputs; k++)
	    {
	      hv = htobe32 ((int32_t) (*f * INT_MAX));
	      *s = hv;
	      f++;
	      s++;
	    }
	}
    }
}

static void LIBUSB_CALL
cb_xfr_in (struct libusb_transfer *xfr)
{
  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      set_usb_input_data_blks (xfr->user_data);
    }
  else
    {
      error_print ("o2j: Error on USB audio transfer: %s\n",
		   libusb_strerror (xfr->status));
    }
  // start new cycle even if this one did not succeed
  prepare_cycle_in (xfr->user_data);
}

static void LIBUSB_CALL
cb_xfr_out (struct libusb_transfer *xfr)
{
  if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
    {
      error_print ("j2o: Error on USB audio transfer: %s\n",
		   libusb_strerror (xfr->status));
    }
  set_usb_output_data_blks (xfr->user_data);
  // We have to make sure that the out cycle is always started after its callback
  // Race condition on slower systems!
  prepare_cycle_out (xfr->user_data);
}

static void LIBUSB_CALL
cb_xfr_in_midi (struct libusb_transfer *xfr)
{
  struct ow_midi_event event;
  int length;
  struct ow_engine *ow = xfr->user_data;

  if (ow_engine_get_status (ow) < OW_STATUS_RUN)
    {
      goto end;
    }

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      length = 0;
      event.time = ow->get_time ();

      while (length < xfr->actual_length)
	{
	  memcpy (event.bytes, &ow->o2p_midi_data[length],
		  OB_MIDI_EVENT_SIZE);
	  //Note-off, Note-on, Poly-KeyPress, Control Change, Program Change, Channel Pressure, PitchBend Change, Single Byte
	  if (event.bytes[0] >= 0x08 && event.bytes[0] <= 0x0f)
	    {
	      debug_print (2, "o2j MIDI: %02x, %02x, %02x, %02x (%f)\n",
			   event.bytes[0], event.bytes[1], event.bytes[2],
			   event.bytes[3], event.time);

	      if (ow->buffer_write_space (ow->o2p_midi_buf) >=
		  sizeof (struct ow_midi_event))
		{
		  ow->buffer_write (ow->o2p_midi_buf, (void *) &event,
				    sizeof (struct ow_midi_event));
		}
	      else
		{
		  error_print
		    ("o2j: MIDI ring buffer overflow. Discarding data...\n");
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
  prepare_cycle_in_midi (ow);
}

static void LIBUSB_CALL
cb_xfr_out_midi (struct libusb_transfer *xfr)
{
  struct ow_engine *ow = xfr->user_data;

  pthread_spin_lock (&ow->p2o_midi_lock);
  ow->p2o_midi_ready = 1;
  pthread_spin_unlock (&ow->p2o_midi_lock);

  if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
    {
      error_print ("Error on USB MIDI out transfer: %s\n",
		   libusb_strerror (xfr->status));
    }
}

static void
prepare_cycle_out (struct ow_engine *ow)
{
  libusb_fill_interrupt_transfer (ow->xfr_out, ow->device_handle,
				  AUDIO_OUT_EP, (void *) ow->usb_data_out,
				  ow->usb_data_out_len, cb_xfr_out, ow, 0);

  int err = libusb_submit_transfer (ow->xfr_out);
  if (err)
    {
      error_print ("j2o: Error when submitting USB audio transfer: %s\n",
		   libusb_strerror (err));
      ow_engine_set_status (ow, OW_STATUS_ERROR);
    }
}

static void
prepare_cycle_in (struct ow_engine *ow)
{
  libusb_fill_interrupt_transfer (ow->xfr_in, ow->device_handle,
				  AUDIO_IN_EP, (void *) ow->usb_data_in,
				  ow->usb_data_in_len, cb_xfr_in, ow, 0);

  int err = libusb_submit_transfer (ow->xfr_in);
  if (err)
    {
      error_print ("o2j: Error when submitting USB audio in transfer: %s\n",
		   libusb_strerror (err));
      ow_engine_set_status (ow, OW_STATUS_ERROR);
    }
}

static void
prepare_cycle_in_midi (struct ow_engine *ow)
{
  libusb_fill_bulk_transfer (ow->xfr_in_midi, ow->device_handle,
			     MIDI_IN_EP, (void *) ow->o2p_midi_data,
			     USB_BULK_MIDI_SIZE, cb_xfr_in_midi, ow, 0);

  int err = libusb_submit_transfer (ow->xfr_in_midi);
  if (err)
    {
      error_print ("o2j: Error when submitting USB MIDI transfer: %s\n",
		   libusb_strerror (err));
      ow_engine_set_status (ow, OW_STATUS_ERROR);
    }
}

static void
prepare_cycle_out_midi (struct ow_engine *ow)
{
  libusb_fill_bulk_transfer (ow->xfr_out_midi, ow->device_handle, MIDI_OUT_EP,
			     (void *) ow->p2o_midi_data,
			     USB_BULK_MIDI_SIZE, cb_xfr_out_midi, ow, 0);

  int err = libusb_submit_transfer (ow->xfr_out_midi);
  if (err)
    {
      error_print ("j2o: Error when submitting USB MIDI transfer: %s\n",
		   libusb_strerror (err));
      ow_engine_set_status (ow, OW_STATUS_ERROR);
    }
}

static void
usb_shutdown (struct ow_engine *ow)
{
  libusb_close (ow->device_handle);
  libusb_exit (ow->context);
}

// initialization taken from sniffed session

ow_err_t
ow_engine_init (struct ow_engine *ow, uint8_t bus, uint8_t address,
		int blocks_per_transfer)
{
  int i, ret, err;
  libusb_device **list = NULL;
  ssize_t count = 0;
  libusb_device *device;
  char *name = NULL;
  struct libusb_device_descriptor desc;
  struct ow_engine_usb_blk *blk;

  // libusb setup
  if (libusb_init (&ow->context) != LIBUSB_SUCCESS)
    {
      return OW_USB_ERROR_LIBUSB_INIT_FAILED;
    }

  ow->device_handle = NULL;
  count = libusb_get_device_list (ow->context, &list);
  for (i = 0; i < count; i++)
    {
      device = list[i];
      err = libusb_get_device_descriptor (device, &desc);
      if (err)
	{
	  error_print ("Error while getting device description: %s",
		       libusb_error_name (err));
	  continue;
	}

      if (ow_is_valid_device (desc.idVendor, desc.idProduct, &name) &&
	  libusb_get_bus_number (device) == bus &&
	  libusb_get_device_address (device) == address)
	{
	  if (libusb_open (device, &ow->device_handle))
	    {
	      error_print ("Error while opening device: %s\n",
			   libusb_error_name (err));
	    }
	  break;
	}
    }

  err = 0;
  libusb_free_device_list (list, count);

  if (!ow->device_handle)
    {
      ret = OW_USB_ERROR_CANT_FIND_DEV;
      goto end;
    }

  for (const struct ow_device_desc ** d = OB_DEVICE_DESCS; *d != NULL; d++)
    {
      debug_print (2, "Checking for %s...\n", (*d)->name);
      if (strcmp ((*d)->name, name) == 0)
	{
	  ow->device_desc = *d;
	  break;
	}
    }

  if (!ow->device_desc)
    {
      ret = OW_USB_ERROR_CANT_FIND_DEV;
      goto end;
    }

  printf ("Device: %s (outputs: %d, inputs: %d)\n", ow->device_desc->name,
	  ow->device_desc->outputs, ow->device_desc->inputs);

  ret = OW_OK;
  err = libusb_set_configuration (ow->device_handle, 1);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_USB_CONFIG;
      goto end;
    }
  err = libusb_claim_interface (ow->device_handle, 1);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (ow->device_handle, 1, 3);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (ow->device_handle, 2);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (ow->device_handle, 2, 2);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (ow->device_handle, 3);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (ow->device_handle, 3, 0);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_clear_halt (ow->device_handle, AUDIO_IN_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (ow->device_handle, AUDIO_OUT_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (ow->device_handle, MIDI_IN_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (ow->device_handle, MIDI_OUT_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_CLEAR_EP;
      goto end;
    }
  err = prepare_transfers (ow);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OW_USB_ERROR_CANT_PREPARE_TRANSFER;
    }

end:
  if (ret == OW_OK)
    {
      pthread_spin_init (&ow->lock, PTHREAD_PROCESS_SHARED);

      ow->blocks_per_transfer = blocks_per_transfer;
      ow->frames_per_transfer = OB_FRAMES_PER_BLOCK * ow->blocks_per_transfer;

      ow->p2o_audio_enabled = 0;

      ow->usb_data_in_blk_len =
	sizeof (struct ow_engine_usb_blk) +
	sizeof (int32_t) * OB_FRAMES_PER_BLOCK * ow->device_desc->outputs;
      ow->usb_data_out_blk_len =
	sizeof (struct ow_engine_usb_blk) +
	sizeof (int32_t) * OB_FRAMES_PER_BLOCK * ow->device_desc->inputs;

      ow->usb_data_in_len = ow->usb_data_in_blk_len * ow->blocks_per_transfer;
      ow->usb_data_out_len =
	ow->usb_data_out_blk_len * ow->blocks_per_transfer;
      ow->usb_data_in = malloc (ow->usb_data_in_len);
      ow->usb_data_out = malloc (ow->usb_data_out_len);
      memset (ow->usb_data_in, 0, ow->usb_data_in_len);
      memset (ow->usb_data_out, 0, ow->usb_data_out_len);

      for (int i = 0; i < ow->blocks_per_transfer; i++)
	{
	  blk = get_nth_usb_out_blk (ow, i);
	  blk->header = htobe16 (0x07ff);
	}

      ow->p2o_frame_size = OB_BYTES_PER_SAMPLE * ow->device_desc->inputs;
      ow->o2p_frame_size = OB_BYTES_PER_SAMPLE * ow->device_desc->outputs;

      ow->p2o_transfer_size = ow->frames_per_transfer * ow->p2o_frame_size;
      ow->o2p_transfer_size = ow->frames_per_transfer * ow->o2p_frame_size;
      ow->p2o_transfer_buf = malloc (ow->p2o_transfer_size);
      ow->o2p_transfer_buf = malloc (ow->o2p_transfer_size);
      memset (ow->p2o_transfer_buf, 0, ow->p2o_transfer_size);
      memset (ow->o2p_transfer_buf, 0, ow->o2p_transfer_size);

      //o2j resampler
      ow->p2o_resampler_buf = malloc (ow->p2o_transfer_size);
      memset (ow->p2o_resampler_buf, 0, ow->p2o_transfer_size);
      ow->p2o_data.data_in = ow->p2o_resampler_buf;
      ow->p2o_data.data_out = ow->p2o_transfer_buf;
      ow->p2o_data.end_of_input = 1;
      ow->p2o_data.input_frames = ow->frames_per_transfer;
      ow->p2o_data.output_frames = ow->frames_per_transfer;

      //MIDI
      ow->p2o_midi_data = malloc (USB_BULK_MIDI_SIZE);
      ow->o2p_midi_data = malloc (USB_BULK_MIDI_SIZE);
      memset (ow->p2o_midi_data, 0, USB_BULK_MIDI_SIZE);
      memset (ow->o2p_midi_data, 0, USB_BULK_MIDI_SIZE);
      pthread_spin_init (&ow->p2o_midi_lock, PTHREAD_PROCESS_SHARED);
    }
  else
    {
      usb_shutdown (ow);
      if (err)
	{
	  error_print ("Error while initializing device: %s\n",
		       libusb_error_name (err));
	}
    }
  return ret;
}

static const char *ob_err_strgs[] = {
  "ok",
  "'buffer_read_space' not set",
  "'buffer_write_space' not set",
  "'buffer_read' not set",
  "'buffer_write' not set",
  "'get_time' not set",
  "'secondary_dll' not set",
  "libusb init failed",
  "can't open device",
  "can't set usb config",
  "can't claim usb interface",
  "can't set usb alt setting",
  "can't cleat endpoint",
  "can't prepare transfer",
  "can't find a matching device"
};

static void *
run_p2o_midi (void *data)
{
  int pos, p2o_midi_ready, event_read = 0;
  double last_time, diff;
  struct timespec sleep_time, smallest_sleep_time;
  struct ow_midi_event event;
  struct ow_engine *ow = data;

  smallest_sleep_time.tv_sec = 0;
  smallest_sleep_time.tv_nsec = SAMPLE_TIME_NS * 32 / 2;	//Average wait time for a 32 buffer sample

  pos = 0;
  diff = 0.0;
  last_time = ow->get_time ();
  ow->p2o_midi_ready = 1;
  while (1)
    {

      while (ow->buffer_read_space (ow->p2o_midi_buf) >=
	     sizeof (struct ow_midi_event) && pos < USB_BULK_MIDI_SIZE)
	{
	  if (!pos)
	    {
	      memset (ow->p2o_midi_data, 0, USB_BULK_MIDI_SIZE);
	      diff = 0;
	    }

	  if (!event_read)
	    {
	      ow->buffer_read (ow->p2o_midi_buf, (void *) &event,
			       sizeof (struct ow_midi_event));
	      event_read = 1;
	    }

	  if (event.time > last_time)
	    {
	      diff = event.time - last_time;
	      last_time = event.time;
	      break;
	    }

	  memcpy (&ow->p2o_midi_data[pos], event.bytes, OB_MIDI_EVENT_SIZE);
	  pos += OB_MIDI_EVENT_SIZE;
	  event_read = 0;
	}

      if (pos)
	{
	  debug_print (2, "Event frames: %f; diff: %f\n", event.time, diff);
	  ow->p2o_midi_ready = 0;
	  prepare_cycle_out_midi (ow);
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

      pthread_spin_lock (&ow->p2o_midi_lock);
      p2o_midi_ready = ow->p2o_midi_ready;
      pthread_spin_unlock (&ow->p2o_midi_lock);
      while (!p2o_midi_ready)
	{
	  nanosleep (&smallest_sleep_time, NULL);
	  pthread_spin_lock (&ow->p2o_midi_lock);
	  p2o_midi_ready = ow->p2o_midi_ready;
	  pthread_spin_unlock (&ow->p2o_midi_lock);
	};

      if (ow_engine_get_status (ow) <= OW_STATUS_STOP)
	{
	  break;
	}
    }

  return NULL;
}

static void *
run_audio_o2p_midi (void *data)
{
  size_t rsj2o, bytes;
  struct ow_engine *ow = data;

  while (ow_engine_get_status (ow) == OW_STATUS_READY);

  //status == OW_STATUS_BOOT

  prepare_cycle_in (ow);
  prepare_cycle_out (ow);
  if (ow->midi)
    {
      prepare_cycle_in_midi (ow);
    }

  while (1)
    {
      ow->p2o_latency = 0;
      ow->p2o_max_latency = 0;
      ow->reading_at_p2o_end = 0;

      //status == OW_STATUS_BOOT

      pthread_spin_lock (&ow->lock);

      if (ow->dll_ow)
	{
	  dll_overwitch_init (ow->dll_ow, OB_SAMPLE_RATE,
			      ow->frames_per_transfer, ow->get_time ());
	}

      ow->status = OW_STATUS_WAIT;
      pthread_spin_unlock (&ow->lock);

      while (ow_engine_get_status (ow) >= OW_STATUS_WAIT)
	{
	  libusb_handle_events_completed (ow->context, NULL);
	}

      if (ow_engine_get_status (ow) <= OW_STATUS_STOP)
	{
	  break;
	}

      ow_engine_set_status (ow, OW_STATUS_BOOT);

      rsj2o = ow->buffer_read_space (ow->p2o_audio_buf);
      bytes = ow_bytes_to_frame_bytes (rsj2o, ow->p2o_frame_size);
      ow->buffer_read (ow->p2o_audio_buf, NULL, bytes);
      memset (ow->p2o_transfer_buf, 0, ow->p2o_transfer_size);
    }

  return NULL;
}

inline int
ow_engine_activate (struct ow_engine *ow, uint64_t features)
{
  int ret;

  if (!ow->buffer_read_space)
    {
      return OW_INIT_ERROR_NO_READ_SPACE;
    }
  if (!ow->buffer_write_space)
    {
      return OW_INIT_ERROR_NO_WRITE_SPACE;
    }
  if (!ow->buffer_read)
    {
      return OW_INIT_ERROR_NO_READ;
    }
  if (!ow->buffer_write)
    {
      return OW_INIT_ERROR_NO_WRITE;
    }

  if (features & OW_OPTION_MIDI)
    {
      if (!ow->get_time)
	{
	  return OW_INIT_ERROR_NO_GET_TIME;
	}
      ow->midi = 1;
    }
  else
    {
      ow->midi = 0;
    }

  if (features & OW_OPTION_SECONDARY_DLL)
    {
      if (!ow->dll_ow)
	{
	  return OW_INIT_ERROR_NO_SECONDARY_DLL;
	}
      if (!ow->get_time)
	{
	  return OW_INIT_ERROR_NO_GET_TIME;
	}
    }

  ow->frames = 0;

  ow->status = OW_STATUS_READY;
  if (ow->midi)
    {
      debug_print (1, "Starting j2o MIDI thread...\n");
      ret = pthread_create (&ow->p2o_midi_thread, NULL, run_p2o_midi, ow);
      if (ret)
	{
	  error_print ("Could not start MIDI thread\n");
	  return ret;
	}
    }

  debug_print (1, "Starting audio and o2j MIDI thread...\n");
  ret =
    pthread_create (&ow->audio_o2p_midi_thread, NULL, run_audio_o2p_midi, ow);
  if (ret)
    {
      error_print ("Could not start device thread\n");
    }

  return ret;
}

inline void
ow_engine_wait (struct ow_engine *ow)
{
  pthread_join (ow->audio_o2p_midi_thread, NULL);
  if (ow->midi)
    {
      pthread_join (ow->p2o_midi_thread, NULL);
    }
}

const char *
ow_get_err_str (ow_err_t errcode)
{
  return ob_err_strgs[errcode];
}

void
ow_engine_destroy (struct ow_engine *ow)
{
  usb_shutdown (ow);
  free_transfers (ow);
  free (ow->p2o_transfer_buf);
  free (ow->p2o_resampler_buf);
  free (ow->o2p_transfer_buf);
  free (ow->usb_data_in);
  free (ow->usb_data_out);
  free (ow->p2o_midi_data);
  free (ow->o2p_midi_data);
  pthread_spin_destroy (&ow->lock);
  pthread_spin_destroy (&ow->p2o_midi_lock);
}

inline ow_engine_status_t
ow_engine_get_status (struct ow_engine *ow)
{
  ow_engine_status_t status;
  pthread_spin_lock (&ow->lock);
  status = ow->status;
  pthread_spin_unlock (&ow->lock);
  return status;
}

inline void
ow_engine_set_status (struct ow_engine *ow, ow_engine_status_t status)
{
  pthread_spin_lock (&ow->lock);
  ow->status = status;
  pthread_spin_unlock (&ow->lock);
}

inline int
ow_engine_is_p2o_audio_enable (struct ow_engine *ow)
{
  int enabled;
  pthread_spin_lock (&ow->lock);
  enabled = ow->p2o_audio_enabled;
  pthread_spin_unlock (&ow->lock);
  return enabled;
}

inline void
ow_engine_set_p2o_audio_enable (struct ow_engine *ow, int enabled)
{
  int last = ow_engine_is_p2o_audio_enable (ow);
  if (last != enabled)
    {
      pthread_spin_lock (&ow->lock);
      ow->p2o_audio_enabled = enabled;
      pthread_spin_unlock (&ow->lock);
      debug_print (1, "Setting j2o audio to %d...\n", enabled);
    }
}
