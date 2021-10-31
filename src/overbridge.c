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
#include <arpa/inet.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

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

#define MAX_USB_DEPTH 7

#define AUDIO_IN_EP  0x83
#define AUDIO_OUT_EP 0x03
#define MIDI_IN_EP   0x81
#define MIDI_OUT_EP  0x01

#define MIDI_BUF_EVENTS 64
#define MIDI_BUF_SIZE (MIDI_BUF_EVENTS * OB_MIDI_EVENT_SIZE)

#define USB_BULK_MIDI_SIZE 512

#define SAMPLE_TIME_NS (1000000000 / ((int)OB_SAMPLE_RATE))

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

static const struct overbridge_device_desc OB_DEVICE_DESCS[] = {
  DIGITAKT_DESC, DIGITONE_DESC, AFMK2_DESC, ARMK2_DESC, DKEYS_DESC,
  AHMK1_DESC, AHMK2_DESC
};

static const int OB_DEVICE_DESCS_N =
  sizeof (OB_DEVICE_DESCS) / sizeof (struct overbridge_device_desc);

static void prepare_cycle_in ();
static void prepare_cycle_out ();
static void prepare_cycle_in_midi ();

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
  double e;
  int32_t *s;
  overbridge_status_t status;

  e = jack_get_time () * 1.0e-6 - ob->o2j_counter.i1.time;
  pthread_spin_lock (&ob->lock);
  ob->o2j_counter.i0.time = ob->o2j_counter.i1.time;
  ob->o2j_counter.i1.time += ob->o2j_counter.b * e + ob->o2j_counter.e2;
  ob->o2j_counter.e2 += ob->o2j_counter.c * e;
  ob->o2j_counter.i0.frames = ob->o2j_counter.i1.frames;
  ob->o2j_counter.i1.frames += ob->frames_per_transfer;
  status = ob->status;
  pthread_spin_unlock (&ob->lock);

  f = ob->o2j_buf;
  for (int i = 0; i < ob->blocks_per_transfer; i++)
    {
      blk = get_nth_usb_in_blk (ob, i);
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < ob->device_desc.outputs; k++)
	    {
	      hv = ntohl (*s);
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
      error_print ("o2j: Buffer overflow. Discarding data...\n");
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
  double e;
  static int running = 0;

  rsj2o = jack_ringbuffer_read_space (ob->j2o_rb);
  if (running)
    {
      if (ob->j2o_latency < rsj2o)
	{
	  ob->j2o_latency = rsj2o;
	}

      if (rsj2o >= ob->j2o_buf_size)
	{
	  e = jack_get_time () * 1.0e-6 - ob->j2o_counter.i1.time;
	  pthread_spin_lock (&ob->lock);
	  ob->j2o_counter.i0.time = ob->j2o_counter.i1.time;
	  ob->j2o_counter.i1.time +=
	    ob->j2o_counter.b * e + ob->j2o_counter.e2;
	  ob->j2o_counter.e2 += ob->j2o_counter.c * e;
	  ob->j2o_counter.i0.frames = ob->j2o_counter.i1.frames;
	  ob->j2o_counter.i1.frames += ob->frames_per_transfer;
	  pthread_spin_unlock (&ob->lock);

	  jack_ringbuffer_read (ob->j2o_rb, (void *) ob->j2o_buf,
				ob->j2o_buf_size);
	}
      else
	{
	  debug_print (2,
		       "j2o: Can not read enough data from ring buffer (%zu < %zu). Resampling...\n",
		       rsj2o, ob->j2o_buf_size);
	  frames = rsj2o / ob->j2o_frame_bytes;
	  bytes = frames * ob->j2o_frame_bytes;
	  jack_ringbuffer_read (ob->j2o_rb, (void *) ob->j2o_buf_res, bytes);
	  ob->j2o_data.input_frames = frames;
	  ob->j2o_data.src_ratio = (double) ob->frames_per_transfer / frames;
	  //We should NOT use the simple API but since this only happens very occasionally and mostly at startup, this has very low impact on audio quality.
	  res = src_simple (&ob->j2o_data, SRC_SINC_FASTEST,
			    ob->device_desc.inputs);
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
    }
  else
    {
      if (rsj2o >= ob->j2o_buf_size)
	{
	  frames = rsj2o / ob->j2o_frame_bytes;
	  bytes = frames * ob->j2o_frame_bytes;
	  jack_ringbuffer_read_advance (ob->j2o_rb, bytes);
	  running = 1;
	}
    }

  f = ob->j2o_buf;
  for (int i = 0; i < ob->blocks_per_transfer; i++)
    {
      blk = get_nth_usb_out_blk (ob, i);
      ob->s_counter += OB_FRAMES_PER_BLOCK;
      blk->s_counter = htons (ob->s_counter);
      s = blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < ob->device_desc.inputs; k++)
	    {
	      hv = htonl (*f * INT_MAX);
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
      error_print ("Error on USB in transfer\n");
    }
  // start new cycle even if this one did not succeed
  prepare_cycle_in (xfr->user_data);
}

static void LIBUSB_CALL
cb_xfr_out (struct libusb_transfer *xfr)
{
  if (xfr->status != LIBUSB_TRANSFER_COMPLETED)
    {
      error_print ("Error on USB out transfer\n");
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
  overbridge_status_t status;
  struct overbridge *ob = xfr->user_data;

  pthread_spin_lock (&ob->lock);
  status = ob->status;
  pthread_spin_unlock (&ob->lock);

  if (status < OB_STATUS_RUN)
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
		    ("o2j: Buffer MIDI overflow. Discarding data...\n");
		}
	    }
	  length += OB_MIDI_EVENT_SIZE;
	}
    }
  else
    {
      if (xfr->status != LIBUSB_TRANSFER_TIMED_OUT)
	{
	  error_print ("Error on USB MIDI in transfer\n");
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
      error_print ("Error on USB MIDI out transfer\n");
    }
}

static void
prepare_cycle_out (struct overbridge *ob)
{
  libusb_fill_interrupt_transfer (ob->xfr_out, ob->device,
				  AUDIO_OUT_EP, (void *) ob->usb_data_out,
				  ob->usb_data_out_blk_len *
				  ob->blocks_per_transfer, cb_xfr_out, ob,
				  100);
  int r = libusb_submit_transfer (ob->xfr_out);
  if (r != 0)
    {
      error_print ("Error when submitting USB out trasfer\n");
    }
}

static void
prepare_cycle_in (struct overbridge *ob)
{
  libusb_fill_interrupt_transfer (ob->xfr_in, ob->device,
				  AUDIO_IN_EP, (void *) ob->usb_data_in,
				  ob->usb_data_in_blk_len *
				  ob->blocks_per_transfer, cb_xfr_in, ob,
				  100);
  int r = libusb_submit_transfer (ob->xfr_in);
  if (r != 0)
    {
      error_print ("Error when submitting USB in trasfer\n");
    }
}

static void
prepare_cycle_in_midi (struct overbridge *ob)
{
  libusb_fill_bulk_transfer (ob->xfr_in_midi, ob->device,
			     MIDI_IN_EP, (void *) ob->o2j_midi_data,
			     USB_BULK_MIDI_SIZE, cb_xfr_in_midi, ob, 100);

  int r = libusb_submit_transfer (ob->xfr_in_midi);
  if (r != 0)
    {
      error_print ("Error when submitting USB MIDI in trasfer\n");
    }
}

static void
prepare_cycle_out_midi (struct overbridge *ob)
{
  libusb_fill_bulk_transfer (ob->xfr_out_midi, ob->device, MIDI_OUT_EP,
			     (void *) ob->j2o_midi_data,
			     USB_BULK_MIDI_SIZE, cb_xfr_out_midi, ob, 100);

  int r = libusb_submit_transfer (ob->xfr_out_midi);
  if (r != 0)
    {
      error_print ("Error when submitting USB MIDI OUT trasfer\n");
    }
}

// initialization taken from sniffed session

static overbridge_err_t
overbridge_init_priv (struct overbridge *ob, char *device_name)
{
  int i;
  int ret;

  // libusb setup
  ret = libusb_init (NULL);
  if (ret != LIBUSB_SUCCESS)
    {
      return OB_LIBUSB_INIT_FAILED;
    }

  for (i = 0; i < OB_DEVICE_DESCS_N; i++)
    {
      if (strcmp (OB_DEVICE_DESCS[i].name, device_name))
	{
	  continue;
	}

      debug_print (2, "Checking for %s...\n", OB_DEVICE_DESCS[i].name);
      ob->device =
	libusb_open_device_with_vid_pid (NULL, ELEKTRON_VID,
					 OB_DEVICE_DESCS[i].pid);

      break;
    }
  if (i == OB_DEVICE_DESCS_N)
    {
      return OB_CANT_FIND_DEV;
    }
  if (ob->device)
    {
      ob->device_desc = OB_DEVICE_DESCS[i];
      printf ("Device: %s (outputs: %d, inputs: %d)\n", ob->device_desc.name,
	      ob->device_desc.outputs, ob->device_desc.inputs);
    }
  else
    {
      return OB_CANT_OPEN_DEV;
    }

  ret = libusb_set_configuration (ob->device, 1);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_SET_USB_CONFIG;
    }
  ret = libusb_claim_interface (ob->device, 1);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_CLAIM_IF;
    }
  ret = libusb_set_interface_alt_setting (ob->device, 1, 3);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_SET_ALT_SETTING;
    }
  ret = libusb_claim_interface (ob->device, 2);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_CLAIM_IF;
    }
  ret = libusb_set_interface_alt_setting (ob->device, 2, 2);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_SET_ALT_SETTING;
    }
  ret = libusb_claim_interface (ob->device, 3);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_CLAIM_IF;
    }
  ret = libusb_set_interface_alt_setting (ob->device, 3, 0);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_SET_ALT_SETTING;
    }
  ret = libusb_clear_halt (ob->device, AUDIO_IN_EP);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_CLEAR_EP;
    }
  ret = libusb_clear_halt (ob->device, AUDIO_OUT_EP);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_CLEAR_EP;
    }
  ret = libusb_clear_halt (ob->device, MIDI_IN_EP);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_CLEAR_EP;
    }
  ret = libusb_clear_halt (ob->device, MIDI_OUT_EP);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_CLEAR_EP;
    }
  ret = prepare_transfers (ob);
  if (LIBUSB_SUCCESS != ret)
    {
      return OB_CANT_PREPARE_TRANSFER;
    }

  return OB_OK;
}

static void
usb_shutdown (struct overbridge *ob)
{
  libusb_close (ob->device);
  libusb_exit (NULL);
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
  overbridge_status_t status;
  int pos;
  jack_time_t last_time;
  jack_time_t event_time;
  jack_time_t diff;
  struct timespec req;
  struct ob_midi_event event;
  struct overbridge *ob = data;
  int sleep_time_ns = SAMPLE_TIME_NS * jack_get_buffer_size (ob->jclient) / 2;	//Average wait time

  last_time = jack_get_time ();
  do
    {
      memset (ob->j2o_midi_data, 0, USB_BULK_MIDI_SIZE);
      diff = 0;
      pos = 0;

      while (jack_ringbuffer_read_space (ob->j2o_rb_midi) >=
	     sizeof (struct ob_midi_event) && pos < USB_BULK_MIDI_SIZE)
	{
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

      pthread_spin_lock (&ob->lock);
      status = ob->status;
      pthread_spin_unlock (&ob->lock);
    }
  while (status);

  return NULL;
}

void *
run (void *data)
{
  struct overbridge *ob = data;
  double w;
  double dtime;

  //Taken from https://github.com/jackaudio/tools/blob/master/zalsa/alsathread.cc.
  dtime = ob->frames_per_transfer / OB_SAMPLE_RATE;
  w = 2 * M_PI * 0.1 * dtime;
  ob->j2o_counter.b = 1.6 * w;
  ob->j2o_counter.c = w * w;
  ob->o2j_counter.b = ob->j2o_counter.b;
  ob->o2j_counter.c = ob->j2o_counter.c;

  //TODO: add this to xrun handler and perhaps clear the buffers. See paper.
  ob->j2o_counter.e2 = dtime;
  ob->j2o_counter.i0.time = jack_get_time () * 1.0e-6;
  ob->j2o_counter.i1.time = ob->o2j_counter.i0.time + ob->o2j_counter.e2;
  ob->o2j_counter.e2 = ob->j2o_counter.e2;
  ob->o2j_counter.i0.time = ob->j2o_counter.i0.time;
  ob->o2j_counter.i1.time = ob->j2o_counter.i1.time;

  ob->j2o_counter.i0.frames = 0;
  ob->j2o_counter.i1.frames = ob->frames_per_transfer;
  ob->o2j_counter.i0.frames = 0;
  ob->o2j_counter.i1.frames = ob->frames_per_transfer;

  prepare_cycle_in (ob);
  prepare_cycle_out (ob);
  prepare_cycle_in_midi (ob);

  while (overbridge_get_status (ob) >= OB_STATUS_BOOT)
    {
      libusb_handle_events_completed (NULL, NULL);
    }

  return NULL;
}

int
overbridge_run (struct overbridge *ob, jack_client_t * client)
{
  int ret;
  size_t max_bufsize;

  ob->jbufsize = jack_get_buffer_size (client);

  max_bufsize =
    ob->frames_per_transfer >
    ob->jbufsize ? ob->frames_per_transfer : ob->jbufsize;
  ob->j2o_rb = jack_ringbuffer_create (max_bufsize * ob->j2o_frame_bytes * 4);
  jack_ringbuffer_mlock (ob->j2o_rb);
  ob->o2j_rb = jack_ringbuffer_create (max_bufsize * ob->o2j_frame_bytes * 4);
  jack_ringbuffer_mlock (ob->o2j_rb);

  ob->jclient = client;
  ob->s_counter = 0;
  ob->status = OB_STATUS_BOOT;

  debug_print (1, "Starting MIDI thread...\n");
  ret = pthread_create (&ob->midi_tinfo, NULL, run_j2o_midi, ob);
  if (ret)
    {
      error_print ("Could not start MIDI thread\n");
      return ret;
    }

  debug_print (1, "Starting device thread...\n");
  ret = pthread_create (&ob->midi_tinfo, NULL, run, ob);
  if (ret)
    {
      error_print ("Could not start device thread\n");
    }

  return ret;
}

void
overbridge_wait (struct overbridge *ob)
{
  pthread_join (ob->midi_tinfo, NULL);
}

const char *
overbrigde_get_err_str (overbridge_err_t errcode)
{
  return ob_err_strgs[errcode];
}

overbridge_err_t
overbridge_init (struct overbridge *ob, char *device_name,
		 int blocks_per_transfer)
{
  struct overbridge_usb_blk *blk;
  overbridge_err_t r = overbridge_init_priv (ob, device_name);

  if (r == OB_OK)
    {
      pthread_spin_init (&ob->lock, PTHREAD_PROCESS_SHARED);

      ob->blocks_per_transfer = blocks_per_transfer;
      ob->frames_per_transfer = OB_FRAMES_PER_BLOCK * ob->blocks_per_transfer;

      ob->usb_data_in_blk_len =
	sizeof (struct overbridge_usb_blk) +
	sizeof (int32_t) * OB_FRAMES_PER_BLOCK * ob->device_desc.outputs;
      ob->usb_data_out_blk_len =
	sizeof (struct overbridge_usb_blk) +
	sizeof (int32_t) * OB_FRAMES_PER_BLOCK * ob->device_desc.inputs;
      int usb_data_in_len = ob->usb_data_in_blk_len * ob->blocks_per_transfer;
      int usb_data_out_len =
	ob->usb_data_out_blk_len * ob->blocks_per_transfer;
      ob->usb_data_in = malloc (usb_data_in_len);
      ob->usb_data_out = malloc (usb_data_out_len);
      memset (ob->usb_data_in, 0, usb_data_in_len);
      memset (ob->usb_data_out, 0, usb_data_out_len);

      for (int i = 0; i < ob->blocks_per_transfer; i++)
	{
	  blk = get_nth_usb_out_blk (ob, i);
	  blk->header = htons (0x07ff);
	}

      ob->j2o_frame_bytes = OB_BYTES_PER_FRAME * ob->device_desc.inputs;
      ob->o2j_frame_bytes = OB_BYTES_PER_FRAME * ob->device_desc.outputs;

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
      ob->j2o_rb_midi = jack_ringbuffer_create (MIDI_BUF_SIZE * 4);
      ob->o2j_rb_midi = jack_ringbuffer_create (MIDI_BUF_SIZE * 4);
      jack_ringbuffer_mlock (ob->j2o_rb_midi);
      jack_ringbuffer_mlock (ob->o2j_rb_midi);
    }
  else
    {
      usb_shutdown (ob);
    }
  return r;
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
}

overbridge_status_t
overbridge_get_status (struct overbridge *ob)
{
  overbridge_status_t status;
  pthread_spin_lock (&ob->lock);
  status = ob->status;
  pthread_spin_unlock (&ob->lock);
  return status;
}

void
overbridge_set_status (struct overbridge *ob, overbridge_status_t status)
{
  pthread_spin_lock (&ob->lock);
  ob->status = status;
  pthread_spin_unlock (&ob->lock);
}

overbridge_err_t
overbridge_list_devices ()
{
  libusb_context *context = NULL;
  libusb_device **list = NULL;
  int ret = 0;
  ssize_t count = 0;
  libusb_device *device;
  struct libusb_device_descriptor desc;
  size_t i;
  int j;
  int k;
  int ports;
  uint8_t port_numbers[MAX_USB_DEPTH];
  uint8_t bus;
  uint8_t address;

  ret = libusb_init (&context);
  if (ret != LIBUSB_SUCCESS)
    {
      return OB_LIBUSB_INIT_FAILED;
    }

  count = libusb_get_device_list (context, &list);

  for (i = 0; i < count; i++)
    {
      device = list[i];
      ret = libusb_get_device_descriptor (device, &desc);

      if (desc.idVendor == ELEKTRON_VID)
	{
	  for (j = 0; j < OB_DEVICE_DESCS_N; j++)
	    {
	      if (OB_DEVICE_DESCS[j].pid == desc.idProduct)
		{
		  bus = libusb_get_bus_number (device);
		  address = libusb_get_device_address (device);
		  ports = libusb_get_port_numbers (device,
						   port_numbers,
						   MAX_USB_DEPTH);
		  fprintf (stderr, "Bus %03d Port %03d", bus,
			   port_numbers[0]);
		  for (k = 1; k < ports; k++)
		    {
		      fprintf (stderr, ":%03d", port_numbers[k]);
		    }
		  fprintf (stderr, " Device %03d: ID %04x:%04x %s\n",
			   address, desc.idVendor, desc.idProduct,
			   OB_DEVICE_DESCS[j].name);
		}
	    }
	}
    }

  libusb_free_device_list (list, count);
  libusb_exit (context);

  return OB_OK;
}
