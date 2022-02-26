/*
 * overwitch.c
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

#include "overwitch.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <endian.h>
#include <time.h>
#include <unistd.h>

#define MAX_OW_LATENCY (8192 * 2)	//This is twice the maximum JACK latency.

#define ELEKTRON_VID 0x1935

#define AFMK1_PID 0x0004
#define AKEYS_PID 0x0006
#define ARMK1_PID 0x0008
#define AHMK1_PID 0x000a
#define DTAKT_PID 0x000c
#define AFMK2_PID 0x000e
#define ARMK2_PID 0x0010
#define DTONE_PID 0x0014
#define AHMK2_PID 0x0016
#define DKEYS_PID 0x001c

#define AUDIO_IN_EP  0x83
#define AUDIO_OUT_EP 0x03
#define MIDI_IN_EP   0x81
#define MIDI_OUT_EP  0x01

#define MIDI_BUF_EVENTS 64
#define MIDI_BUF_SIZE (MIDI_BUF_EVENTS * OB_MIDI_EVENT_SIZE)

#define USB_BULK_MIDI_SIZE 512

#define SAMPLE_TIME_NS (1000000000 / ((int)OB_SAMPLE_RATE))

#define ERROR_ON_GET_DEV_DESC "Error while getting device description: %s\n"

static const struct overwitch_device_desc DIGITAKT_DESC = {
  .pid = DTAKT_PID,
  .name = "Digitakt",
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Main L Input", "Main R Input"},
  .output_track_names =
    {"Main L", "Main R", "Track 1", "Track 2", "Track 3", "Track 4",
     "Track 5", "Track 6", "Track 7", "Track 8", "Input L",
     "Input R"}
};

static const struct overwitch_device_desc DIGITONE_DESC = {
  .pid = DTONE_PID,
  .name = "Digitone",
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Main L Input", "Main R Input"},
  .output_track_names =
    {"Main L", "Main R", "Track 1 L", "Track 1 R", "Track 2 L",
     "Track 2 R", "Track 3 L", "Track 3 R", "Track 4 L", "Track 4 R",
     "Input L", "Input R"}
};

static const struct overwitch_device_desc AFMK2_DESC = {
  .pid = AFMK2_PID,
  .name = "Analog Four MKII",
  .inputs = 6,
  .outputs = 8,
  .input_track_names = {"Main L Input", "Main R Input", "Synth Track 1 Input",
			"Synth Track 2 Input", "Synth Track 3 Input",
			"Synth Track 4 Input"},
  .output_track_names =
    {"Main L", "Main R", "Synth Track 1", "Synth Track 2", "Synth Track 3",
     "Synth Track 4", "Input L", "Input R"}
};

static const struct overwitch_device_desc ARMK2_DESC = {
  .pid = ARMK2_PID,
  .name = "Analog Rytm MKII",
  .inputs = 12,
  .outputs = 12,
  .input_track_names =
    {"Main L Input", "Main R Input", "Main FX L Input", "Main FX R Input",
     "BD Input", "SD Input", "RS/CP Input", "BT Input",
     "LT Input", "MT/HT Input", "CH/OH Input", "CY/CB Input"},
  .output_track_names = {"Main L", "Main R", "BD", "SD", "RS/CP",
			 "BT", "LT", "MT/HT", "CH/OH", "CY/CB", "Input L",
			 "Input R"}
};

static const struct overwitch_device_desc DKEYS_DESC = {
  .pid = DKEYS_PID,
  .name = "Digitone Keys",
  .inputs = 2,
  .outputs = 12,
  .input_track_names = {"Main L Input", "Main R Input"},
  .output_track_names =
    {"Main L", "Main R", "Track 1 L", "Track 1 R", "Track 2 L",
     "Track 2 R", "Track 3 L", "Track 3 R", "Track 4 L", "Track 4 R",
     "Input L", "Input R"}
};

static const struct overwitch_device_desc AHMK1_DESC = {
  .pid = AHMK1_PID,
  .name = "Analog Heat",
  .inputs = 4,
  .outputs = 4,
  .input_track_names =
    {"Main L Input", "Main R Input", "FX Send L", "FX Send R"},
  .output_track_names = {"Main L", "Main R", "FX Return L", "FX Return R"}
};

static const struct overwitch_device_desc AHMK2_DESC = {
  .pid = AHMK2_PID,
  .name = "Analog Heat MKII",
  .inputs = 4,
  .outputs = 4,
  .input_track_names =
    {"Main L Input", "Main R Input", "FX Send L", "FX Send R"},
  .output_track_names = {"Main L", "Main R", "FX Return L", "FX Return R"}
};

static const struct overwitch_device_desc *OB_DEVICE_DESCS[] = {
  &DIGITAKT_DESC, &DIGITONE_DESC, &AFMK2_DESC, &ARMK2_DESC, &DKEYS_DESC,
  &AHMK1_DESC, &AHMK2_DESC
};

static const int OB_DEVICE_DESCS_N =
  sizeof (OB_DEVICE_DESCS) / sizeof (struct overwitch_device_desc *);

static void prepare_cycle_in ();
static void prepare_cycle_out ();
static void prepare_cycle_in_midi ();

int
overwitch_is_valid_device (uint16_t vid, uint16_t pid, char **name)
{
  if (vid != ELEKTRON_VID)
    {
      return 0;
    }
  for (int i = 0; i < OB_DEVICE_DESCS_N; i++)
    {
      if (OB_DEVICE_DESCS[i]->pid == pid)
	{
	  *name = OB_DEVICE_DESCS[i]->name;
	  return 1;
	}
    }
  return 0;
}

static struct overwitch_usb_blk *
get_nth_usb_in_blk (struct overwitch *ow, int n)
{
  char *blk = &ow->usb_data_in[n * ow->usb_data_in_blk_len];
  return (struct overwitch_usb_blk *) blk;
}

static struct overwitch_usb_blk *
get_nth_usb_out_blk (struct overwitch *ow, int n)
{
  char *blk = &ow->usb_data_out[n * ow->usb_data_out_blk_len];
  return (struct overwitch_usb_blk *) blk;
}

static int
prepare_transfers (struct overwitch *ow)
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
free_transfers (struct overwitch *ow)
{
  libusb_free_transfer (ow->xfr_in);
  libusb_free_transfer (ow->xfr_out);
  libusb_free_transfer (ow->xfr_in_midi);
  libusb_free_transfer (ow->xfr_out_midi);
}

static void
set_usb_input_data_blks (struct overwitch *ow)
{
  struct overwitch_usb_blk *blk;
  size_t wso2j;
  int32_t hv;
  float *f;
  int32_t *s;
  overwitch_status_t status;

  pthread_spin_lock (&ow->lock);
  if (ow->inc_sample_counter)
    {
      ow->inc_sample_counter (ow->sample_counter_data,
			      ow->frames_per_transfer);
    }
  status = ow->status;
  pthread_spin_unlock (&ow->lock);

  f = ow->o2j_buf;
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

  if (status < OB_STATUS_RUN)
    {
      return;
    }

  wso2j = jack_ringbuffer_write_space (ow->o2j_rb);
  if (ow->o2j_buf_size <= wso2j)
    {
      jack_ringbuffer_write (ow->o2j_rb, (void *) ow->o2j_buf,
			     ow->o2j_buf_size);
    }
  else
    {
      error_print ("o2j: Audio ring buffer overflow. Discarding data...\n");
    }
}

static void
set_usb_output_data_blks (struct overwitch *ow)
{
  struct overwitch_usb_blk *blk;
  size_t rsj2o;
  int32_t hv;
  size_t bytes;
  long frames;
  float *f;
  int res;
  int32_t *s;
  int enabled = overwitch_is_j2o_audio_enable (ow);

  rsj2o = jack_ringbuffer_read_space (ow->j2o_rb);
  if (!ow->reading_at_j2o_end)
    {
      if (enabled && rsj2o >= ow->j2o_buf_size)
	{
	  debug_print (2, "j2o: Emptying buffer and running...\n");
	  frames = rsj2o / ow->j2o_frame_bytes;
	  bytes = frames * ow->j2o_frame_bytes;
	  jack_ringbuffer_read_advance (ow->j2o_rb, bytes);
	  ow->reading_at_j2o_end = 1;
	}
      goto set_blocks;
    }

  if (!enabled)
    {
      ow->reading_at_j2o_end = 0;
      debug_print (2, "j2o: Clearing buffer and stopping...\n");
      memset (ow->j2o_buf, 0, ow->j2o_buf_size);
      goto set_blocks;
    }

  pthread_spin_lock (&ow->lock);
  ow->j2o_latency = rsj2o;
  if (ow->j2o_latency > ow->j2o_max_latency)
    {
      ow->j2o_max_latency = ow->j2o_latency;
    }
  pthread_spin_unlock (&ow->lock);

  if (rsj2o >= ow->j2o_buf_size)
    {
      jack_ringbuffer_read (ow->j2o_rb, (void *) ow->j2o_buf,
			    ow->j2o_buf_size);
    }
  else
    {
      debug_print (2,
		   "j2o: Audio ring buffer underflow (%zu < %zu). Resampling...\n",
		   rsj2o, ow->j2o_buf_size);
      frames = rsj2o / ow->j2o_frame_bytes;
      bytes = frames * ow->j2o_frame_bytes;
      jack_ringbuffer_read (ow->j2o_rb, (void *) ow->j2o_buf_res, bytes);
      ow->j2o_data.input_frames = frames;
      ow->j2o_data.src_ratio = (double) ow->frames_per_transfer / frames;
      //We should NOT use the simple API but since this only happens very occasionally and mostly at startup, this has very low impact on audio quality.
      res = src_simple (&ow->j2o_data, SRC_SINC_FASTEST,
			ow->device_desc->inputs);
      if (res)
	{
	  debug_print (2, "j2o: Error while resampling: %s\n",
		       src_strerror (res));
	}
      else if (ow->j2o_data.output_frames_gen != ow->frames_per_transfer)
	{
	  error_print
	    ("j2o: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	     ow->j2o_data.src_ratio, ow->j2o_data.output_frames_gen,
	     ow->frames_per_transfer);
	}
    }

set_blocks:
  f = ow->j2o_buf;
  for (int i = 0; i < ow->blocks_per_transfer; i++)
    {
      blk = get_nth_usb_out_blk (ow, i);
      ow->s_counter += OB_FRAMES_PER_BLOCK;
      blk->s_counter = htobe16 (ow->s_counter);
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
  struct overwitch_midi_event event;
  int length;
  struct overwitch *ow = xfr->user_data;

  if (overwitch_get_status (ow) < OB_STATUS_RUN)
    {
      goto end;
    }

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      length = 0;
      event.frames = jack_frame_time (ow->jclient);

      while (length < xfr->actual_length)
	{
	  memcpy (event.bytes, &ow->o2j_midi_data[length],
		  OB_MIDI_EVENT_SIZE);
	  //Note-off, Note-on, Poly-KeyPress, Control Change, Program Change, Channel Pressure, PitchBend Change, Single Byte
	  if (event.bytes[0] >= 0x08 && event.bytes[0] <= 0x0f)
	    {
	      debug_print (2, "o2j MIDI: %02x, %02x, %02x, %02x (%u)\n",
			   event.bytes[0], event.bytes[1], event.bytes[2],
			   event.bytes[3], event.frames);

	      if (jack_ringbuffer_write_space (ow->o2j_rb_midi) >=
		  sizeof (struct overwitch_midi_event))
		{
		  jack_ringbuffer_write (ow->o2j_rb_midi, (void *) &event,
					 sizeof (struct
						 overwitch_midi_event));
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
  struct overwitch *ow = xfr->user_data;

  pthread_spin_lock (&ow->j2o_midi_lock);
  ow->j2o_midi_ready = 1;
  pthread_spin_unlock (&ow->j2o_midi_lock);

  if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
    {
      error_print ("Error on USB MIDI out transfer: %s\n",
		   libusb_strerror (xfr->status));
    }
}

static void
prepare_cycle_out (struct overwitch *ow)
{
  libusb_fill_interrupt_transfer (ow->xfr_out, ow->device_handle,
				  AUDIO_OUT_EP, (void *) ow->usb_data_out,
				  ow->usb_data_out_len, cb_xfr_out, ow, 0);

  int err = libusb_submit_transfer (ow->xfr_out);
  if (err)
    {
      error_print ("j2o: Error when submitting USB audio transfer: %s\n",
		   libusb_strerror (err));
      overwitch_set_status (ow, OB_STATUS_ERROR);
    }
}

static void
prepare_cycle_in (struct overwitch *ow)
{
  libusb_fill_interrupt_transfer (ow->xfr_in, ow->device_handle,
				  AUDIO_IN_EP, (void *) ow->usb_data_in,
				  ow->usb_data_in_len, cb_xfr_in, ow, 0);

  int err = libusb_submit_transfer (ow->xfr_in);
  if (err)
    {
      error_print ("o2j: Error when submitting USB audio in transfer: %s\n",
		   libusb_strerror (err));
      overwitch_set_status (ow, OB_STATUS_ERROR);
    }
}

static void
prepare_cycle_in_midi (struct overwitch *ow)
{
  libusb_fill_bulk_transfer (ow->xfr_in_midi, ow->device_handle,
			     MIDI_IN_EP, (void *) ow->o2j_midi_data,
			     USB_BULK_MIDI_SIZE, cb_xfr_in_midi, ow, 0);

  int err = libusb_submit_transfer (ow->xfr_in_midi);
  if (err)
    {
      error_print ("o2j: Error when submitting USB MIDI transfer: %s\n",
		   libusb_strerror (err));
      overwitch_set_status (ow, OB_STATUS_ERROR);
    }
}

static void
prepare_cycle_out_midi (struct overwitch *ow)
{
  libusb_fill_bulk_transfer (ow->xfr_out_midi, ow->device_handle, MIDI_OUT_EP,
			     (void *) ow->j2o_midi_data,
			     USB_BULK_MIDI_SIZE, cb_xfr_out_midi, ow, 0);

  int err = libusb_submit_transfer (ow->xfr_out_midi);
  if (err)
    {
      error_print ("j2o: Error when submitting USB MIDI transfer: %s\n",
		   libusb_strerror (err));
      overwitch_set_status (ow, OB_STATUS_ERROR);
    }
}

static void
usb_shutdown (struct overwitch *ow)
{
  libusb_close (ow->device_handle);
  libusb_exit (ow->context);
}

// initialization taken from sniffed session

overwitch_err_t
overwitch_init (struct overwitch *ow, uint8_t bus, uint8_t address,
		int blocks_per_transfer)
{
  int i, ret, err;
  libusb_device **list = NULL;
  ssize_t count = 0;
  libusb_device *device;
  char *name = NULL;
  struct libusb_device_descriptor desc;
  struct overwitch_usb_blk *blk;

  // libusb setup
  if (libusb_init (&ow->context) != LIBUSB_SUCCESS)
    {
      return OB_LIBUSB_INIT_FAILED;
    }

  ow->device_handle = NULL;
  count = libusb_get_device_list (ow->context, &list);
  for (i = 0; i < count; i++)
    {
      device = list[i];
      err = libusb_get_device_descriptor (device, &desc);
      if (err)
	{
	  error_print (ERROR_ON_GET_DEV_DESC, libusb_error_name (err));
	  continue;
	}

      if (overwitch_is_valid_device (desc.idVendor, desc.idProduct, &name) &&
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
      ret = OB_CANT_FIND_DEV;
      goto end;
    }

  for (i = 0; i < OB_DEVICE_DESCS_N; i++)
    {
      debug_print (2, "Checking for %s...\n", OB_DEVICE_DESCS[i]->name);
      if (strcmp (OB_DEVICE_DESCS[i]->name, name) == 0)
	{
	  ow->device_desc = OB_DEVICE_DESCS[i];
	  break;
	}
    }

  if (!ow->device_desc)
    {
      ret = OB_CANT_FIND_DEV;
      goto end;
    }

  printf ("Device: %s (outputs: %d, inputs: %d)\n", ow->device_desc->name,
	  ow->device_desc->outputs, ow->device_desc->inputs);

  ret = OB_OK;
  err = libusb_set_configuration (ow->device_handle, 1);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_SET_USB_CONFIG;
      goto end;
    }
  err = libusb_claim_interface (ow->device_handle, 1);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (ow->device_handle, 1, 3);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (ow->device_handle, 2);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (ow->device_handle, 2, 2);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (ow->device_handle, 3);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (ow->device_handle, 3, 0);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_clear_halt (ow->device_handle, AUDIO_IN_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (ow->device_handle, AUDIO_OUT_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (ow->device_handle, MIDI_IN_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (ow->device_handle, MIDI_OUT_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLEAR_EP;
      goto end;
    }
  err = prepare_transfers (ow);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_PREPARE_TRANSFER;
    }

end:
  if (ret == OB_OK)
    {
      pthread_spin_init (&ow->lock, PTHREAD_PROCESS_SHARED);

      ow->blocks_per_transfer = blocks_per_transfer;
      ow->frames_per_transfer = OB_FRAMES_PER_BLOCK * ow->blocks_per_transfer;

      ow->j2o_audio_enabled = 0;

      ow->usb_data_in_blk_len =
	sizeof (struct overwitch_usb_blk) +
	sizeof (int32_t) * OB_FRAMES_PER_BLOCK * ow->device_desc->outputs;
      ow->usb_data_out_blk_len =
	sizeof (struct overwitch_usb_blk) +
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

      ow->j2o_frame_bytes = OB_BYTES_PER_FRAME * ow->device_desc->inputs;
      ow->o2j_frame_bytes = OB_BYTES_PER_FRAME * ow->device_desc->outputs;

      ow->j2o_buf_size = ow->frames_per_transfer * ow->j2o_frame_bytes;
      ow->o2j_buf_size = ow->frames_per_transfer * ow->o2j_frame_bytes;
      ow->j2o_buf = malloc (ow->j2o_buf_size);
      ow->o2j_buf = malloc (ow->o2j_buf_size);
      memset (ow->j2o_buf, 0, ow->j2o_buf_size);
      memset (ow->o2j_buf, 0, ow->o2j_buf_size);

      //o2j resampler
      ow->j2o_buf_res = malloc (ow->j2o_buf_size);
      memset (ow->j2o_buf_res, 0, ow->j2o_buf_size);
      ow->j2o_data.data_in = ow->j2o_buf_res;
      ow->j2o_data.data_out = ow->j2o_buf;
      ow->j2o_data.end_of_input = 1;
      ow->j2o_data.input_frames = ow->frames_per_transfer;
      ow->j2o_data.output_frames = ow->frames_per_transfer;

      //MIDI
      ow->j2o_midi_data = malloc (USB_BULK_MIDI_SIZE);
      ow->o2j_midi_data = malloc (USB_BULK_MIDI_SIZE);
      memset (ow->j2o_midi_data, 0, USB_BULK_MIDI_SIZE);
      memset (ow->o2j_midi_data, 0, USB_BULK_MIDI_SIZE);
      ow->j2o_rb_midi = jack_ringbuffer_create (MIDI_BUF_SIZE * 8);
      ow->o2j_rb_midi = jack_ringbuffer_create (MIDI_BUF_SIZE * 8);
      jack_ringbuffer_mlock (ow->j2o_rb_midi);
      jack_ringbuffer_mlock (ow->o2j_rb_midi);
      pthread_spin_init (&ow->j2o_midi_lock, PTHREAD_PROCESS_SHARED);
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

static const char *ob_err_strgs[] = { "ok", "libusb init failed",
  "can't open device", "can't set usb config",
  "can't claim usb interface", "can't set usb alt setting",
  "can't cleat endpoint", "can't prepare transfer",
  "can't find a matching device"
};

void *
run_j2o_midi (void *data)
{
  int pos, j2o_midi_ready;
  jack_time_t last_time;
  jack_time_t event_time;
  jack_time_t diff;
  struct timespec sleep_time, smallest_sleep_time;
  struct overwitch_midi_event event;
  struct overwitch *ow = data;

  smallest_sleep_time.tv_sec = 0;
  smallest_sleep_time.tv_nsec = SAMPLE_TIME_NS * jack_get_buffer_size (ow->jclient) / 2;	//Average wait time

  pos = 0;
  diff = 0;
  last_time = jack_get_time ();
  ow->j2o_midi_ready = 1;
  while (1)
    {

      while (jack_ringbuffer_read_space (ow->j2o_rb_midi) >=
	     sizeof (struct overwitch_midi_event) && pos < USB_BULK_MIDI_SIZE)
	{
	  if (!pos)
	    {
	      memset (ow->j2o_midi_data, 0, USB_BULK_MIDI_SIZE);
	      diff = 0;
	    }

	  jack_ringbuffer_peek (ow->j2o_rb_midi, (void *) &event,
				sizeof (struct overwitch_midi_event));
	  event_time = jack_frames_to_time (ow->jclient, event.frames);

	  if (event_time > last_time)
	    {
	      diff = event_time - last_time;
	      last_time = event_time;
	      break;
	    }

	  memcpy (&ow->j2o_midi_data[pos], event.bytes, OB_MIDI_EVENT_SIZE);
	  pos += OB_MIDI_EVENT_SIZE;
	  jack_ringbuffer_read_advance (ow->j2o_rb_midi,
					sizeof (struct overwitch_midi_event));
	}

      if (pos)
	{
	  debug_print (2, "Event frames: %u; diff: %lu\n", event.frames,
		       diff);
	  ow->j2o_midi_ready = 0;
	  prepare_cycle_out_midi (ow);
	  pos = 0;
	}

      if (diff)
	{
	  sleep_time.tv_sec = diff / 1000000;
	  sleep_time.tv_nsec = (diff % 1000000) * 1000;
	  nanosleep (&sleep_time, NULL);
	}
      else
	{
	  nanosleep (&smallest_sleep_time, NULL);
	}

      pthread_spin_lock (&ow->j2o_midi_lock);
      j2o_midi_ready = ow->j2o_midi_ready;
      pthread_spin_unlock (&ow->j2o_midi_lock);
      while (!j2o_midi_ready)
	{
	  nanosleep (&smallest_sleep_time, NULL);
	  pthread_spin_lock (&ow->j2o_midi_lock);
	  j2o_midi_ready = ow->j2o_midi_ready;
	  pthread_spin_unlock (&ow->j2o_midi_lock);
	};

      if (overwitch_get_status (ow) <= OB_STATUS_STOP)
	{
	  break;
	}
    }

  return NULL;
}

void *
run_audio_o2j_midi (void *data)
{
  size_t rsj2o, bytes, frames;
  struct overwitch *ow = data;

  while (overwitch_get_status (ow) == OB_STATUS_READY);

  //status == OB_STATUS_BOOT_OVERBRIDGE

  prepare_cycle_in (ow);
  prepare_cycle_out (ow);
  prepare_cycle_in_midi (ow);

  while (1)
    {
      ow->j2o_latency = 0;
      ow->j2o_max_latency = 0;
      ow->reading_at_j2o_end = 0;

      //status == OB_STATUS_BOOT_OVERBRIDGE

      pthread_spin_lock (&ow->lock);
      if (ow->init_sample_counter)
	{
	  ow->init_sample_counter (ow->sample_counter_data, OB_SAMPLE_RATE,
				   ow->frames_per_transfer);
	}
      ow->status = OB_STATUS_BOOT_JACK;
      pthread_spin_unlock (&ow->lock);

      while (overwitch_get_status (ow) >= OB_STATUS_BOOT_JACK)
	{
	  libusb_handle_events_completed (ow->context, NULL);
	}

      if (overwitch_get_status (ow) <= OB_STATUS_STOP)
	{
	  break;
	}

      overwitch_set_status (ow, OB_STATUS_BOOT_OVERBRIDGE);

      rsj2o = jack_ringbuffer_read_space (ow->j2o_rb);
      frames = rsj2o / ow->j2o_frame_bytes;
      bytes = frames * ow->j2o_frame_bytes;
      jack_ringbuffer_read_advance (ow->j2o_rb, bytes);
      memset (ow->j2o_buf, 0, ow->j2o_buf_size);
    }

  return NULL;
}

int
overwitch_activate (struct overwitch *ow, jack_client_t * jclient)
{
  int ret;

  ow->j2o_rb = jack_ringbuffer_create (MAX_OW_LATENCY * ow->j2o_frame_bytes);
  jack_ringbuffer_mlock (ow->j2o_rb);
  ow->o2j_rb = jack_ringbuffer_create (MAX_OW_LATENCY * ow->o2j_frame_bytes);
  jack_ringbuffer_mlock (ow->o2j_rb);

  ow->jclient = jclient;
  ow->s_counter = 0;

  ow->status = OB_STATUS_READY;
  debug_print (1, "Starting j2o MIDI thread...\n");
  ret = pthread_create (&ow->j2o_midi_t, NULL, run_j2o_midi, ow);
  if (ret)
    {
      error_print ("Could not start MIDI thread\n");
      return ret;
    }

  debug_print (1, "Starting audio and o2j MIDI thread...\n");
  ret = pthread_create (&ow->audio_o2j_midi_t, NULL, run_audio_o2j_midi, ow);
  if (ret)
    {
      error_print ("Could not start device thread\n");
    }

  return ret;
}

void
overwitch_wait (struct overwitch *ow)
{
  pthread_join (ow->audio_o2j_midi_t, NULL);
  pthread_join (ow->j2o_midi_t, NULL);
}

const char *
overbrigde_get_err_str (overwitch_err_t errcode)
{
  return ob_err_strgs[errcode];
}

void
overwitch_destroy (struct overwitch *ow)
{
  usb_shutdown (ow);
  free_transfers (ow);
  if (ow->j2o_rb)
    {
      jack_ringbuffer_free (ow->j2o_rb);
    }
  if (ow->o2j_rb)
    {
      jack_ringbuffer_free (ow->o2j_rb);
    }
  jack_ringbuffer_free (ow->o2j_rb_midi);
  free (ow->j2o_buf);
  free (ow->j2o_buf_res);
  free (ow->o2j_buf);
  free (ow->usb_data_in);
  free (ow->usb_data_out);
  free (ow->j2o_midi_data);
  free (ow->o2j_midi_data);
  pthread_spin_destroy (&ow->lock);
  pthread_spin_destroy (&ow->j2o_midi_lock);
}

inline overwitch_status_t
overwitch_get_status (struct overwitch *ow)
{
  overwitch_status_t status;
  pthread_spin_lock (&ow->lock);
  status = ow->status;
  pthread_spin_unlock (&ow->lock);
  return status;
}

inline void
overwitch_set_status (struct overwitch *ow, overwitch_status_t status)
{
  pthread_spin_lock (&ow->lock);
  ow->status = status;
  pthread_spin_unlock (&ow->lock);
}

inline int
overwitch_is_j2o_audio_enable (struct overwitch *ow)
{
  int enabled;
  pthread_spin_lock (&ow->lock);
  enabled = ow->j2o_audio_enabled;
  pthread_spin_unlock (&ow->lock);
  return enabled;
}

inline void
overwitch_set_j2o_audio_enable (struct overwitch *ow, int enabled)
{
  int last = overwitch_is_j2o_audio_enable (ow);
  if (last != enabled)
    {
      pthread_spin_lock (&ow->lock);
      ow->j2o_audio_enabled = enabled;
      pthread_spin_unlock (&ow->lock);
      debug_print (1, "Setting j2o audio to %d...\n", enabled);
    }
}

int
overwitch_list_devices ()
{
  libusb_context *context = NULL;
  libusb_device **list = NULL;
  ssize_t count = 0;
  libusb_device *device;
  struct libusb_device_descriptor desc;
  int i, j, err;
  char *name;
  uint8_t bus, address;

  if (libusb_init (&context) != LIBUSB_SUCCESS)
    {
      return 1;
    }

  count = libusb_get_device_list (context, &list);
  j = 0;
  for (i = 0; i < count; i++)
    {
      device = list[i];
      err = libusb_get_device_descriptor (device, &desc);
      if (err)
	{
	  error_print (ERROR_ON_GET_DEV_DESC, libusb_error_name (err));
	  continue;
	}

      if (overwitch_is_valid_device (desc.idVendor, desc.idProduct, &name))
	{
	  bus = libusb_get_bus_number (device);
	  address = libusb_get_device_address (device);
	  printf ("%d: Bus %03d Device %03d: ID %04x:%04x %s\n", j,
		  bus, address, desc.idVendor, desc.idProduct, name);
	  j++;
	}
    }

  libusb_free_device_list (list, count);
  libusb_exit (context);
  return 0;
}

int
overwitch_get_bus_address (int index, char *name, uint8_t * bus,
			   uint8_t * address)
{
  libusb_context *context = NULL;
  libusb_device **list = NULL;
  int err, i, j;
  ssize_t count = 0;
  libusb_device *device = NULL;
  struct libusb_device_descriptor desc;
  char *dev_name;

  err = libusb_init (&context);
  if (err != LIBUSB_SUCCESS)
    {
      return err;
    }

  j = 0;
  count = libusb_get_device_list (context, &list);
  for (i = 0; i < count; i++)
    {
      device = list[i];
      err = libusb_get_device_descriptor (device, &desc);
      if (err)
	{
	  error_print (ERROR_ON_GET_DEV_DESC, libusb_error_name (err));
	  continue;
	}

      err = 1;
      if (overwitch_is_valid_device
	  (desc.idVendor, desc.idProduct, &dev_name))
	{
	  if (index >= 0)
	    {
	      if (j == index)
		{
		  err = 0;
		  break;
		}
	    }
	  else
	    {
	      if (strcmp (name, dev_name) == 0)
		{
		  err = 0;
		  break;
		}
	    }
	  j++;
	}
    }

  if (err)
    {
      error_print ("No device found\n");
    }
  else
    {
      *bus = libusb_get_bus_number (device);
      *address = libusb_get_device_address (device);
    }

  libusb_free_device_list (list, count);
  libusb_exit (context);
  return err;
}
