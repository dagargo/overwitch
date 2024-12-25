/*
 *   state.c
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
 *
 *   This file is part of Overwitch.
 *
 *   Overwitch is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Overwitch is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Overwitch. If not, see <http://www.gnu.org/licenses/>.
 */

#include "message.h"

#define STATE_DEVICES "devices"
#define STATE_DEVICE_ID "id"
#define STATE_DEVICE_NAME "name"
#define STATE_DEVICE_DEVICE "device"
#define STATE_DEVICE_BUS "bus"
#define STATE_DEVICE_ADDRESS "address"
#define STATE_DEVICE_LATENCY_O2H "latencyO2H"
#define STATE_DEVICE_LATENCY_H2O "latencyH2O"
#define STATE_DEVICE_RATIO_O2H "ratioO2H"
#define STATE_DEVICE_RATIO_H2O "ratioH2O"

#define STATE_SERVER_SAMPLE_RATE "sampleRate"
#define STATE_SERVER_BUFFER_SIZE "bufferSize"
#define STATE_SERVER_TARGET_DELAY "targetDelay"

JsonBuilder *
message_state_builder_start (guint samplerate, guint buffer_size,
			     gdouble target_delay_ms)
{
  JsonBuilder *builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, STATE_DEVICES);
  json_builder_begin_array (builder);

  return builder;
}

void
message_state_builder_add_device (JsonBuilder *builder, guint id,
				  const gchar *overbridge_name,
				  const struct ow_device *device,
				  struct ow_resampler_state *state)
{
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, STATE_DEVICE_ID);
  json_builder_add_int_value (builder, id);
  json_builder_set_member_name (builder, STATE_DEVICE_NAME);
  json_builder_add_string_value (builder, overbridge_name);
  json_builder_set_member_name (builder, STATE_DEVICE_DEVICE);
  json_builder_add_string_value (builder, device->desc.name);
  json_builder_set_member_name (builder, STATE_DEVICE_BUS);
  json_builder_add_int_value (builder, device->bus);
  json_builder_set_member_name (builder, STATE_DEVICE_ADDRESS);
  json_builder_add_int_value (builder, device->address);
  json_builder_set_member_name (builder, STATE_DEVICE_LATENCY_O2H);
  json_builder_add_double_value (builder, state->latency_o2h);
  json_builder_set_member_name (builder, STATE_DEVICE_LATENCY_H2O);
  json_builder_add_double_value (builder, state->latency_h2o);
  json_builder_set_member_name (builder, STATE_DEVICE_RATIO_O2H);
  json_builder_add_double_value (builder, state->ratio_o2h);
  json_builder_set_member_name (builder, STATE_DEVICE_RATIO_H2O);
  json_builder_add_double_value (builder, state->ratio_h2o);
  json_builder_end_object (builder);
}

gchar *
message_state_builder_end (JsonBuilder *builder, gint64 samplerate,
			   gint64 buffer_size, double target_delay_ms)
{
  gchar *json;
  JsonNode *root;
  JsonGenerator *gen;

  json_builder_end_array (builder);

  json_builder_set_member_name (builder, STATE_SERVER_SAMPLE_RATE);
  json_builder_add_int_value (builder, samplerate);
  json_builder_set_member_name (builder, STATE_SERVER_BUFFER_SIZE);
  json_builder_add_int_value (builder, buffer_size);
  json_builder_set_member_name (builder, STATE_SERVER_TARGET_DELAY);
  json_builder_add_double_value (builder, target_delay_ms);

  json_builder_end_object (builder);

  gen = json_generator_new ();
  root = json_builder_get_root (builder);

  json_generator_set_root (gen, root);
  json_generator_set_pretty (gen, TRUE);
  json = json_generator_to_data (gen, NULL);

  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);

  return json;
}
