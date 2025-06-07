/*
 * engine.h
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

/*
 Protocol details known so far
 -----------------------------

 For USB configuration and transfer setup, please read the code :)

 All values are big-endian (MSB first)!
 Sample rate is 48kHz

 Data format TO Digitakt (USB interrupt transfer, EP 0x03)
 ----------------------------------------------------------------

 Raw data of the transfer consists of 24 blocks, each block holding
 - a fixed header
 - a sample counter (uint16 BE), increased by 7 per block (because
 each block contains 7 samples)
 - 7 samples (int32 BE) * 2 channels (interleaved)

 (total 168 samples, 2112 Bytes). Structure of a single block:

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | fixed header: 0x07FF          | sample counter (uint16_t)     |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                                                               |
 +                            unknown                            +
 ...
 +                           (28 Bytes)                          +
 |                                                               |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, Master Out 1, int32_t                              |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, Master Out 2, int32_t                              |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, Master Out 1, int32_t                              |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, Master Out 2, int32_t                              |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                                                               |
 +                        samples 3 .. 7                         +
 ...
 +                     (5*8 Bytes = 40 Bytes)                    +
 |                                                               |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+



 Data format FROM Digitakt (USB interrupt transfer, EP 0x83)
 ----------------------------------------------------------------

 Raw data of 24 blocks (total 8832 Bytes). Structure of a
 single block:

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | fixed header: 0x0700          | sample counter (uint16_t)     |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                                                               |
 +                            unknown                            +
 ...
 +                           (28 Bytes)                          +
 |                                                               |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, Master 1/FX1, int32_t                              |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, Master 2/FX2, int32_t                              |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, CH1, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, CH2, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, CH3, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, CH4, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, CH5, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, CH6, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, CH7, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, CH8, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, EXT IN 1, int32_t                                  |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  1, EXT IN 2, int32_t                                  |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, Master 1/FX1, int32_t                              |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, Master 2/FX2, int32_t                              |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, CH1, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, CH2, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, CH3, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, CH4, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, CH5, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, CH6, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, CH7, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, CH8, int32_t                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, EXT IN 1, int32_t                                  |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | sample  2, EXT IN 2, int32_t                                  |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                                                               |
 +                        samples 3 .. 7                         +
 ...
 +                     (5*48 Bytes = 240 Bytes)                  +
 |                                                               |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

--------------------------------------------------------------------------------

 Protocol details known so far for Overbridge 1 devices (MKI)
 ------------------------------------------------------------

 MK1 devices use USB isochronous transfers with 3 packets.
 Each isochronous packet is an Overbridge block.
 Every block has 48 frames and the frame counter reflects this increment.
 Sample size and track amount can be configured.
 All sample values are little-endian (LSB first)!

 Data format TO Analog Rytm MK1 (USB isochronous transfer, EP 0x03)
 ------------------------------------------------------------------

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | fixed header: 0x30FF          | frame counter (uint32_t) MSB  |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 + sample counter (uint32_t) LSB | samples                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 Data format FROM Analog Rytm MK1 (USB isochronous transfer, EP 0x83)
 --------------------------------------------------------------------

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | fixed header: 0x3000          | frame counter (uint32_t) MSB  |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 + frame counter (uint32_t) LSB  | unknown (98 bytes)            |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 + samples                                                       |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 + padding (4 bytes)                                             +
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 */

#include <libusb.h>
#include <samplerate.h>
#include <pthread.h>
#include "utils.h"
#include "overwitch.h"

#define GET_NTH_USB_BLK(blks,blk_len,n) ((void *) &blks[n * blk_len])
#define GET_NTH_INPUT_USB_BLK(engine,n) (GET_NTH_USB_BLK((engine)->usb.xfr_audio_in_data, (engine)->usb.audio_in_blk_size, n))
#define GET_NTH_OUTPUT_USB_BLK(engine,n) (GET_NTH_USB_BLK((engine)->usb.xfr_audio_out_data, (engine)->usb.audio_out_blk_size, n))

#define OB2_PADDING_LEN 28

#define OB1_OUT_PADDING_LEN 102

#define OB_NAME_MAX_LEN 32

//This stores "%s @ %03d,%03d" where the string is OW_LABEL_MAX_LEN so more
//space than OW_LABEL_MAX_LEN is needed.
#define OW_ENGINE_NAME_MAX_LEN (OW_LABEL_MAX_LEN * 2)

struct ow_engine
{
  char name[OW_ENGINE_NAME_MAX_LEN];
  char overbridge_name[OB_NAME_MAX_LEN];
  struct ow_device *device;
  ow_engine_status_t status;
  unsigned int blocks_per_transfer;
  unsigned int frames_per_block;
  unsigned int frames_per_transfer;
  pthread_spinlock_t lock;
  //Latencies are measured in frames
  size_t latency_o2h;
  size_t latency_o2h_min;
  size_t latency_o2h_max;
  size_t latency_h2o;
  size_t latency_h2o_min;
  size_t latency_h2o_max;
  pthread_t thread;
  size_t h2o_transfer_size;
  size_t o2h_transfer_size;
  float *h2o_transfer_buf;
  float *o2h_transfer_buf;
  size_t o2h_frame_size;
  size_t h2o_frame_size;
  struct
  {
    libusb_context *context;
    libusb_device *device;
    libusb_device_handle *device_handle;
    unsigned int xfr_timeout;
    //Audio
    int audio_in_interface;
    int audio_out_interface;
    uint32_t audio_frames_counter_ob1;
    uint16_t audio_frames_counter_ob2;
    struct libusb_transfer *xfr_audio_in;
    struct libusb_transfer *xfr_audio_out;
    uint8_t *xfr_audio_in_data;
    uint8_t *xfr_audio_out_data;
    size_t audio_in_blk_size;
    size_t audio_out_blk_size;
    int xfr_audio_in_data_size;
    int xfr_audio_out_data_size;
    //Control
    struct libusb_transfer *xfr_control_out;
    struct libusb_transfer *xfr_control_in;
    uint8_t *xfr_control_out_data;
    uint8_t *xfr_control_in_data;
  } usb;
  //j2o resampler
  float *h2o_resampler_buf;
  SRC_DATA h2o_data;
  int reading_at_h2o_end;
  struct ow_context *context;
};

struct ow_engine_usb_blk_ob1_in
{
  uint16_t header;
  uint16_t frames_msb;
  uint16_t frames_lsb;
  uint8_t data[];
};

struct ow_engine_usb_blk_ob1_out
{
  uint16_t header;
  uint16_t frames_msb;
  uint16_t frames_lsb;
  uint8_t padding[OB1_OUT_PADDING_LEN];
  uint8_t data[];
};

struct ow_engine_usb_blk_ob2
{
  uint16_t header;
  uint16_t frames;
  uint8_t padding[OB2_PADDING_LEN];
  uint8_t data[];
};

int ow_bytes_to_frame_bytes (int, int);

void ow_engine_read_usb_input_blocks (struct ow_engine *engine, int print);

void ow_engine_write_usb_output_blocks (struct ow_engine *engine, int print);

unsigned int ow_engine_get_valid_blocks_per_transfer (unsigned int
						      blocks_per_transfer,
						      unsigned int min,
						      unsigned int max,
						      unsigned int def);

int ow_engine_init_mem (struct ow_engine *engine,
			unsigned int blocks_per_transfer,
			size_t usb_audio_in_blk_size,
			size_t usb_audio_out_blk_size);

void ow_engine_free_mem (struct ow_engine *);
