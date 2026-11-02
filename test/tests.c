#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/jclient.h"
#include "../src/engine.h"
#include "../src/common.h"
#include "../src/message.h"

#define BLOCKS 6
#define TRACKS 6
#define NFRAMES 64

static const struct ow_device_desc TESTDEV_DESC_V2 = {
  .pid = 0,
  .version = OW_DEVICE_VERSION_2,
  .name = "Test Device Type 2",
  .inputs = TRACKS,
  .outputs = TRACKS,
  .input_tracks = {{.name = "T1",.size = 4},
		   {.name = "T2",.size = 4},
		   {.name = "T3",.size = 4},
		   {.name = "T4",.size = 4},
		   {.name = "T5",.size = 4},
		   {.name = "T6",.size = 4}},
  .output_tracks = {{.name = "T1",.size = 4},
		    {.name = "T2",.size = 4},
		    {.name = "T3",.size = 4},
		    {.name = "T4",.size = 4},
		    {.name = "T5",.size = 4},
		    {.name = "T6",.size = 4}},
};

static const struct ow_device_desc TESTDEV_DESC_V2_1 = {
  .pid = 0,
  .version = OW_DEVICE_VERSION_2_1,
  .name = "Test Device Type 3",
  .inputs = TRACKS,
  .outputs = TRACKS,
  .input_tracks = {{.name = "T1",.size = 4},
		   {.name = "T2",.size = 4},
		   {.name = "T3",.size = 3},
		   {.name = "T4",.size = 3},
		   {.name = "T5",.size = 3},
		   {.name = "T6",.size = 3}},
  .output_tracks = {{.name = "T1",.size = 4},
		    {.name = "T2",.size = 4},
		    {.name = "T3",.size = 3},
		    {.name = "T4",.size = 3},
		    {.name = "T5",.size = 3},
		    {.name = "T6",.size = 3}},
};

static const struct ow_device_desc TESTDEV_DESC_SIZE = {
  .pid = 0,
  .version = OW_DEVICE_VERSION_2_1,
  .name = "Test Device Size",
  .inputs = 2,
  .outputs = 4,
  .input_tracks = {{.name = "T1",.size = 4},
		   {.name = "T2",.size = 4}},
  .output_tracks = {{.name = "T1",.size = 4},
		    {.name = "T2",.size = 4},
		    {.name = "T3",.size = 3},
		    {.name = "T4",.size = 3}}
};

//2 tracks of 16 bits in both directions
static const struct ow_device_desc TESTDEV_DESC_V1_16 = {
  .pid = 0,
  .version = OW_DEVICE_VERSION_1,
  .name = "Test Device V1 16 bits",
  .inputs = 2,
  .outputs = 2,
  .input_tracks = {{.name = "Main L Input",.size = 2},
		   {.name = "Main R Input",.size = 2}},
  .output_tracks = {{.name = "Main L",.size = 2},
		    {.name = "Main R",.size = 2}}
};

//3 tracks of 24 bits in both directions
static const struct ow_device_desc TESTDEV_DESC_V1_24 = {
  .pid = 0,
  .version = OW_DEVICE_VERSION_1,
  .name = "Test Device V1 24 bits",
  .inputs = 3,
  .outputs = 3,
  .input_tracks = {{.name = "T1",.size = 3},
		   {.name = "T2",.size = 3},
		   {.name = "T3",.size = 3}},
  .output_tracks = {{.name = "T1",.size = 3},
		    {.name = "T2",.size = 3},
		    {.name = "T3",.size = 3}}
};

//4 tracks of 24 bits from the device and 2 tracks of 24 bits to the device
static const struct ow_device_desc TESTDEV_DESC_V1_4_2 = {
  .pid = 0,
  .version = OW_DEVICE_VERSION_1,
  .name = "Test Device V1 4 and 2 tracks",
  .inputs = 2,
  .outputs = 4,
  .input_tracks = {{.name = "Main L Input",.size = 3},
		   {.name = "Main R Input",.size = 3}},
  .output_tracks = {{.name = "Main L",.size = 3},
		    {.name = "Main R",.size = 3},
		    {.name = "Track 1",.size = 3},
		    {.name = "Track 2",.size = 3}}
};

//Header and samples of a captured block with 2 tracks of 16 bits (1 kHz and 1.5 kHz at -6 dBFS)
static const uint8_t OB1_OUT_BLK_16[] = {
  0x30, 0xff, 0x00, 0x07, 0x49, 0x6c, 0xec, 0xdf, 0x5c, 0x2d, 0xf2, 0xd8,
  0xa4, 0x23, 0xa3, 0xd2, 0x8c, 0x18, 0x1a, 0xcd, 0x83, 0x0c, 0x71, 0xc8,
  0x00, 0x00, 0xbb, 0xc4, 0x7c, 0xf3, 0x08, 0xc2, 0x73, 0xe7, 0x65, 0xc0,
  0x5b, 0xdc, 0xd9, 0xbf, 0xa3, 0xd2, 0x65, 0xc0, 0xa8, 0xca, 0x08, 0xc2,
  0xbb, 0xc4, 0xbb, 0xc4, 0x14, 0xc1, 0x71, 0xc8, 0xd9, 0xbf, 0x1a, 0xcd,
  0x14, 0xc1, 0xa3, 0xd2, 0xbb, 0xc4, 0xf2, 0xd8, 0xa8, 0xca, 0xec, 0xdf,
  0xa3, 0xd2, 0x73, 0xe7, 0x5b, 0xdc, 0x65, 0xef, 0x73, 0xe7, 0xa0, 0xf7,
  0x7c, 0xf3, 0x00, 0x00, 0x00, 0x00, 0x5f, 0x08, 0x83, 0x0c, 0x9a, 0x10,
  0x8c, 0x18, 0x8c, 0x18, 0xa4, 0x23, 0x13, 0x20, 0x5c, 0x2d, 0x0d, 0x27,
  0x57, 0x35, 0x5c, 0x2d, 0x44, 0x3b, 0xe5, 0x32, 0xeb, 0x3e, 0x8e, 0x37,
  0x26, 0x40, 0x44, 0x3b, 0xeb, 0x3e, 0xf7, 0x3d, 0x44, 0x3b, 0x9a, 0x3f,
  0x57, 0x35, 0x26, 0x40, 0x5c, 0x2d, 0x9a, 0x3f, 0xa4, 0x23, 0xf7, 0x3d,
  0x8c, 0x18, 0x44, 0x3b, 0x83, 0x0c, 0x8e, 0x37, 0x00, 0x00, 0xe5, 0x32,
  0x7c, 0xf3, 0x5c, 0x2d, 0x73, 0xe7, 0x0d, 0x27, 0x5b, 0xdc, 0x13, 0x20,
  0xa3, 0xd2, 0x8c, 0x18, 0xa8, 0xca, 0x9a, 0x10, 0xbb, 0xc4, 0x5f, 0x08,
  0x14, 0xc1, 0x00, 0x00, 0xd9, 0xbf, 0xa0, 0xf7, 0x14, 0xc1, 0x65, 0xef,
  0xbb, 0xc4, 0x73, 0xe7, 0xa8, 0xca,
};

//Header and samples of a captured block with 2 tracks of 24 bits (1 kHz and 1.5 kHz at -6 dBFS)
static const uint8_t OB1_OUT_BLK_24[] = {
  0x30, 0xff, 0x00, 0x07, 0x44, 0x5b, 0x5e, 0xf2, 0xd8, 0xdb, 0xa8, 0xca,
  0x8c, 0xec, 0xdf, 0x41, 0xa3, 0xd2, 0x3a, 0x73, 0xe7, 0xec, 0x5b, 0xdc,
  0x70, 0x65, 0xef, 0x3a, 0x73, 0xe7, 0x61, 0xa0, 0xf7, 0x0d, 0x7c, 0xf3,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9e, 0x5f, 0x08, 0xf2, 0x83, 0x0c,
  0x8f, 0x9a, 0x10, 0xc5, 0x8c, 0x18, 0xc5, 0x8c, 0x18, 0x13, 0xa4, 0x23,
  0x73, 0x13, 0x20, 0xbe, 0x5c, 0x2d, 0xa1, 0x0d, 0x27, 0x24, 0x57, 0x35,
  0xbe, 0x5c, 0x2d, 0xc8, 0x44, 0x3b, 0x2a, 0xe5, 0x32, 0x57, 0xeb, 0x3e,
  0xa6, 0x8e, 0x37, 0xe6, 0x26, 0x40, 0xc8, 0x44, 0x3b, 0x57, 0xeb, 0x3e,
  0x4e, 0xf7, 0x3d, 0xc8, 0x44, 0x3b, 0x66, 0x9a, 0x3f, 0x24, 0x57, 0x35,
  0xe6, 0x26, 0x40, 0xbe, 0x5c, 0x2d, 0x66, 0x9a, 0x3f, 0x13, 0xa4, 0x23,
  0x4e, 0xf7, 0x3d, 0xc5, 0x8c, 0x18, 0xc8, 0x44, 0x3b, 0xf2, 0x83, 0x0c,
  0xa6, 0x8e, 0x37, 0x00, 0x00, 0x00, 0x2a, 0xe5, 0x32, 0x0d, 0x7c, 0xf3,
  0xbe, 0x5c, 0x2d, 0x3a, 0x73, 0xe7, 0xa1, 0x0d, 0x27, 0xec, 0x5b, 0xdc,
  0x73, 0x13, 0x20, 0x41, 0xa3, 0xd2, 0xc5, 0x8c, 0x18, 0xdb, 0xa8, 0xca,
  0x8f, 0x9a, 0x10, 0x37, 0xbb, 0xc4, 0x9e, 0x5f, 0x08, 0xa8, 0x14, 0xc1,
  0x00, 0x00, 0x00, 0x19, 0xd9, 0xbf, 0x61, 0xa0, 0xf7, 0xa8, 0x14, 0xc1,
  0x70, 0x65, 0xef, 0x37, 0xbb, 0xc4, 0x3a, 0x73, 0xe7, 0xdb, 0xa8, 0xca,
  0x8c, 0xec, 0xdf, 0x41, 0xa3, 0xd2, 0x5e, 0xf2, 0xd8, 0xec, 0x5b, 0xdc,
  0x41, 0xa3, 0xd2, 0x3a, 0x73, 0xe7, 0xd5, 0x1a, 0xcd, 0x0d, 0x7c, 0xf3,
  0x59, 0x71, 0xc8, 0x00, 0x00, 0x00, 0x37, 0xbb, 0xc4, 0xf2, 0x83, 0x0c,
  0xb1, 0x08, 0xc2, 0xc5, 0x8c, 0x18, 0x99, 0x65, 0xc0, 0x13, 0xa4, 0x23,
  0x19, 0xd9, 0xbf, 0xbe, 0x5c, 0x2d, 0x99, 0x65, 0xc0, 0x24, 0x57, 0x35,
  0xb1, 0x08, 0xc2, 0xc8, 0x44, 0x3b, 0x37, 0xbb, 0xc4, 0x57, 0xeb, 0x3e,
  0x59, 0x71, 0xc8, 0xe6, 0x26, 0x40, 0xd5, 0x1a, 0xcd, 0x57, 0xeb, 0x3e,
  0x41, 0xa3, 0xd2, 0xc8, 0x44, 0x3b,
};

//A captured block with 4 tracks of 24 bits
static const uint8_t OB1_IN_BLK_24[] = {
  0x30, 0x00, 0x01, 0x14, 0x91, 0x94, 0xfc, 0xfb, 0xfd, 0x3e, 0x1b, 0x09,
  0xa0, 0xb0, 0x05, 0x00, 0x00, 0x00, 0x96, 0x39, 0xfe, 0x5f, 0x30, 0x08,
  0xac, 0xdf, 0x05, 0x00, 0x00, 0x00, 0xcc, 0x90, 0xfe, 0x61, 0x24, 0x07,
  0x8e, 0x0d, 0x06, 0x00, 0x00, 0x00, 0x26, 0x06, 0xff, 0xbe, 0x03, 0x06,
  0x47, 0x3b, 0x06, 0x00, 0x00, 0x00, 0x93, 0x93, 0xff, 0x83, 0xd9, 0x04,
  0xb9, 0x67, 0x06, 0x00, 0x00, 0x00, 0xac, 0x3b, 0x00, 0x66, 0xb2, 0x03,
  0x63, 0x94, 0x06, 0x00, 0x00, 0x00, 0xcb, 0xf7, 0x00, 0x5f, 0x9d, 0x02,
  0x59, 0xc0, 0x06, 0x00, 0x00, 0x00, 0x64, 0xc8, 0x01, 0xbf, 0xa4, 0x01,
  0x50, 0xeb, 0x06, 0x00, 0x00, 0x00, 0x77, 0xa7, 0x02, 0x13, 0xd4, 0x00,
  0xde, 0x15, 0x07, 0x00, 0x00, 0x00, 0xde, 0x95, 0x03, 0xef, 0x33, 0x00,
  0x3b, 0x40, 0x07, 0x00, 0x00, 0x00, 0xf1, 0x8b, 0x04, 0x04, 0xcc, 0xff,
  0x97, 0x69, 0x07, 0x00, 0x00, 0x00, 0x80, 0x89, 0x05, 0xa9, 0xa2, 0xff,
  0x0c, 0x93, 0x07, 0x00, 0x00, 0x00, 0xe1, 0x87, 0x06, 0x3e, 0xb8, 0xff,
  0x27, 0xbb, 0x07, 0x00, 0x00, 0x00, 0x85, 0x85, 0x07, 0x9a, 0x0f, 0x00,
  0x55, 0xe3, 0x07, 0x00, 0x00, 0x00, 0xbf, 0x7c, 0x08, 0xf7, 0xa5, 0x00,
  0x3e, 0x0a, 0x08, 0x00, 0x00, 0x00, 0x65, 0x6b, 0x09, 0xa2, 0x76, 0x01,
  0x30, 0x31, 0x08, 0x00, 0x00, 0x00, 0x86, 0x4d, 0x0a, 0xce, 0x7a, 0x02,
  0x13, 0x58, 0x08, 0x00, 0x00, 0x00, 0x69, 0x1f, 0x0b, 0x4a, 0xaa, 0x03,
  0x46, 0x7e, 0x08, 0x00, 0x00, 0x00, 0xef, 0xdc, 0x0b, 0xc8, 0xf8, 0x04,
  0x96, 0xa3, 0x08, 0x00, 0x00, 0x00, 0x49, 0x86, 0x0c, 0x17, 0x5c, 0x06,
  0x6b, 0xc8, 0x08, 0x00, 0x00, 0x00, 0xd7, 0x15, 0x0d, 0x88, 0xc6, 0x07,
  0x36, 0xec, 0x08, 0x00, 0x00, 0x00, 0xcc, 0x8c, 0x0d, 0x88, 0x2c, 0x09,
  0x82, 0x10, 0x09, 0x00, 0x00, 0x00, 0xbe, 0xe6, 0x0d, 0x34, 0x80, 0x0a,
  0xbd, 0x33, 0x09, 0x00, 0x00, 0x00, 0x67, 0x23, 0x0e, 0xc7, 0xb6, 0x0b,
  0x37, 0x56, 0x09, 0x00, 0x00, 0x00, 0x17, 0x43, 0x0e, 0x63, 0xc4, 0x0c,
  0x7e, 0x78, 0x09, 0x00, 0x00, 0x00, 0xcf, 0x45, 0x0e, 0xc9, 0xa0, 0x0d,
  0x55, 0x9a, 0x09, 0x00, 0x00, 0x00, 0xff, 0x2a, 0x0e, 0xb9, 0x42, 0x0e,
  0x22, 0xbb, 0x09, 0x00, 0x00, 0x00, 0xfb, 0xf2, 0x0d, 0xc3, 0xa8, 0x0e,
  0xe9, 0xdb, 0x09, 0x00, 0x00, 0x00, 0xd1, 0xa1, 0x0d, 0x0d, 0xcb, 0x0e,
  0x18, 0xfc, 0x09, 0x00, 0x00, 0x00, 0x23, 0x36, 0x0d, 0xdf, 0xad, 0x0e,
  0x27, 0x1c, 0x0a, 0x00, 0x00, 0x00, 0xf0, 0xb4, 0x0c, 0x64, 0x4f, 0x0e,
  0x1f, 0x3b, 0x0a, 0x00, 0x00, 0x00, 0xce, 0x1e, 0x0c, 0xb0, 0xb6, 0x0d,
  0x0b, 0x5a, 0x0a, 0x00, 0x00, 0x00, 0x5e, 0x78, 0x0b, 0xd4, 0xe9, 0x0c,
  0xe8, 0x78, 0x0a, 0x00, 0x00, 0x00, 0xd7, 0xc3, 0x0a, 0x78, 0xf3, 0x0b,
  0x9b, 0x97, 0x0a, 0x00, 0x00, 0x00, 0x1f, 0xe9, 0x09, 0xa4, 0xbc, 0x0a,
  0xcf, 0x75, 0x0a, 0x00, 0x00, 0x00, 0x38, 0x72, 0x07, 0x29, 0xd4, 0x07,
  0x2e, 0xb6, 0x07, 0x00, 0x00, 0x00, 0xbe, 0x5d, 0x01, 0x62, 0x32, 0x01,
  0x41, 0x9d, 0x00, 0x00, 0x00, 0x00, 0x27, 0xbd, 0xfc, 0x3b, 0x11, 0xfc,
  0x71, 0x00, 0xfd, 0x00, 0x00, 0x00, 0x12, 0x9a, 0xfc, 0x5a, 0x94, 0xfb,
  0x3b, 0x33, 0xfe, 0x00, 0x00, 0x00, 0xa3, 0xd1, 0xfb, 0x64, 0x80, 0xfa,
  0xa4, 0x0c, 0xfe, 0x00, 0x00, 0x00, 0x95, 0x87, 0xfb, 0x80, 0x0a, 0xfa,
  0x87, 0xd9, 0xfe, 0x00, 0x00, 0x00, 0x4b, 0x09, 0xfb, 0x1a, 0x7f, 0xf9,
  0x2e, 0xea, 0xfe, 0x00, 0x00, 0x00, 0x70, 0xa0, 0xfa, 0x97, 0x2f, 0xf9,
  0xe3, 0x5f, 0xff, 0x00, 0x00, 0x00, 0xa3, 0x62, 0xfa, 0x7c, 0x34, 0xf9,
  0xd4, 0x9c, 0xff, 0x00, 0x00, 0x00, 0x2c, 0x17, 0xfa, 0xe5, 0x53, 0xf9,
  0x44, 0xf1, 0xff, 0x00, 0x00, 0x00, 0x45, 0x11, 0xfa, 0xd9, 0xe2, 0xf9,
  0xcc, 0x41, 0x00, 0x00, 0x00, 0x00, 0x2c, 0xf6, 0xf9, 0x61, 0x7d, 0xfa,
  0xe2, 0x85, 0x00, 0x00, 0x00, 0x00, 0x5c, 0x28, 0xfa, 0x6f, 0x86, 0xfb,
  0x32, 0xde, 0x00, 0x00, 0x00, 0x00,
};

//The first frames of OB1_IN_BLK_24
static const float OB1_IN_BLK_24_FRAMES[][4] = {
  {-0.015747547f, 0.071143866f, 0.044452667f, 0.000000000f},
  {-0.013867617f, 0.063976169f, 0.045888424f, 0.000000000f},
  {-0.011206150f, 0.055797696f, 0.047288656f, 0.000000000f},
  {-0.007624865f, 0.046989202f, 0.048684001f, 0.000000000f},
};

static void
copy_device_desc (struct ow_device_desc *dst,
		  const struct ow_device_desc *src)
{
  memcpy (dst, src, sizeof (struct ow_device_desc));
}

static void
print_blocks_v2 (struct ow_engine *engine)
{
  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      uint16_t debug_counter = 0;
      ow_engine_print_usb_block_v2 (engine, i, 0, &debug_counter);
    }
}

static void
test_get_frame_size_from_desc_tracks ()
{
  printf ("\n");

  size_t frame_size =
    ow_get_frame_size_from_desc_tracks (TESTDEV_DESC_SIZE.outputs,
					TESTDEV_DESC_SIZE.output_tracks);

  CU_ASSERT_EQUAL (frame_size, 2 * 4 + 2 * 3);
}

static void
test_ow_engine_get_valid_blocks_per_transfer ()
{
  printf ("\n");

  CU_ASSERT_EQUAL (ow_engine_get_valid_blocks_per_transfer (0, 10, 20, 15),
		   15);
  CU_ASSERT_EQUAL (ow_engine_get_valid_blocks_per_transfer (9, 10, 20, 15),
		   15);
  CU_ASSERT_EQUAL (ow_engine_get_valid_blocks_per_transfer (21, 10, 20, 15),
		   15);
  CU_ASSERT_EQUAL (ow_engine_get_valid_blocks_per_transfer (12, 10, 20, 15),
		   12);
}

static void
test_sizes_v2 ()
{
  struct ow_engine engine;

  printf ("\n");

  engine.device = malloc (sizeof (struct ow_device));
  copy_device_desc (&engine.device->desc, &TESTDEV_DESC_SIZE);
  engine.usb.audio_in_blk_len = 0;
  engine.usb.audio_out_blk_len = 0;
  ow_engine_init_mem_v2 (&engine, BLOCKS);

  printf ("\n");

  size_t o2h_frame_size = 2 * 4 + 2 * 3;
  size_t h2o_frame_size = 2 * 4;

  CU_ASSERT_EQUAL (engine.o2h_frame_size, o2h_frame_size);
  CU_ASSERT_EQUAL (engine.h2o_frame_size, h2o_frame_size);

  CU_ASSERT_EQUAL (engine.usb.audio_in_blk_len,
		   OB2_FRAMES_PER_BLOCK * o2h_frame_size + 32);
  CU_ASSERT_EQUAL (engine.usb.audio_out_blk_len,
		   OB2_FRAMES_PER_BLOCK * h2o_frame_size + 32);

  CU_ASSERT_EQUAL (engine.o2h_transfer_size,
		   BLOCKS * OB2_FRAMES_PER_BLOCK * 4 * OW_BYTES_PER_SAMPLE);
  CU_ASSERT_EQUAL (engine.h2o_transfer_size,
		   BLOCKS * OB2_FRAMES_PER_BLOCK * 2 * OW_BYTES_PER_SAMPLE);

  ow_engine_free_mem (&engine);
}

static void
test_usb_blocks_v2_error (const struct ow_device_desc *device_desc,
			  float max_error)
{
  float *a, *b;
  size_t blk_size;
  struct ow_engine engine;
  size_t frame_size;

  frame_size = ow_get_frame_size_from_desc_tracks (device_desc->inputs,
						   device_desc->input_tracks);

  blk_size = sizeof (struct ow_engine_usb_blk_v2) +
    OB2_FRAMES_PER_BLOCK * frame_size;

  printf ("\n");

  engine.device = malloc (sizeof (struct ow_device));
  copy_device_desc (&engine.device->desc, device_desc);
  engine.usb.audio_in_blk_len = 0;
  engine.usb.audio_out_blk_len = 0;
  ow_engine_init_mem_v2 (&engine, BLOCKS);

  CU_ASSERT_EQUAL (engine.usb.audio_out_blk_len, blk_size);
  CU_ASSERT_EQUAL (engine.usb.audio_in_blk_len, blk_size);

  a = engine.h2o_transfer_buf;
  for (int i = 0; i < engine.blocks_per_transfer; i++)
    {
      for (int j = 0; j < engine.frames_per_block; j++)
	{
	  for (int k = 0; k < engine.device->desc.outputs; k++)
	    {
	      *a = 1e-4 * (i + 1) * (k + 1);
	      a++;
	    }
	}
    }

  ow_engine_write_usb_output_blocks_v2 (&engine);

  for (int i = 0; i < BLOCKS; i++)
    {
      CU_ASSERT_EQUAL (0x7ff,
		       be16toh (GET_NTH_OUTPUT_USB_BLK_V2
				(&engine, i)->header));
      CU_ASSERT_EQUAL (i * 7,
		       be16toh (GET_NTH_OUTPUT_USB_BLK_V2
				(&engine, i)->frames));
    }

  print_blocks_v2 (&engine);

  memcpy (engine.usb.xfr_audio_in_data, engine.usb.xfr_audio_out_data,
	  engine.usb.xfr_audio_in_data_len);

  ow_engine_read_usb_input_blocks_v2 (&engine);

  a = engine.h2o_transfer_buf;
  b = engine.o2h_transfer_buf;
  for (int i = 0; i < engine.blocks_per_transfer; i++)
    {
      for (int j = 0; j < engine.frames_per_block; j++)
	{
	  for (int k = 0; k < engine.device->desc.outputs; k++)
	    {
	      float error = fabsf (*a - *b);
	      CU_ASSERT_TRUE (error < max_error);
	      printf ("%.10f =?= %.10f; error: %.10f\n", *a, *b, error);
	      a++;
	      b++;
	    }
	}
    }

  ow_engine_free_mem (&engine);
}

static void
test_usb_blocks_v2 ()
{
  test_usb_blocks_v2_error (&TESTDEV_DESC_V2, 1e-9);
}

static void
test_usb_blocks_v2_1 ()
{
  test_usb_blocks_v2_error (&TESTDEV_DESC_V2_1, 1e-6);
}

static void
init_engine_v1 (struct ow_engine *engine,
		const struct ow_device_desc *device_desc,
		unsigned int blocks_per_transfer)
{
  engine->device = malloc (sizeof (struct ow_device));
  copy_device_desc (&engine->device->desc, device_desc);
  engine->usb.device_handle = NULL;
  engine->usb.audio_in_blk_len = 0;
  engine->usb.audio_out_blk_len = 0;
  //On failure, the memory is already freed so the test can not continue.
  CU_ASSERT_EQUAL_FATAL (ow_engine_init_mem_v1 (engine, blocks_per_transfer),
			 OW_OK);
}

static void
free_engine_v1 (struct ow_engine *engine)
{
  for (int i = 0; i < OB1_TRANSFERS; i++)
    {
      libusb_free_transfer (engine->usb.xfr_audio_in_v1[i]);
      libusb_free_transfer (engine->usb.xfr_audio_out_v1[i]);
    }
  ow_engine_free_mem (engine);
  free (engine->device);
}

static void
set_audio_in_frames_v1 (struct ow_engine *engine, const uint8_t *frames,
			int n)
{
  engine->usb.audio_in_frames_read = 0;
  engine->usb.audio_in_frames_written = n;
  if (n)
    {
      memcpy (engine->usb.audio_in_frames, frames, n);
    }
}

static void
test_sizes_v1 ()
{
  struct ow_engine engine;

  printf ("\n");

  init_engine_v1 (&engine, &TESTDEV_DESC_V1_4_2, 0);

  CU_ASSERT_EQUAL (engine.blocks_per_transfer, OW1_DEFAULT_BLOCKS);
  CU_ASSERT_EQUAL (engine.frames_per_block, OB1_FRAMES_PER_BLOCK);
  CU_ASSERT_EQUAL (engine.frames_per_transfer,
		   OW1_DEFAULT_BLOCKS * OB1_FRAMES_PER_BLOCK);

  CU_ASSERT_EQUAL (engine.o2h_frame_size, 4 * 3);
  CU_ASSERT_EQUAL (engine.h2o_frame_size, 2 * 3);
  CU_ASSERT_EQUAL (engine.o2h_audio_frame_size, 4 * OW_BYTES_PER_SAMPLE);
  CU_ASSERT_EQUAL (engine.h2o_audio_frame_size, 2 * OW_BYTES_PER_SAMPLE);

  //These are the maximum packet sizes of the alternate settings 5 and 3.
  CU_ASSERT_EQUAL (engine.usb.audio_in_blk_len, 594);
  CU_ASSERT_EQUAL (engine.usb.audio_out_blk_len, 398);

  CU_ASSERT_EQUAL (engine.o2h_transfer_size,
		   OW1_DEFAULT_BLOCKS * OB1_MAX_FRAMES_PER_BLOCK * 4 *
		   OW_BYTES_PER_SAMPLE);
  CU_ASSERT_EQUAL (engine.h2o_transfer_size,
		   OW1_DEFAULT_BLOCKS * OB1_MAX_FRAMES_PER_BLOCK * 2 *
		   OW_BYTES_PER_SAMPLE);

  for (int i = 0; i < OB1_TRANSFERS; i++)
    {
      CU_ASSERT_EQUAL (engine.usb.xfr_audio_in_v1[i]->num_iso_packets,
		       OW1_DEFAULT_BLOCKS);
      CU_ASSERT_EQUAL (engine.usb.xfr_audio_in_v1[i]->
		       iso_packet_desc[0].length, 594);
      CU_ASSERT_EQUAL (engine.usb.xfr_audio_out_v1[i]->
		       iso_packet_desc[0].length, 398);
    }

  //The blocks of the queued transfers and one more transfer.
  CU_ASSERT_EQUAL (engine.usb.audio_in_frames_written -
		   engine.usb.audio_in_frames_read,
		   (OB1_TRANSFERS + 1) * OW1_DEFAULT_BLOCKS);

  free_engine_v1 (&engine);

  init_engine_v1 (&engine, &TESTDEV_DESC_V1_16, OW2_DEFAULT_BLOCKS);

  CU_ASSERT_EQUAL (engine.blocks_per_transfer, OW1_DEFAULT_BLOCKS);
  //These are the maximum packet sizes of the alternate settings 2.
  CU_ASSERT_EQUAL (engine.usb.audio_in_blk_len, 202);
  CU_ASSERT_EQUAL (engine.usb.audio_out_blk_len, 300);

  free_engine_v1 (&engine);
}

//On failure, the memory is freed and the engine can be discarded.
static void
test_init_mem_error ()
{
  struct ow_engine engine;

  printf ("\n");

  engine.device = malloc (sizeof (struct ow_device));

  copy_device_desc (&engine.device->desc, &TESTDEV_DESC_V1_4_2);
  engine.usb.device_handle = NULL;
  //This is smaller than the block size needed by the device description.
  engine.usb.audio_in_blk_len = 202;
  engine.usb.audio_out_blk_len = 0;
  CU_ASSERT_EQUAL (ow_engine_init_mem_v1 (&engine, 3),
		   OW_USB_UNEXPECTED_PACKET_SIZE);
  CU_ASSERT_PTR_NULL (engine.usb.xfr_audio_in_v1[0]);

  copy_device_desc (&engine.device->desc, &TESTDEV_DESC_V2);
  engine.usb.audio_in_blk_len = 1;
  engine.usb.audio_out_blk_len = 0;
  CU_ASSERT_EQUAL (ow_engine_init_mem_v2 (&engine, BLOCKS),
		   OW_USB_UNEXPECTED_PACKET_SIZE);

  free (engine.device);
}

static void
test_alt_setting_v1 ()
{
  int max;
  const uint16_t in_sizes[] = { 6, 202, 300, 398, 594, 790, 888 };
  const uint16_t out_sizes[] = { 104, 300, 398, 496, 692, 888, 986 };
  struct libusb_endpoint_descriptor in_eps[7], out_eps[7], midi_ep;
  struct libusb_interface_descriptor in_alts[8], out_alts[8], midi_alt;
  struct libusb_interface interfaces[3];
  struct libusb_config_descriptor config;

  printf ("\n");

  memset (in_alts, 0, sizeof (in_alts));
  memset (out_alts, 0, sizeof (out_alts));

  for (int i = 0; i < 8; i++)
    {
      in_alts[i].bInterfaceNumber = 0;
      in_alts[i].bAlternateSetting = i;
      out_alts[i].bInterfaceNumber = 1;
      out_alts[i].bAlternateSetting = i;
      if (i)
	{
	  in_eps[i - 1].bEndpointAddress = 0x83;
	  in_eps[i - 1].wMaxPacketSize = in_sizes[i - 1];
	  in_alts[i].bNumEndpoints = 1;
	  in_alts[i].endpoint = &in_eps[i - 1];
	  out_eps[i - 1].bEndpointAddress = 0x03;
	  out_eps[i - 1].wMaxPacketSize = out_sizes[i - 1];
	  out_alts[i].bNumEndpoints = 1;
	  out_alts[i].endpoint = &out_eps[i - 1];
	}
    }

  memset (&midi_alt, 0, sizeof (midi_alt));
  midi_ep.bEndpointAddress = 0x81;
  midi_ep.wMaxPacketSize = 64;
  midi_alt.bInterfaceNumber = 4;
  midi_alt.bNumEndpoints = 1;
  midi_alt.endpoint = &midi_ep;

  interfaces[0].altsetting = in_alts;
  interfaces[0].num_altsetting = 8;
  interfaces[1].altsetting = out_alts;
  interfaces[1].num_altsetting = 8;
  interfaces[2].altsetting = &midi_alt;
  interfaces[2].num_altsetting = 1;
  config.bNumInterfaces = 3;
  config.interface = interfaces;

  //2 tracks of 16 bits in both directions
  CU_ASSERT_EQUAL (ow_engine_get_alt_setting_v1 (&config, 0, 0x83,
						 OB1_USB_AUDIO_IN_BLK_LEN (4),
						 &max), 2);
  CU_ASSERT_EQUAL (max, 202);
  CU_ASSERT_EQUAL (ow_engine_get_alt_setting_v1 (&config, 1, 0x03,
						 OB1_USB_AUDIO_OUT_BLK_LEN
						 (4), &max), 2);
  CU_ASSERT_EQUAL (max, 300);

  //4 tracks of 24 bits from the device and 2 tracks of 24 bits to the device
  CU_ASSERT_EQUAL (ow_engine_get_alt_setting_v1 (&config, 0, 0x83,
						 OB1_USB_AUDIO_IN_BLK_LEN
						 (12), &max), 5);
  CU_ASSERT_EQUAL (max, 594);
  CU_ASSERT_EQUAL (ow_engine_get_alt_setting_v1 (&config, 1, 0x03,
						 OB1_USB_AUDIO_OUT_BLK_LEN
						 (6), &max), 3);
  CU_ASSERT_EQUAL (max, 398);

  //6 tracks of 24 bits
  CU_ASSERT_EQUAL (ow_engine_get_alt_setting_v1 (&config, 0, 0x83,
						 OB1_USB_AUDIO_IN_BLK_LEN
						 (18), &max), 7);
  CU_ASSERT_EQUAL (max, 888);

  //8 tracks of 24 bits do not fit
  CU_ASSERT_EQUAL (ow_engine_get_alt_setting_v1 (&config, 0, 0x83,
						 OB1_USB_AUDIO_IN_BLK_LEN
						 (24), &max), -1);
}

static void
test_check_layout_v1 ()
{
  struct ow_engine engine;
  const uint8_t o2h_map[] = { 0xa0, 0xa1, 0x80, 0x81, 0xff, 0xff, 0xff, 0xff };
  const uint8_t h2o_map[] = { 0x20, 0x21, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

  printf ("\n");

  init_engine_v1 (&engine, &TESTDEV_DESC_V1_4_2, 0);

  memcpy (engine.usb.layout_map[OB1_O2H], o2h_map, OB1_MAP_LEN);
  memcpy (engine.usb.layout_map[OB1_H2O], h2o_map, OB1_MAP_LEN);
  engine.usb.layout_depth[OB1_O2H] = 24;
  engine.usb.layout_depth[OB1_H2O] = 24;
  CU_ASSERT_EQUAL (ow_engine_check_layout_v1 (&engine), 0);

  engine.usb.layout_depth[OB1_H2O] = 16;
  CU_ASSERT_NOT_EQUAL (ow_engine_check_layout_v1 (&engine), 0);

  engine.usb.layout_depth[OB1_H2O] = 24;
  engine.usb.layout_map[OB1_O2H][3] = OB1_MAP_EMPTY;
  CU_ASSERT_NOT_EQUAL (ow_engine_check_layout_v1 (&engine), 0);

  free_engine_v1 (&engine);
}

static void
test_get_desc_layout_v1 ()
{
  struct ow_device_desc desc;
  uint8_t map[2][OB1_MAP_LEN];
  uint8_t depth[2];
  const uint8_t o2h_codes[] = { 0xa0, 0xa1, 0x80, 0x81 };
  const uint8_t h2o_codes[] = { 0x20, 0x21 };
  const uint8_t o2h_map[] = { 0xa0, 0xa1, 0x80, 0x81, 0xff, 0xff, 0xff, 0xff };
  const uint8_t h2o_map[] = { 0x20, 0x21, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

  printf ("\n");

  memcpy (&desc, &TESTDEV_DESC_V1_4_2, sizeof (struct ow_device_desc));
  for (int i = 0; i < OB_MAX_TRACKS; i++)
    {
      desc.output_tracks[i].code = OW_TRACK_CODE_NONE;
      desc.input_tracks[i].code = OW_TRACK_CODE_NONE;
    }
  CU_ASSERT_EQUAL (ow_engine_get_desc_layout_v1 (&desc, map, depth), 0);

  for (int i = 0; i < desc.outputs; i++)
    {
      desc.output_tracks[i].code = o2h_codes[i];
    }
  for (int i = 0; i < desc.inputs; i++)
    {
      desc.input_tracks[i].code = h2o_codes[i];
    }
  CU_ASSERT_EQUAL (ow_engine_get_desc_layout_v1 (&desc, map, depth), 1);
  CU_ASSERT_EQUAL (memcmp (map[OB1_O2H], o2h_map, OB1_MAP_LEN), 0);
  CU_ASSERT_EQUAL (memcmp (map[OB1_H2O], h2o_map, OB1_MAP_LEN), 0);
  CU_ASSERT_EQUAL (depth[OB1_O2H], 24);
  CU_ASSERT_EQUAL (depth[OB1_H2O], 24);

  for (int i = 0; i < desc.inputs; i++)
    {
      desc.input_tracks[i].size = 2;
    }
  CU_ASSERT_EQUAL (ow_engine_get_desc_layout_v1 (&desc, map, depth), 1);
  CU_ASSERT_EQUAL (depth[OB1_H2O], 16);

  desc.outputs = OB1_MAP_LEN + 1;
  for (int i = 0; i < desc.outputs; i++)
    {
      desc.output_tracks[i].size = 3;
      desc.output_tracks[i].code = 0x80;
    }
  CU_ASSERT_EQUAL (ow_engine_get_desc_layout_v1 (&desc, map, depth),
		   -EINVAL);
}

static void
test_usb_blocks_v1_error (const struct ow_device_desc *device_desc,
			  float max_error)
{
  float *a, *b;
  uint32_t frames;
  uint8_t *s, *end;
  uint32_t debug_counter;
  struct ow_engine engine;
  struct ow_engine_usb_blk_v1 *blk;
  struct libusb_transfer *in, *out;
  const uint8_t blk_frames[] = { 48, 47, 49 };
  //This makes the counter wrap around after the first block.
  const uint32_t counter = 0xffffffff - 47;
  const uint32_t counters[] = { 0, 47, 96 };

  printf ("\n");

  init_engine_v1 (&engine, device_desc, 3);
  in = engine.usb.xfr_audio_in_v1[0];
  out = engine.usb.xfr_audio_out_v1[0];

  set_audio_in_frames_v1 (&engine, blk_frames, 3);
  frames = ow_engine_set_usb_output_frames_v1 (&engine, out);
  CU_ASSERT_EQUAL (frames, 48 + 47 + 49);
  CU_ASSERT_EQUAL (engine.usb.audio_in_frames_read,
		   engine.usb.audio_in_frames_written);

  a = engine.h2o_transfer_buf;
  for (int i = 0; i < frames; i++)
    {
      for (int k = 0; k < engine.device->desc.inputs; k++)
	{
	  *a = (k % 2 ? -1e-3 : 1e-3) * (i + 1) * (k + 1);
	  a++;
	}
    }

  engine.usb.audio_frames_counter_v1 = counter;
  ow_engine_write_usb_output_blocks_v1 (&engine, out);

  for (int i = 0; i < 3; i++)
    {
      blk = (struct ow_engine_usb_blk_v1 *)
	libusb_get_iso_packet_buffer_simple (out, i);
      CU_ASSERT_EQUAL (blk->frames, blk_frames[i]);
      CU_ASSERT_EQUAL (blk->msg_offset, OB1_OUT_MSG_NONE);
      CU_ASSERT_EQUAL ((uint32_t) blk->counter[0] << 24 |
		       (uint32_t) blk->counter[1] << 16 |
		       (uint32_t) blk->counter[2] << 8 | blk->counter[3],
		       counters[i]);

      for (int j = 0; j < OB1_OUT_MSG_LEN; j++)
	{
	  CU_ASSERT_EQUAL (blk->data[j], 0);
	}

      s = blk->data + OB1_OUT_MSG_LEN + blk->frames * engine.h2o_frame_size;
      end = (uint8_t *) blk + out->iso_packet_desc[i].length;
      for (; s < end; s++)
	{
	  CU_ASSERT_EQUAL (*s, 0);
	}
    }

  debug_counter = 0;
  ow_engine_print_usb_block_v1 (&engine, out, 0, &debug_counter);

  //The device sends the samples right after the header.
  for (int i = 0; i < 3; i++)
    {
      struct ow_engine_usb_blk_v1 *src = (struct ow_engine_usb_blk_v1 *)
	libusb_get_iso_packet_buffer_simple (out, i);
      size_t len = src->frames * engine.o2h_frame_size;

      blk = (struct ow_engine_usb_blk_v1 *)
	libusb_get_iso_packet_buffer_simple (in, i);
      memcpy (blk, src, sizeof (struct ow_engine_usb_blk_v1));
      blk->msg_offset = 0;
      memcpy (blk->data, src->data + OB1_OUT_MSG_LEN, len);
      in->iso_packet_desc[i].actual_length =
	sizeof (struct ow_engine_usb_blk_v1) + len;
      in->iso_packet_desc[i].status = LIBUSB_TRANSFER_COMPLETED;
    }

  set_audio_in_frames_v1 (&engine, NULL, 0);
  frames = ow_engine_read_usb_input_blocks_v1 (&engine, in);
  CU_ASSERT_EQUAL (frames, 48 + 47 + 49);
  CU_ASSERT_EQUAL (engine.usb.audio_in_errors, 0);
  CU_ASSERT_EQUAL (engine.usb.audio_in_frames_counter_v1, counters[2]);
  CU_ASSERT_EQUAL (engine.usb.audio_in_frames_written, 3);
  CU_ASSERT_EQUAL (memcmp (engine.usb.audio_in_frames, blk_frames, 3), 0);

  debug_counter = 0;
  ow_engine_print_usb_block_v1 (&engine, in, 1, &debug_counter);

  a = engine.h2o_transfer_buf;
  b = engine.o2h_transfer_buf;
  for (int i = 0; i < frames * engine.device->desc.outputs; i++, a++, b++)
    {
      float error = fabsf (*a - *b);
      CU_ASSERT_TRUE (error < max_error);
    }

  //Invalid blocks are replaced by silence.
  in->iso_packet_desc[1].status = LIBUSB_TRANSFER_OVERFLOW;
  in->iso_packet_desc[2].actual_length--;

  set_audio_in_frames_v1 (&engine, NULL, 0);
  frames = ow_engine_read_usb_input_blocks_v1 (&engine, in);
  CU_ASSERT_EQUAL (frames, 3 * OB1_FRAMES_PER_BLOCK);
  CU_ASSERT_EQUAL (engine.usb.audio_in_errors, 2);
  CU_ASSERT_EQUAL (engine.usb.audio_in_frames[0], 48);
  CU_ASSERT_EQUAL (engine.usb.audio_in_frames[1], OB1_FRAMES_PER_BLOCK);
  CU_ASSERT_EQUAL (engine.usb.audio_in_frames[2], OB1_FRAMES_PER_BLOCK);

  b = engine.o2h_transfer_buf + 48 * engine.device->desc.outputs;
  for (int i = 0; i < 2 * OB1_FRAMES_PER_BLOCK * engine.device->desc.outputs;
       i++, b++)
    {
      CU_ASSERT_EQUAL (*b, 0);
    }

  //Empty blocks, sent at the start of a stream, are not errors.
  in->iso_packet_desc[0].actual_length = 0;

  set_audio_in_frames_v1 (&engine, NULL, 0);
  frames = ow_engine_read_usb_input_blocks_v1 (&engine, in);
  CU_ASSERT_EQUAL (frames, 3 * OB1_FRAMES_PER_BLOCK);
  CU_ASSERT_EQUAL (engine.usb.audio_in_errors, 4);
  CU_ASSERT_EQUAL (engine.usb.audio_in_frames[0], OB1_FRAMES_PER_BLOCK);

  b = engine.o2h_transfer_buf;
  for (int i = 0; i < OB1_FRAMES_PER_BLOCK * engine.device->desc.outputs;
       i++, b++)
    {
      CU_ASSERT_EQUAL (*b, 0);
    }

  free_engine_v1 (&engine);
}

static void
test_usb_blocks_v1_16 ()
{
  test_usb_blocks_v1_error (&TESTDEV_DESC_V1_16, 1e-4);
}

static void
test_usb_blocks_v1_24 ()
{
  test_usb_blocks_v1_error (&TESTDEV_DESC_V1_24, 1e-6);
}

static void
test_captured_usb_block_v1_out (const struct ow_device_desc *device_desc,
				const uint8_t *data, size_t len)
{
  int size;
  uint8_t *expected;
  const uint8_t *s;
  struct ow_engine engine;
  struct libusb_transfer *out;
  const uint8_t blk_frames[] = { 48 };
  struct ow_engine_usb_blk_v1 *blk = (struct ow_engine_usb_blk_v1 *) data;

  printf ("\n");

  init_engine_v1 (&engine, device_desc, 1);
  out = engine.usb.xfr_audio_out_v1[0];
  size = device_desc->input_tracks[0].size;

  //The samples of the captured block are converted to floats.
  s = blk->data;
  for (int i = 0; i < (len - sizeof (struct ow_engine_usb_blk_v1)) / size;
       i++, s += size)
    {
      int32_t v = size == 2 ?
	(int32_t) ((uint32_t) s[0] << 16 | (uint32_t) s[1] << 24) :
	(int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 |
		   (uint32_t) s[2] << 24);
      engine.h2o_transfer_buf[i] = v / (float) INT32_MAX;
    }

  set_audio_in_frames_v1 (&engine, blk_frames, 1);
  ow_engine_set_usb_output_frames_v1 (&engine, out);
  engine.usb.audio_frames_counter_v1 =
    ((uint32_t) blk->counter[0] << 24 | (uint32_t) blk->counter[1] << 16 |
     (uint32_t) blk->counter[2] << 8 | blk->counter[3]) - 48;
  ow_engine_write_usb_output_blocks_v1 (&engine, out);

  //The header, the empty message region, the samples and the padding.
  expected = calloc (1, engine.usb.audio_out_blk_len);
  memcpy (expected, data, sizeof (struct ow_engine_usb_blk_v1));
  memcpy (expected + sizeof (struct ow_engine_usb_blk_v1) + OB1_OUT_MSG_LEN,
	  blk->data, len - sizeof (struct ow_engine_usb_blk_v1));

  CU_ASSERT_EQUAL (out->iso_packet_desc[0].length,
		   engine.usb.audio_out_blk_len);
  CU_ASSERT_EQUAL (memcmp (libusb_get_iso_packet_buffer_simple (out, 0),
			   expected, engine.usb.audio_out_blk_len), 0);

  free (expected);
  free_engine_v1 (&engine);
}

static void
test_captured_usb_blocks_v1 ()
{
  struct ow_engine engine;
  struct libusb_transfer *in;
  float *f;

  test_captured_usb_block_v1_out (&TESTDEV_DESC_V1_16, OB1_OUT_BLK_16,
				  sizeof (OB1_OUT_BLK_16));
  test_captured_usb_block_v1_out (&TESTDEV_DESC_V1_4_2, OB1_OUT_BLK_24,
				  sizeof (OB1_OUT_BLK_24));

  init_engine_v1 (&engine, &TESTDEV_DESC_V1_4_2, 1);
  in = engine.usb.xfr_audio_in_v1[0];

  memcpy (libusb_get_iso_packet_buffer_simple (in, 0), OB1_IN_BLK_24,
	  sizeof (OB1_IN_BLK_24));
  in->iso_packet_desc[0].actual_length = sizeof (OB1_IN_BLK_24);
  in->iso_packet_desc[0].status = LIBUSB_TRANSFER_COMPLETED;

  set_audio_in_frames_v1 (&engine, NULL, 0);
  CU_ASSERT_EQUAL (ow_engine_read_usb_input_blocks_v1 (&engine, in), 48);
  CU_ASSERT_EQUAL (engine.usb.audio_in_errors, 0);
  CU_ASSERT_EQUAL (engine.usb.audio_in_frames_counter_v1, 0x01149194);

  f = engine.o2h_transfer_buf;
  for (int i = 0; i < 4; i++)
    {
      for (int j = 0; j < 4; j++, f++)
	{
	  CU_ASSERT_DOUBLE_EQUAL (*f, OB1_IN_BLK_24_FRAMES[i][j], 1e-7);
	}
    }

  free_engine_v1 (&engine);
}

static void
test_jack_buffers ()
{
  jack_default_audio_sample_t *jack_input[TRACKS];
  jack_default_audio_sample_t *jack_output[TRACKS];
  float input[TRACKS * NFRAMES];
  float output[TRACKS * NFRAMES];
  struct ow_engine engine;

  printf ("\n");

  engine.device = malloc (sizeof (struct ow_device));
  copy_device_desc (&engine.device->desc, &TESTDEV_DESC_V2);

  for (int i = 0; i < TRACKS; i++)
    {
      jack_input[i] = malloc (sizeof (jack_default_audio_sample_t) * NFRAMES);
      jack_output[i] =
	malloc (sizeof (jack_default_audio_sample_t) * NFRAMES);
      for (int j = 0; j < NFRAMES; j++)
	{
	  jack_input[i][j] = 1e-8 * (i + 1) * (j + 1);
	}
    }

  jclient_copy_j2o_audio (output, NFRAMES, jack_input, &engine.device->desc);

  memcpy (input, output,
	  TRACKS * NFRAMES * sizeof (jack_default_audio_sample_t));

  jclient_copy_o2j_audio (input, NFRAMES, jack_output, &engine.device->desc);

  for (int i = 0; i < TRACKS; i++)
    {
      for (int j = 0; j < NFRAMES; j++)
	{
	  printf ("%.10f =?= %.10f\n", jack_output[i][j], jack_input[i][j]);
	  CU_ASSERT_EQUAL (jack_output[i][j], jack_input[i][j]);
	}

      free (jack_input[i]);
      free (jack_output[i]);
    }

  free (engine.device);
}

static void
test_get_bus_address_from_str ()
{
  uint8_t bus, address;

  printf ("\n");

  CU_ASSERT_EQUAL (get_bus_address_from_str ("a", &bus, &address), -EINVAL);
  CU_ASSERT_EQUAL (get_bus_address_from_str ("a,", &bus, &address), -EINVAL);
  CU_ASSERT_EQUAL (get_bus_address_from_str ("a,b", &bus, &address), -EINVAL);
  CU_ASSERT_EQUAL (get_bus_address_from_str ("1,b", &bus, &address), -EINVAL);
  CU_ASSERT_EQUAL (get_bus_address_from_str ("a,2", &bus, &address), -EINVAL);
  CU_ASSERT_EQUAL (get_bus_address_from_str ("1,2", &bus, &address), 0);
  CU_ASSERT_EQUAL (bus, 1);
  CU_ASSERT_EQUAL (address, 2);
}

static void
test_state_parser ()
{
  char *message;
  guint devices;
  OverwitchDevice *device;
  guint32 samplerate, buffer_size;
  JsonReader *reader;
  JsonBuilder *builder;
  struct ow_engine engine;
  struct ow_resampler_state state;

  engine.device = malloc (sizeof (struct ow_device));
  copy_device_desc (&engine.device->desc, &TESTDEV_DESC_V2);

  builder = message_state_builder_start ();

  state.t_latency_o2h = 2;
  state.t_latency_o2h_max = 3;
  state.t_latency_o2h_min = 1;
  state.t_latency_h2o = 2;
  state.t_latency_h2o_max = 3;
  state.t_latency_h2o_min = 1;
  state.ratio_o2h = 0.9;
  state.ratio_h2o = 1.0 / 0.9;
  state.status = OW_RESAMPLER_STATUS_RUN;

  message_state_builder_add_device (builder, 0, "name 1", engine.device,
				    &state);
  message_state_builder_add_device (builder, 1, "name 2", engine.device,
				    &state);
  message = message_state_builder_end (builder, 1, 2);


  reader = message_state_reader_start (message, &devices);
  CU_ASSERT_TRUE (reader != NULL);
  CU_ASSERT_EQUAL (devices, 2);

  for (guint i = 0; i < devices; i++)
    {
      device = message_state_reader_get_device (reader, i);
      g_object_unref (device);
    }

  message_state_reader_end (reader, &samplerate, &buffer_size);

  CU_ASSERT_EQUAL (samplerate, 1);
  CU_ASSERT_EQUAL (buffer_size, 2);

  free (message);
  free (engine.device);
}

int
main (int argc, char *argv[])
{
  int err = 0;

  debug_level = 3;

  if (CU_initialize_registry () != CUE_SUCCESS)
    {
      goto cleanup;
    }
  CU_pSuite suite = CU_add_suite ("Overwitch USB blocks tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_get_frame_size_from_desc_tracks",
		    test_get_frame_size_from_desc_tracks))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_ow_engine_get_valid_blocks_per_transfer",
		    test_ow_engine_get_valid_blocks_per_transfer))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_sizes_v2", test_sizes_v2))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_usb_blocks_v2", test_usb_blocks_v2))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_usb_blocks_v2.1", test_usb_blocks_v2_1))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_sizes_v1", test_sizes_v1))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_init_mem_error", test_init_mem_error))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_alt_setting_v1", test_alt_setting_v1))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_check_layout_v1", test_check_layout_v1))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_get_desc_layout_v1",
		    test_get_desc_layout_v1))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_usb_blocks_v1_16", test_usb_blocks_v1_16))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_usb_blocks_v1_24", test_usb_blocks_v1_24))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_captured_usb_blocks_v1",
		    test_captured_usb_blocks_v1))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_jack_buffers", test_jack_buffers))
    {
      goto cleanup;
    }

  if (!CU_add_test
      (suite, "get_bus_address_from_str", test_get_bus_address_from_str))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "state_parser", test_state_parser))
    {
      goto cleanup;
    }

  CU_basic_set_mode (CU_BRM_VERBOSE);

  CU_basic_run_tests ();
  err = CU_get_number_of_tests_failed ();

cleanup:
  CU_cleanup_registry ();
  return err || CU_get_error ();
}
