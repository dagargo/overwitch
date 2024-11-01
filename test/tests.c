#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/jclient.h"
#include "../src/engine.h"

#define BLOCKS 4
#define TRACKS 6
#define NFRAMES 64

static const struct ow_device_desc TESTDEV_DESC_V1 = {
  .pid = 0,
  .protocol = 1,
  .name = "Test",
  .inputs = TRACKS,
  .outputs = TRACKS,
  .input_track_names = {"T1", "T2", "T3", "T4", "T5", "T6"},
  .custom_input_track_sizes = {4, 4, 4, 4, 4, 4},
  .output_track_names = {"T1", "T2", "T3", "T4", "T5", "T6"},
  .custom_output_track_sizes = {4, 4, 4, 4, 4, 4},
};

static const struct ow_device_desc TESTDEV_DESC_V2 = {
  .pid = 0,
  .protocol = 2,
  .name = "Test",
  .inputs = TRACKS,
  .outputs = TRACKS,
  .input_track_names = {"T1", "T2", "T3", "T4", "T5", "T6"},
  .custom_input_track_sizes = {4, 4, 3, 3, 3, 3},
  .output_track_names = {"T1", "T2", "T3", "T4", "T5", "T6"},
  .custom_output_track_sizes = {4, 4, 3, 3, 3, 3}
};

static void
ow_engine_print_blocks (struct ow_engine *engine, char *blks, size_t blk_len)
{
  int32_t v;
  unsigned char *s;
  struct ow_engine_usb_blk *blk;

  for (int i = 0; i < engine->blocks_per_transfer; i++)
    {
      blk = GET_NTH_USB_BLK (blks, blk_len, i);
      printf ("Block %d\n", i);
      printf ("0x%04x | 0x%04x\n", be16toh (blk->header),
	      be16toh (blk->frames));
      s = (unsigned char *) blk->data;
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  if (engine->device_desc.protocol == OW_ENGINE_PROTOCOL_V1)
	    {
	      for (int k = 0; k < engine->device_desc.outputs; k++)
		{
		  v = be32toh (*((int32_t *) s));
		  printf ("Frame %2d, track %2d: %d\n", j, k, v);
		  s += sizeof (int32_t);
		}
	    }
	  else
	    {
	      int *size = engine->device_desc.custom_output_track_sizes;
	      for (int k = 0; k < engine->device_desc.outputs; k++)
		{
		  unsigned char *dst;
		  if (*size == 4)
		    {
		      dst = (unsigned char *) &v;
		      memcpy (dst, s, *size);
		    }
		  else
		    {
		      dst = &((unsigned char *) &v)[1];
		      memcpy (dst, s, *size);
		    }
		  v = be32toh (v);
		  v <<= 8;
		  printf ("Frame %2d, track %2d: %d\n", j, k, v);
		  s += *size;
		  size++;
		}

	    }
	}
    }
}

static void
test_sizes ()
{
  struct ow_engine engine;

  printf ("\n");

  ow_copy_device_desc_static (&engine.device_desc, &TESTDEV_DESC_V1);
  engine.usb.audio_in_blk_len = 0;
  engine.usb.audio_out_blk_len = 0;
  ow_engine_init_mem (&engine, BLOCKS);

  printf ("\n");

  CU_ASSERT_EQUAL (engine.usb.audio_out_blk_len,
		   TRACKS * OB_FRAMES_PER_BLOCK * OW_BYTES_PER_SAMPLE + 32);
  CU_ASSERT_EQUAL (engine.usb.audio_in_blk_len,
		   TRACKS * OB_FRAMES_PER_BLOCK * OW_BYTES_PER_SAMPLE + 32);

  ow_engine_free_mem (&engine);
}

static void
test_usb_blocks (const struct ow_device_desc *device_desc, size_t blk_size,
		 float max_error)
{
  float *a, *b;
  struct ow_engine engine;

  printf ("\n");

  ow_copy_device_desc_static (&engine.device_desc, device_desc);
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
	  for (int k = 0; k < engine.device_desc.outputs; k++)
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
	  for (int k = 0; k < engine.device_desc.outputs; k++)
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
test_usb_blocks_v1 ()
{
  size_t blk_size = sizeof (struct ow_engine_usb_blk) +
    OB_FRAMES_PER_BLOCK * TRACKS * sizeof (float);

  test_usb_blocks (&TESTDEV_DESC_V1, blk_size, 1e-9);
}

static void
test_usb_blocks_v2 ()
{
  size_t frame_size = 0;
  for (int i = 0; i < TESTDEV_DESC_V2.inputs; i++)
    {
      frame_size += TESTDEV_DESC_V2.custom_input_track_sizes[i];
    }
  size_t blk_size = sizeof (struct ow_engine_usb_blk) +
    OB_FRAMES_PER_BLOCK * frame_size;

  test_usb_blocks (&TESTDEV_DESC_V2, blk_size, 1e-6);
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

  ow_copy_device_desc_static (&engine.device_desc, &TESTDEV_DESC_V1);

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

  jclient_copy_j2o_audio (output, NFRAMES, jack_input, &engine.device_desc);

  memcpy (input, output,
	  TRACKS * NFRAMES * sizeof (jack_default_audio_sample_t));

  jclient_copy_o2j_audio (input, NFRAMES, jack_output, &engine.device_desc);

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
}

int
main (int argc, char *argv[])
{
  int err;

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

  if (!CU_add_test (suite, "test_sizes", test_sizes))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_usb_blocks_v1", test_usb_blocks_v1))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_usb_blocks_v2", test_usb_blocks_v2))
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_jack_buffers", test_jack_buffers))
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
