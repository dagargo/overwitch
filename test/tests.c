#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/jclient.h"
#include "../src/engine.h"
#include "../src/common.h"
#include "../src/message.h"

#define BLOCKS 4
#define TRACKS 6
#define NFRAMES 64

static const struct ow_device_desc TESTDEV_DESC_T2 = {
  .pid = 0,
  .type = OW_DEVICE_TYPE_2,
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

static const struct ow_device_desc TESTDEV_DESC_T3 = {
  .pid = 0,
  .type = OW_DEVICE_TYPE_3,
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
  .type = OW_DEVICE_TYPE_1,
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

static void
ow_engine_print_blocks (struct ow_engine *engine, uint8_t *blks, size_t blk_len)
{
  int32_t v;
  uint8_t *s;
  struct ow_engine_usb_blk *blk;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_USB_BLK (blks, blk_len, i);
      printf ("Block %d\n", i);
      printf ("0x%04x | 0x%04x\n", be16toh (blk->header),
	      be16toh (blk->frames));
      s = (uint8_t *) blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  if (engine->device->desc.type == OW_DEVICE_TYPE_2)
	    {
	      for (int k = 0; k < engine->device->desc.outputs; k++)
		{
		  v = be32toh (*((int32_t *) s));
		  printf ("Frame %2d, track %2d: %d\n", j, k, v);
		  s += sizeof (int32_t);
		}
	    }
	  else if (engine->device->desc.type == OW_DEVICE_TYPE_3)
	    {
	      struct ow_device_track *track =
		engine->device->desc.output_tracks;
	      for (int k = 0; k < engine->device->desc.outputs; k++)
		{
		  uint8_t *dst;
		  if (track->size == 4)
		    {
		      dst = (uint8_t *) &v;
		      memcpy (dst, s, track->size);
		    }
		  else
		    {
		      dst = &((uint8_t *) &v)[1];
		      memcpy (dst, s, track->size);
		    }
		  v = be32toh (v);
		  v <<= 8;
		  printf ("Frame %2d, track %2d: %d\n", j, k, v);
		  s += track->size;
		  track++;
		}
	    }
	}
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
test_sizes ()
{
  struct ow_engine engine;

  printf ("\n");

  engine.device = malloc (sizeof (struct ow_device));
  ow_copy_device_desc (&engine.device->desc, &TESTDEV_DESC_SIZE);
  engine.usb.audio_in_blk_len = 0;
  engine.usb.audio_out_blk_len = 0;
  ow_engine_init_mem (&engine, BLOCKS);

  printf ("\n");

  size_t o2h_frame_size = 2 * 4 + 2 * 3;
  size_t h2o_frame_size = 2 * 4;

  CU_ASSERT_EQUAL (engine.o2h_frame_size, o2h_frame_size);
  CU_ASSERT_EQUAL (engine.h2o_frame_size, h2o_frame_size);

  CU_ASSERT_EQUAL (engine.usb.audio_in_blk_len,
		   OB_FRAMES_PER_BLOCK * o2h_frame_size + 32);
  CU_ASSERT_EQUAL (engine.usb.audio_out_blk_len,
		   OB_FRAMES_PER_BLOCK * h2o_frame_size + 32);

  CU_ASSERT_EQUAL (engine.o2h_transfer_size,
		   BLOCKS * OB_FRAMES_PER_BLOCK * 4 * OW_BYTES_PER_SAMPLE);
  CU_ASSERT_EQUAL (engine.h2o_transfer_size,
		   BLOCKS * OB_FRAMES_PER_BLOCK * 2 * OW_BYTES_PER_SAMPLE);

  ow_engine_free_mem (&engine);
}

static void
test_usb_blocks (const struct ow_device_desc *device_desc, float max_error)
{
  float *a, *b;
  size_t blk_size;
  struct ow_engine engine;
  size_t frame_size;

  frame_size = ow_get_frame_size_from_desc_tracks (device_desc->inputs,
						   device_desc->input_tracks);

  blk_size = sizeof (struct ow_engine_usb_blk) +
    OB_FRAMES_PER_BLOCK * frame_size;

  printf ("\n");

  engine.device = malloc (sizeof (struct ow_device));
  ow_copy_device_desc (&engine.device->desc, device_desc);
  engine.usb.audio_in_blk_len = 0;
  engine.usb.audio_out_blk_len = 0;
  ow_engine_init_mem (&engine, BLOCKS);

  CU_ASSERT_EQUAL (engine.usb.audio_out_blk_len, blk_size);
  CU_ASSERT_EQUAL (engine.usb.audio_in_blk_len, blk_size);

  a = engine.h2o_transfer_buf;
  for (int i = 0; i < BLOCKS; i++)
    {
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine.device->desc.outputs; k++)
	    {
	      *a = 1e-4 * (i + 1) * (k + 1);
	      a++;
	    }
	}
    }

  ow_engine_write_usb_output_blocks (&engine);

  for (int i = 0; i < BLOCKS; i++)
    {
      CU_ASSERT_EQUAL (0x7ff,
		       be16toh (GET_NTH_OUTPUT_USB_BLK (&engine, i)->header));
      CU_ASSERT_EQUAL (i * 7,
		       be16toh (GET_NTH_OUTPUT_USB_BLK (&engine, i)->frames));
    }

  ow_engine_print_blocks (&engine, engine.usb.xfr_audio_out_data,
			  engine.usb.audio_out_blk_len);

  memcpy (engine.usb.xfr_audio_in_data, engine.usb.xfr_audio_out_data,
	  engine.usb.xfr_audio_in_data_len);

  ow_engine_read_usb_input_blocks (&engine);

  a = engine.h2o_transfer_buf;
  b = engine.o2h_transfer_buf;
  for (int i = 0; i < BLOCKS; i++)
    {
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
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
test_usb_blocks_t1 ()
{
  test_usb_blocks (&TESTDEV_DESC_T2, 1e-9);
}

static void
test_usb_blocks_t2 ()
{
  test_usb_blocks (&TESTDEV_DESC_T3, 1e-6);
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
  ow_copy_device_desc (&engine.device->desc, &TESTDEV_DESC_T2);

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
  double target_delay_ms;
  JsonReader *reader;
  JsonBuilder *builder;
  struct ow_engine engine;
  struct ow_resampler_state state;

  engine.device = malloc (sizeof (struct ow_device));
  ow_copy_device_desc (&engine.device->desc, &TESTDEV_DESC_T2);

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
  message = message_state_builder_end (builder, 1, 2, 3);


  reader = message_state_reader_start (message, &devices);
  CU_ASSERT_TRUE (reader != NULL);
  CU_ASSERT_EQUAL (devices, 2);

  for (guint i = 0; i < devices; i++)
    {
      device = message_state_reader_get_device (reader, i);
      g_object_unref (device);
    }

  message_state_reader_end (reader, &samplerate, &buffer_size,
			    &target_delay_ms);

  CU_ASSERT_EQUAL (samplerate, 1);
  CU_ASSERT_EQUAL (buffer_size, 2);
  CU_ASSERT_EQUAL (target_delay_ms, 3);

  free (message);
  free (engine.device);
}

int
main (int argc, char *argv[])
{
  int err = 0;

  debug_level = 2;

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

  if (!CU_add_test (suite, "test_sizes", test_sizes))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_usb_blocks_t1", test_usb_blocks_t1))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_usb_blocks_t2", test_usb_blocks_t2))
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
