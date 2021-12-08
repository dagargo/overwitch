/*
 * overbridge.c
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

#include "overbridge.h"

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

static const struct overbridge_device_desc DIGITAKT_DESC = {
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

static const struct overbridge_device_desc DIGITONE_DESC = {
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

static const struct overbridge_device_desc AFMK2_DESC = {
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

static const struct overbridge_device_desc ARMK2_DESC = {
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

static const struct overbridge_device_desc DKEYS_DESC = {
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

static const struct overbridge_device_desc AHMK1_DESC = {
  .pid = AHMK1_PID,
  .name = "Analog Heat",
  .inputs = 4,
  .outputs = 4,
  .input_track_names =
    {"Main L Input", "Main R Input", "FX Send L", "FX Send R"},
  .output_track_names = {"Main L", "Main R", "FX Return L", "FX Return R"}
};

static const struct overbridge_device_desc AHMK2_DESC = {
  .pid = AHMK2_PID,
  .name = "Analog Heat MKII",
  .inputs = 4,
  .outputs = 4,
  .input_track_names =
    {"Main L Input", "Main R Input", "FX Send L", "FX Send R"},
  .output_track_names = {"Main L", "Main R", "FX Return L", "FX Return R"}
};

static const struct overbridge_device_desc *OB_DEVICE_DESCS[] = {
  &DIGITAKT_DESC, &DIGITONE_DESC, &AFMK2_DESC, &ARMK2_DESC, &DKEYS_DESC,
  &AHMK1_DESC, &AHMK2_DESC
};

static const int OB_DEVICE_DESCS_N =
  sizeof (OB_DEVICE_DESCS) / sizeof (struct overbridge_device_desc *);

static void prepare_cycle_in ();
static void prepare_cycle_out ();
static void prepare_cycle_in_midi ();

static int
overbridge_is_valid_device (uint16_t vid, uint16_t pid, char **name)
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

static struct overbridge_usb_blk *
get_nth_usb_in_blk (struct overbridge *ob, int n)
{
  char *blk = &ob->usb_data_in[n * ob->usb_data_in_blk_len];
  return (struct overbridge_usb_blk *) blk;
}

static struct overbridge_usb_blk *
get_nth_usb_out_blk (struct overbridge *ob, int n)
{
  char *blk = &ob->usb_data_out[n * ob->usb_data_out_blk_len];
  return (struct overbridge_usb_blk *) blk;
}

static int
prepare_transfers (struct overbridge *ob)
{
  ob->xfr_in = libusb_alloc_transfer (0);
  if (!ob->xfr_in)
    {
      return -ENOMEM;
    }

  ob->xfr_out = libusb_alloc_transfer (0);
  if (!ob->xfr_out)
    {
      return -ENOMEM;
    }

  ob->xfr_in_midi = libusb_alloc_transfer (0);
  if (!ob->xfr_in_midi)
    {
      return -ENOMEM;
    }

  ob->xfr_out_midi = libusb_alloc_transfer (0);
  if (!ob->xfr_out_midi)
    {
      return -ENOMEM;
    }

  return LIBUSB_SUCCESS;
}

static void
free_transfers (struct overbridge *ob)
{
  libusb_free_transfer (ob->xfr_in);
  libusb_free_transfer (ob->xfr_out);
  libusb_free_transfer (ob->xfr_in_midi);
  libusb_free_transfer (ob->xfr_out_midi);
}

static void
set_usb_input_data_blks (struct overbridge *ob)
{
  struct overbridge_usb_blk *blk;
  size_t wso2j;
  int32_t hv;
  jack_default_audio_sample_t *f;
  int32_t *s;
  overbridge_status_t status;

  pthread_spin_lock (&ob->lock);
  dll_counter_inc (&ob->o2j_dll_counter, ob->frames_per_transfer);
  status = ob->status;
  pthread_spin_unlock (&ob->lock);

  f = ob->o2j_buf;
  for (int i = 0; i < ob->blocks_per_transfer; i++)
    {
      blk = get_nth_usb_in_blk (ob, i);
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < ob->device_desc->outputs; k++)
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

  wso2j = jack_ringbuffer_write_space (ob->o2j_rb);
  if (ob->o2j_buf_size <= wso2j)
    {
      jack_ringbuffer_write (ob->o2j_rb, (void *) ob->o2j_buf,
			     ob->o2j_buf_size);
    }
  else
    {
      error_print ("o2j: Audio ring buffer overflow. Discarding data...\n");
    }
}

static void
set_usb_output_data_blks (struct overbridge *ob)
{
  struct overbridge_usb_blk *blk;
  size_t rsj2o;
  int32_t hv;
  size_t bytes;
  long frames;
  jack_default_audio_sample_t *f;
  int res;
  int32_t *s;
  int enabled = overbridge_is_j2o_audio_enable (ob);

  rsj2o = jack_ringbuffer_read_space (ob->j2o_rb);
  if (!ob->reading_at_j2o_end)
    {
      if (enabled && rsj2o >= ob->j2o_buf_size)
	{
	  debug_print (2, "j2o: Emptying buffer and running...\n");
	  frames = rsj2o / ob->j2o_frame_bytes;
	  bytes = frames * ob->j2o_frame_bytes;
	  jack_ringbuffer_read_advance (ob->j2o_rb, bytes);
	  ob->reading_at_j2o_end = 1;
	}
      goto set_blocks;
    }

  if (!enabled)
    {
      ob->reading_at_j2o_end = 0;
      debug_print (2, "j2o: Clearing buffer and stopping...\n");
      memset (ob->j2o_buf, 0, ob->j2o_buf_size);
      goto set_blocks;
    }

  pthread_spin_lock (&ob->lock);
  ob->j2o_latency = rsj2o;
  if (ob->j2o_latency > ob->j2o_max_latency)
    {
      ob->j2o_max_latency = ob->j2o_latency;
    }
  pthread_spin_unlock (&ob->lock);

  if (rsj2o >= ob->j2o_buf_size)
    {
      jack_ringbuffer_read (ob->j2o_rb, (void *) ob->j2o_buf,
			    ob->j2o_buf_size);
    }
  else
    {
      debug_print (2,
		   "j2o: Audio ring buffer underflow (%zu < %zu). Resampling...\n",
		   rsj2o, ob->j2o_buf_size);
      frames = rsj2o / ob->j2o_frame_bytes;
      bytes = frames * ob->j2o_frame_bytes;
      jack_ringbuffer_read (ob->j2o_rb, (void *) ob->j2o_buf_res, bytes);
      ob->j2o_data.input_frames = frames;
      ob->j2o_data.src_ratio = (double) ob->frames_per_transfer / frames;
      //We should NOT use the simple API but since this only happens very occasionally and mostly at startup, this has very low impact on audio quality.
      res = src_simple (&ob->j2o_data, SRC_SINC_FASTEST,
			ob->device_desc->inputs);
      if (res)
	{
	  debug_print (2, "j2o: Error while resampling: %s\n",
		       src_strerror (res));
	}
      else if (ob->j2o_data.output_frames_gen != ob->frames_per_transfer)
	{
	  error_print
	    ("j2o: Unexpected frames with ratio %f (output %ld, expected %d)\n",
	     ob->j2o_data.src_ratio, ob->j2o_data.output_frames_gen,
	     ob->frames_per_transfer);
	}
    }

set_blocks:
  f = ob->j2o_buf;
  for (int i = 0; i < ob->blocks_per_transfer; i++)
    {
      blk = get_nth_usb_out_blk (ob, i);
      ob->s_counter += OB_FRAMES_PER_BLOCK;
      blk->s_counter = htobe16 (ob->s_counter);
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < ob->device_desc->inputs; k++)
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
  struct ob_midi_event event;
  int length;
  struct overbridge *ob = xfr->user_data;

  if (overbridge_get_status (ob) < OB_STATUS_RUN)
    {
      goto end;
    }

  if (xfr->status == LIBUSB_TRANSFER_COMPLETED)
    {
      length = 0;
      event.frames = jack_frame_time (ob->jclient);

      while (length < xfr->actual_length)
	{
	  memcpy (event.bytes, &ob->o2j_midi_data[length],
		  OB_MIDI_EVENT_SIZE);
	  //Note-off, Note-on, Poly-KeyPress, Control Change, Program Change, Channel Pressure, PitchBend Change, Single Byte
	  if (event.bytes[0] >= 0x08 && event.bytes[0] <= 0x0f)
	    {
	      debug_print (2, "o2j MIDI: %02x, %02x, %02x, %02x (%u)\n",
			   event.bytes[0], event.bytes[1], event.bytes[2],
			   event.bytes[3], event.frames);

	      if (jack_ringbuffer_write_space (ob->o2j_rb_midi) >=
		  sizeof (struct ob_midi_event))
		{
		  jack_ringbuffer_write (ob->o2j_rb_midi, (void *) &event,
					 sizeof (struct ob_midi_event));
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
  prepare_cycle_in_midi (ob);
}

static void LIBUSB_CALL
cb_xfr_out_midi (struct libusb_transfer *xfr)
{
  if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
    {
      error_print ("Error on USB MIDI out transfer: %s\n",
		   libusb_strerror (xfr->status));
    }
}

static void
prepare_cycle_out (struct overbridge *ob)
{
  libusb_fill_interrupt_transfer (ob->xfr_out, ob->device_handle,
				  AUDIO_OUT_EP, (void *) ob->usb_data_out,
				  ob->usb_data_out_len, cb_xfr_out, ob, 0);

  int err = libusb_submit_transfer (ob->xfr_out);
  if (err)
    {
      error_print ("j2o: Error when submitting USB audio transfer: %s\n",
		   libusb_strerror (err));
      overbridge_set_status (ob, OB_STATUS_ERROR);
    }
}

static void
prepare_cycle_in (struct overbridge *ob)
{
  libusb_fill_interrupt_transfer (ob->xfr_in, ob->device_handle,
				  AUDIO_IN_EP, (void *) ob->usb_data_in,
				  ob->usb_data_in_len, cb_xfr_in, ob, 0);

  int err = libusb_submit_transfer (ob->xfr_in);
  if (err)
    {
      error_print ("o2j: Error when submitting USB audio in transfer: %s\n",
		   libusb_strerror (err));
      overbridge_set_status (ob, OB_STATUS_ERROR);
    }
}

static void
prepare_cycle_in_midi (struct overbridge *ob)
{
  libusb_fill_bulk_transfer (ob->xfr_in_midi, ob->device_handle,
			     MIDI_IN_EP, (void *) ob->o2j_midi_data,
			     USB_BULK_MIDI_SIZE, cb_xfr_in_midi, ob, 0);

  int err = libusb_submit_transfer (ob->xfr_in_midi);
  if (err)
    {
      error_print ("o2j: Error when submitting USB MIDI transfer: %s\n",
		   libusb_strerror (err));
      overbridge_set_status (ob, OB_STATUS_ERROR);
    }
}

static void
prepare_cycle_out_midi (struct overbridge *ob)
{
  libusb_fill_bulk_transfer (ob->xfr_out_midi, ob->device_handle, MIDI_OUT_EP,
			     (void *) ob->j2o_midi_data,
			     USB_BULK_MIDI_SIZE, cb_xfr_out_midi, ob, 0);

  int err = libusb_submit_transfer (ob->xfr_out_midi);
  if (err)
    {
      error_print ("j2o: Error when submitting USB MIDI transfer: %s\n",
		   libusb_strerror (err));
      overbridge_set_status (ob, OB_STATUS_ERROR);
    }
}

static void
usb_shutdown (struct overbridge *ob)
{
  libusb_close (ob->device_handle);
  libusb_exit (ob->context);
}

// initialization taken from sniffed session

overbridge_err_t
overbridge_init (struct overbridge *ob, uint8_t bus, uint8_t address,
		 int blocks_per_transfer)
{
  int i, ret, err;
  libusb_device **list = NULL;
  ssize_t count = 0;
  libusb_device *device;
  char *name = NULL;
  struct libusb_device_descriptor desc;
  struct overbridge_usb_blk *blk;

  // libusb setup
  if (libusb_init (&ob->context) != LIBUSB_SUCCESS)
    {
      return OB_LIBUSB_INIT_FAILED;
    }

  ob->device_handle = NULL;
  count = libusb_get_device_list (ob->context, &list);
  for (i = 0; i < count; i++)
    {
      device = list[i];
      err = libusb_get_device_descriptor (device, &desc);
      if (err)
	{
	  error_print (ERROR_ON_GET_DEV_DESC, libusb_error_name (err));
	  continue;
	}

      if (overbridge_is_valid_device (desc.idVendor, desc.idProduct, &name))
	{
	  if (libusb_get_bus_number (device) == bus &&
	      libusb_get_device_address (device) == address)
	    {
	      if (libusb_open (device, &ob->device_handle))
		{
		  error_print ("Error while opening device: %s\n",
			       libusb_error_name (err));
		}
	    }
	  break;
	}
    }

  err = 0;
  libusb_free_device_list (list, count);

  if (!ob->device_handle)
    {
      ret = OB_CANT_FIND_DEV;
      goto end;
    }

  for (i = 0; i < OB_DEVICE_DESCS_N; i++)
    {
      debug_print (2, "Checking for %s...\n", OB_DEVICE_DESCS[i]->name);
      if (strcmp (OB_DEVICE_DESCS[i]->name, name) == 0)
	{
	  ob->device_desc = OB_DEVICE_DESCS[i];
	  break;
	}
    }

  if (!ob->device_desc)
    {
      ret = OB_CANT_FIND_DEV;
      goto end;
    }

  printf ("Device: %s (outputs: %d, inputs: %d)\n", ob->device_desc->name,
	  ob->device_desc->outputs, ob->device_desc->inputs);

  ret = OB_OK;
  err = libusb_set_configuration (ob->device_handle, 1);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_SET_USB_CONFIG;
      goto end;
    }
  err = libusb_claim_interface (ob->device_handle, 1);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (ob->device_handle, 1, 3);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (ob->device_handle, 2);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (ob->device_handle, 2, 2);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_claim_interface (ob->device_handle, 3);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLAIM_IF;
      goto end;
    }
  err = libusb_set_interface_alt_setting (ob->device_handle, 3, 0);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_SET_ALT_SETTING;
      goto end;
    }
  err = libusb_clear_halt (ob->device_handle, AUDIO_IN_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (ob->device_handle, AUDIO_OUT_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (ob->device_handle, MIDI_IN_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLEAR_EP;
      goto end;
    }
  err = libusb_clear_halt (ob->device_handle, MIDI_OUT_EP);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_CLEAR_EP;
      goto end;
    }
  err = prepare_transfers (ob);
  if (LIBUSB_SUCCESS != err)
    {
      ret = OB_CANT_PREPARE_TRANSFER;
    }

end:
  if (ret == OB_OK)
    {
      pthread_spin_init (&ob->lock, PTHREAD_PROCESS_SHARED);

      ob->blocks_per_transfer = blocks_per_transfer;
      ob->frames_per_transfer = OB_FRAMES_PER_BLOCK * ob->blocks_per_transfer;

      ob->j2o_audio_enabled = 0;

      ob->usb_data_in_blk_len =
	sizeof (struct overbridge_usb_blk) +
	sizeof (int32_t) * OB_FRAMES_PER_BLOCK * ob->device_desc->outputs;
      ob->usb_data_out_blk_len =
	sizeof (struct overbridge_usb_blk) +
	sizeof (int32_t) * OB_FRAMES_PER_BLOCK * ob->device_desc->inputs;

      ob->usb_data_in_len = ob->usb_data_in_blk_len * ob->blocks_per_transfer;
      ob->usb_data_out_len =
	ob->usb_data_out_blk_len * ob->blocks_per_transfer;
      ob->usb_data_in = malloc (ob->usb_data_in_len);
      ob->usb_data_out = malloc (ob->usb_data_out_len);
      memset (ob->usb_data_in, 0, ob->usb_data_in_len);
      memset (ob->usb_data_out, 0, ob->usb_data_out_len);

      for (int i = 0; i < ob->blocks_per_transfer; i++)
	{
	  blk = get_nth_usb_out_blk (ob, i);
	  blk->header = htobe16 (0x07ff);
	}

      ob->j2o_frame_bytes = OB_BYTES_PER_FRAME * ob->device_desc->inputs;
      ob->o2j_frame_bytes = OB_BYTES_PER_FRAME * ob->device_desc->outputs;

      ob->j2o_buf_size = ob->frames_per_transfer * ob->j2o_frame_bytes;
      ob->o2j_buf_size = ob->frames_per_transfer * ob->o2j_frame_bytes;
      ob->j2o_buf = malloc (ob->j2o_buf_size);
      ob->o2j_buf = malloc (ob->o2j_buf_size);
      memset (ob->j2o_buf, 0, ob->j2o_buf_size);
      memset (ob->o2j_buf, 0, ob->o2j_buf_size);

      //o2j resampler
      ob->j2o_buf_res = malloc (ob->j2o_buf_size);
      memset (ob->j2o_buf_res, 0, ob->j2o_buf_size);
      ob->j2o_data.data_in = ob->j2o_buf_res;
      ob->j2o_data.data_out = ob->j2o_buf;
      ob->j2o_data.end_of_input = 1;
      ob->j2o_data.input_frames = ob->frames_per_transfer;
      ob->j2o_data.output_frames = ob->frames_per_transfer;

      //MIDI
      ob->j2o_midi_data = malloc (USB_BULK_MIDI_SIZE);
      ob->o2j_midi_data = malloc (USB_BULK_MIDI_SIZE);
      memset (ob->j2o_midi_data, 0, USB_BULK_MIDI_SIZE);
      memset (ob->o2j_midi_data, 0, USB_BULK_MIDI_SIZE);
      ob->j2o_rb_midi = jack_ringbuffer_create (MIDI_BUF_SIZE * 8);
      ob->o2j_rb_midi = jack_ringbuffer_create (MIDI_BUF_SIZE * 8);
      jack_ringbuffer_mlock (ob->j2o_rb_midi);
      jack_ringbuffer_mlock (ob->o2j_rb_midi);
    }
  else
    {
      usb_shutdown (ob);
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
  int pos;
  jack_time_t last_time;
  jack_time_t event_time;
  jack_time_t diff;
  struct timespec req;
  struct ob_midi_event event;
  struct overbridge *ob = data;
  int sleep_time_ns = SAMPLE_TIME_NS * jack_get_buffer_size (ob->jclient) / 2;	//Average wait time

  set_rt_priority (ob->priority);

  pos = 0;
  diff = 0;
  last_time = jack_get_time ();
  while (1)
    {
      while (jack_ringbuffer_read_space (ob->j2o_rb_midi) >=
	     sizeof (struct ob_midi_event) && pos < USB_BULK_MIDI_SIZE)
	{
	  if (!pos)
	    {
	      memset (ob->j2o_midi_data, 0, USB_BULK_MIDI_SIZE);
	      diff = 0;
	    }

	  jack_ringbuffer_peek (ob->j2o_rb_midi, (void *) &event,
				sizeof (struct ob_midi_event));
	  event_time = jack_frames_to_time (ob->jclient, event.frames);

	  if (event_time > last_time)
	    {
	      diff = event_time - last_time;
	      last_time = event_time;
	      break;
	    }

	  memcpy (&ob->j2o_midi_data[pos], event.bytes, OB_MIDI_EVENT_SIZE);
	  pos += OB_MIDI_EVENT_SIZE;
	  jack_ringbuffer_read_advance (ob->j2o_rb_midi,
					sizeof (struct ob_midi_event));
	}

      if (pos)
	{
	  debug_print (2, "Event frames: %u; diff: %lu\n", event.frames,
		       diff);
	  prepare_cycle_out_midi (ob);
	  pos = 0;
	}

      if (diff)
	{
	  req.tv_sec = diff / 1000000;
	  req.tv_nsec = (diff % 1000000) * 1000;
	}
      else
	{
	  req.tv_sec = 0;
	  req.tv_nsec = sleep_time_ns;
	}
      nanosleep (&req, NULL);

      if (!overbridge_get_status (ob))
	{
	  break;
	}
    }

  return NULL;
}

void *
run_audio_and_o2j_midi (void *data)
{
  size_t rsj2o, bytes, frames;
  struct overbridge *ob = data;

  set_rt_priority (ob->priority);

  while (overbridge_get_status (ob) == OB_STATUS_READY);

  //status == OB_STATUS_BOOT_OVERBRIDGE

  prepare_cycle_in (ob);
  prepare_cycle_out (ob);
  prepare_cycle_in_midi (ob);

  while (1)
    {
      ob->j2o_latency = 0;
      ob->j2o_max_latency = 0;
      ob->reading_at_j2o_end = 0;

      //status == OB_STATUS_BOOT_OVERBRIDGE

      pthread_spin_lock (&ob->lock);
      dll_counter_init (&ob->o2j_dll_counter, OB_SAMPLE_RATE,
			ob->frames_per_transfer);
      ob->status = OB_STATUS_BOOT_JACK;
      pthread_spin_unlock (&ob->lock);

      while (overbridge_get_status (ob) >= OB_STATUS_BOOT_JACK)
	{
	  libusb_handle_events_completed (ob->context, NULL);
	}

      if (overbridge_get_status (ob) <= OB_STATUS_STOP)
	{
	  break;
	}

      overbridge_set_status (ob, OB_STATUS_BOOT_OVERBRIDGE);

      rsj2o = jack_ringbuffer_read_space (ob->j2o_rb);
      frames = rsj2o / ob->j2o_frame_bytes;
      bytes = frames * ob->j2o_frame_bytes;
      jack_ringbuffer_read_advance (ob->j2o_rb, bytes);
      memset (ob->j2o_buf, 0, ob->j2o_buf_size);
    }

  return NULL;
}

int
overbridge_activate (struct overbridge *ob, jack_client_t * jclient,
		     int priority)
{
  int ret;

  ob->priority = priority;

  ob->j2o_rb = jack_ringbuffer_create (MAX_OW_LATENCY * ob->j2o_frame_bytes);
  jack_ringbuffer_mlock (ob->j2o_rb);
  ob->o2j_rb = jack_ringbuffer_create (MAX_OW_LATENCY * ob->o2j_frame_bytes);
  jack_ringbuffer_mlock (ob->o2j_rb);

  ob->jclient = jclient;
  ob->s_counter = 0;

  ob->status = OB_STATUS_READY;
  debug_print (1, "Starting j2o MIDI thread...\n");
  ret = pthread_create (&ob->midi_tinfo, NULL, run_j2o_midi, ob);
  if (ret)
    {
      error_print ("Could not start MIDI thread\n");
      return ret;
    }

  debug_print (1, "Starting audio and o2j MIDI thread...\n");
  ret =
    pthread_create (&ob->audio_and_o2j_midi, NULL, run_audio_and_o2j_midi,
		    ob);
  if (ret)
    {
      error_print ("Could not start device thread\n");
    }

  return ret;
}

void
overbridge_wait (struct overbridge *ob)
{
  pthread_join (ob->audio_and_o2j_midi, NULL);
  pthread_join (ob->midi_tinfo, NULL);
}

const char *
overbrigde_get_err_str (overbridge_err_t errcode)
{
  return ob_err_strgs[errcode];
}

void
overbridge_destroy (struct overbridge *ob)
{
  usb_shutdown (ob);
  free_transfers (ob);
  if (ob->j2o_rb)
    {
      jack_ringbuffer_free (ob->j2o_rb);
    }
  if (ob->o2j_rb)
    {
      jack_ringbuffer_free (ob->o2j_rb);
    }
  jack_ringbuffer_free (ob->o2j_rb_midi);
  free (ob->j2o_buf);
  free (ob->j2o_buf_res);
  free (ob->o2j_buf);
  free (ob->usb_data_in);
  free (ob->usb_data_out);
  free (ob->j2o_midi_data);
  free (ob->o2j_midi_data);
  pthread_spin_destroy (&ob->lock);
}

inline overbridge_status_t
overbridge_get_status (struct overbridge *ob)
{
  overbridge_status_t status;
  pthread_spin_lock (&ob->lock);
  status = ob->status;
  pthread_spin_unlock (&ob->lock);
  return status;
}

inline void
overbridge_set_status (struct overbridge *ob, overbridge_status_t status)
{
  pthread_spin_lock (&ob->lock);
  ob->status = status;
  pthread_spin_unlock (&ob->lock);
}

inline int
overbridge_is_j2o_audio_enable (struct overbridge *ob)
{
  int enabled;
  pthread_spin_lock (&ob->lock);
  enabled = ob->j2o_audio_enabled;
  pthread_spin_unlock (&ob->lock);
  return enabled;
}

inline void
overbridge_set_j2o_audio_enable (struct overbridge *ob, int enabled)
{
  int last = overbridge_is_j2o_audio_enable (ob);
  if (last != enabled)
    {
      pthread_spin_lock (&ob->lock);
      ob->j2o_audio_enabled = enabled;
      pthread_spin_unlock (&ob->lock);
      debug_print (1, "Setting j2o audio to %d...\n", enabled);
    }
}

int
overbridge_list_devices ()
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

      if (overbridge_is_valid_device (desc.idVendor, desc.idProduct, &name))
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
overbridge_get_bus_address (int index, char *name, uint8_t * bus,
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
      if (overbridge_is_valid_device
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
