#include <string.h>
#include <math.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "../src/jclient.h"
#include "../src/engine.h"

#define OW_CONV_SCALE_32 (1.0f / (float) INT_MAX)
#define BLOCKS 4
#define TRACKS 6
#define NFRAMES 64

static const struct ow_device_desc TESTDEV_DESC = {
  .pid = 0,
  .name = "Test",
  .inputs = TRACKS,
  .outputs = TRACKS,
  .input_track_names = {"T1", "T2", "T3", "T4", "T5", "T6"},
  .output_track_names = {"T1", "T2", "T3", "T4", "T5", "T6"},
  .output_track_scales =
    {OW_CONV_SCALE_32, OW_CONV_SCALE_32, OW_CONV_SCALE_32, OW_CONV_SCALE_32,
     OW_CONV_SCALE_32, OW_CONV_SCALE_32}
};

void
test_usb_blocks ()
{
  float *a, *b;
  size_t blk_size;
  struct ow_engine engine;

  printf ("\n");

  engine.device_desc = &TESTDEV_DESC;
  ow_engine_init_mem (&engine, BLOCKS);

  blk_size =
    sizeof (struct ow_engine_usb_blk) +
    OB_FRAMES_PER_BLOCK * TRACKS * sizeof (float);

  CU_ASSERT_EQUAL (engine.usb.data_out_blk_len, blk_size);
  CU_ASSERT_EQUAL (engine.usb.data_in_blk_len, blk_size);

  a = engine.p2o_transfer_buf;
  for (int i = 0; i < BLOCKS; i++)
    {
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine.device_desc->outputs; k++)
	    {
	      *a = 1e-8 * (i + 1) * (k + 1);
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

  ow_engine_print_blocks (&engine, engine.usb.data_out,
			  engine.usb.data_out_blk_len);

  memcpy (engine.usb.data_in, engine.usb.data_out, engine.usb.data_in_len);

  ow_engine_read_usb_input_blocks (&engine);

  a = engine.p2o_transfer_buf;
  b = engine.o2p_transfer_buf;
  for (int i = 0; i < BLOCKS; i++)
    {
      for (int j = 0; j < OB_FRAMES_PER_BLOCK; j++)
	{
	  for (int k = 0; k < engine.device_desc->outputs; k++)
	    {
	      float error = fabsf (*a - *b);
	      CU_ASSERT_TRUE (error < 1e-8);
	      printf ("%.10f =?= %.10f; error: %.10f\n", *a, *b, error);
	      a++;
	      b++;
	    }
	}
    }

  ow_engine_free_mem (&engine);
}

void
test_jack_buffers ()
{
  jack_default_audio_sample_t *jack_input[TRACKS];
  jack_default_audio_sample_t *jack_output[TRACKS];
  float input[TRACKS * NFRAMES];
  float output[TRACKS * NFRAMES];

  printf ("\n");

  for (int i = 0; i < TRACKS; i++)
    {
      jack_input[i] = malloc(sizeof(jack_default_audio_sample_t) * NFRAMES);
      jack_output[i] = malloc(sizeof(jack_default_audio_sample_t) * NFRAMES);
      for (int j = 0; j < NFRAMES; j++)
	{
	  jack_input[i][j] = 1e-8 * (i + 1) * (j + 1);
	}
    }

  jclient_copy_j2o_audio (output, NFRAMES, jack_input, &TESTDEV_DESC);

  memcpy (input, output,
	  TRACKS * NFRAMES * sizeof (jack_default_audio_sample_t));

  jclient_copy_o2j_audio (input, NFRAMES, jack_output, &TESTDEV_DESC);

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

  if (CU_initialize_registry () != CUE_SUCCESS)
    {
      goto cleanup;
    }
  CU_pSuite suite = CU_add_suite ("Overwitch USB blocks tests", 0, 0);
  if (!suite)
    {
      goto cleanup;
    }

  if (!CU_add_test (suite, "test_usb_blocks", test_usb_blocks))
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
