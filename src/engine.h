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

 Protocol details known so far for Overbridge 1 devices
 ------------------------------------------------------

 This applies to the Analog Four MKI, Analog Keys and Analog Rytm MKI with an
 OS that reports Overbridge protocol 113 and async header 2 (vendor request
 0xc0 4 returns 00 71 00 02), which is the case of the Analog Four MKI OS 1.56.

 These devices are USB full speed devices that use isochronous transfers.
 Every isochronous packet is a block holding the frames of 1 ms. As the device
 is the clock master, it sends 48 frames per block most of the time and
 sometimes 47, while the packets have space for 49. The host sends the same
 amount of frames it receives (implicit feedback) and always sends packets of
 the maximum size.

 Header values are big-endian but samples are little-endian (LSB first)!
 Samples are 16 bits or packed 24 bits integers, the same size for all the
 tracks in one direction.

 The frame counter is the cumulative amount of frames, including the frames of
 the block. The device counter runs freely while the host counter starts from
 0 on every stream start, so its first block holds the frames of the block.

 Data format FROM the device (USB isochronous transfer, EP 0x83)
 ---------------------------------------------------------------

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | frames        | 0x00          | frame counter (uint32_t)      |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | frame counter (cont.)         | samples (frames * frame size) |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
 ...

 Data format TO the device (USB isochronous transfer, EP 0x03)
 -------------------------------------------------------------

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | frames        | msg. offset   | frame counter (uint32_t)      |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | frame counter (cont.)         | message region (98 bytes)     |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
 ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 | samples (frames * frame size) and zero padding                |
 ...

 The message offset is the position of a message in the message region or
 0xff if there is none. Overbridge sends a few messages at the start of a
 stream but they are not needed.

 The first 3 blocks of a stream are empty and the host sends them back as
 blocks of 48 frames.

 The track layout, which is configured with the Overbridge Control Panel, is
 stored in the device. Before every stream start, the host writes the stored
 layout as the active layout. Without this, the device sends no audio and
 half of the blocks are bigger than the maximum packet size, even on the first
 stream after setting the configuration. These are the vendor requests
 involved, where wIndex is 0 for the device to host direction and 1 for the
 host to device one.

 bmRequestType  bRequest  wValue      Data
 0xc1           2         0           Stored bit depth (1 byte)
 0xc1           3         0           Stored channel map (8 bytes)
 0x41           0         Bit depth   -
 0x41           1         0           Active channel map (8 bytes)

 Channel maps contain the track codes, with 0xff for unused slots. Known codes
 are 0xa0 and 0xa1 for the main outputs, 0x80 + n for the synth track n + 1
 output, 0xc0 and 0xc1 for the external inputs, 0x20 and 0x21 for the main
 inputs and n for the synth track n + 1 input. 0x80 is the device to host bit.

 The alternate setting used for each direction is the smallest one with enough
 space for 49 frames.

 */

#include <libusb.h>
#include <samplerate.h>
#include <pthread.h>
#include "utils.h"
#include "overwitch.h"

#define GET_NTH_USB_BLK_V2(blks,blk_len,n) ((struct ow_engine_usb_blk_v2 *) &blks[n * blk_len])
#define GET_NTH_INPUT_USB_BLK_V2(engine,n) (GET_NTH_USB_BLK_V2((engine)->usb.xfr_audio_in_data, (engine)->usb.audio_in_blk_len, n))
#define GET_NTH_OUTPUT_USB_BLK_V2(engine,n) (GET_NTH_USB_BLK_V2((engine)->usb.xfr_audio_out_data, (engine)->usb.audio_out_blk_len, n))

#define OB2_PRIVATE_LEN 28

#define OB1_TRANSFERS 3
#define OB1_OUT_MSG_LEN 98
#define OB1_OUT_MSG_NONE 0xff
#define OB1_MAP_LEN 8
#define OB1_MAP_EMPTY 0xff
#define OB1_O2H 0
#define OB1_H2O 1
//This must be a power of 2.
#define OB1_FRAMES_RING_LEN 64

#define OB1_USB_AUDIO_IN_BLK_LEN(frame_size) (sizeof (struct ow_engine_usb_blk_v1) + OB1_MAX_FRAMES_PER_BLOCK * (frame_size))
#define OB1_USB_AUDIO_OUT_BLK_LEN(frame_size) (sizeof (struct ow_engine_usb_blk_v1) + OB1_OUT_MSG_LEN + OB1_MAX_FRAMES_PER_BLOCK * (frame_size))

#define OB_NAME_MAX_LEN 32

#define OW_ENGINE_MAX_INTERFACES 32

//This stores "%s @ %03d,%03d" where the string is OW_LABEL_MAX_LEN so more
//space than OW_LABEL_MAX_LEN is needed.
#define OW_ENGINE_NAME_MAX_LEN (OW_LABEL_MAX_LEN * 2)

struct ow_engine;

//Version specific steps of the audio thread
struct ow_engine_ops
{
  //Called before notifying readiness to the host side. Optional.
  void (*prepare) (struct ow_engine *);
  //Called when booting, right before handling the USB events. Optional.
  int (*start) (struct ow_engine *);
  //Called when the engine has stopped.
  void (*stop) (struct ow_engine *);
};

struct ow_engine
{
  const struct ow_engine_ops *ops;
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
  //Frame sizes in the o2h_audio and h2o_audio buffers, which store floats
  size_t o2h_audio_frame_size;
  size_t h2o_audio_frame_size;
  struct
  {
    libusb_context *context;
    libusb_device *device;
    libusb_device_handle *device_handle;
    unsigned int xfr_timeout;
    //Audio
    int audio_in_interface;
    int audio_out_interface;
    uint16_t audio_frames_counter;
    struct libusb_transfer *xfr_audio_in;
    struct libusb_transfer *xfr_audio_out;
    uint8_t *xfr_audio_in_data;
    uint8_t *xfr_audio_out_data;
    size_t audio_in_blk_len;
    size_t audio_out_blk_len;
    int xfr_audio_in_data_len;
    int xfr_audio_out_data_len;
    //Overbridge 1 audio
    int audio_in_alt_setting;
    int audio_out_alt_setting;
    struct libusb_transfer *xfr_audio_in_v1[OB1_TRANSFERS];
    struct libusb_transfer *xfr_audio_out_v1[OB1_TRANSFERS];
    int xfrs_in_flight;
    //Received transfers used to update the Overbridge side of the DLL
    unsigned int audio_in_dll_updates;
    uint32_t audio_frames_counter_v1;
    uint32_t audio_in_frames_counter_v1;
    int audio_in_frames_counter_valid;
    //Frames of the received blocks, which are sent back to the device.
    uint8_t audio_in_frames[OB1_FRAMES_RING_LEN];
    unsigned int audio_in_frames_read;
    unsigned int audio_in_frames_written;
    unsigned int audio_in_errors;
    unsigned int audio_out_errors;
    unsigned int audio_out_frames_missing;
    //Interfaces whose kernel drivers need to be attached again
    int detached[OW_ENGINE_MAX_INTERFACES];
    int detached_count;
    uint8_t layout_map[2][OB1_MAP_LEN];
    uint8_t layout_depth[2];
    //Control
    struct libusb_transfer *xfr_control_out;
    struct libusb_transfer *xfr_control_in;
    uint8_t *xfr_control_out_data;
    uint8_t *xfr_control_in_data;
  } usb;
  //h2o resampler
  float *h2o_resampler_buf;
  SRC_DATA h2o_data;
  int reading_at_h2o_end;
  struct ow_context *context;
};

struct ow_engine_usb_blk_v2
{
  uint16_t header;
  uint16_t frames;
  uint8_t private[OB2_PRIVATE_LEN];
  int32_t data[];
};

//Blocks are not aligned so only bytes are used.
struct ow_engine_usb_blk_v1
{
  uint8_t frames;
  uint8_t msg_offset;
  uint8_t counter[4];
  uint8_t data[];
};

int ow_bytes_to_frame_bytes (int, int);

int ow_engine_init_mem_v1 (struct ow_engine *engine, unsigned int);

uint32_t ow_engine_read_usb_input_blocks_v1 (struct ow_engine *engine,
					     struct libusb_transfer *xfr);

uint32_t ow_engine_set_usb_output_frames_v1 (struct ow_engine *engine,
					     struct libusb_transfer *xfr);

void ow_engine_write_usb_output_blocks_v1 (struct ow_engine *engine,
					   struct libusb_transfer *xfr);

int ow_engine_get_alt_setting_v1 (const struct libusb_config_descriptor
				  *config, int interface, uint8_t endpoint,
				  size_t size, int *max_packet_size);

int ow_engine_check_layout_v1 (struct ow_engine *engine);

int ow_engine_get_desc_layout_v1 (const struct ow_device_desc *desc,
				  uint8_t map[2][OB1_MAP_LEN],
				  uint8_t depth[2]);

void ow_engine_print_usb_block_v1 (struct ow_engine *engine,
				   struct libusb_transfer *xfr, int o2h,
				   uint32_t * debug_counter);

void ow_engine_read_usb_input_blocks_v2 (struct ow_engine *engine);

void ow_engine_write_usb_output_blocks_v2 (struct ow_engine *engine);

int ow_engine_init_mem_v2 (struct ow_engine *engine, unsigned int);

unsigned int ow_engine_get_valid_blocks_per_transfer (unsigned int
						      blocks_per_transfer,
						      unsigned int min,
						      unsigned int max,
						      unsigned int def);

void ow_engine_free_mem (struct ow_engine *engine);

void ow_engine_print_usb_block_v2 (struct ow_engine *engine, int block,
				   int o2h, uint16_t * debug_counter);
