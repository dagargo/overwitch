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

#include <glib/gi18n.h>
#include "message.h"

#define STATE_DEVICES "devices"
#define STATE_DEVICE_ID "id"
#define STATE_DEVICE_NAME "name"
#define STATE_DEVICE_DEVICE "device"
#define STATE_DEVICE_BUS "bus"
#define STATE_DEVICE_ADDRESS "address"
#define STATE_DEVICE_STATUS "status"
#define STATE_DEVICE_LATENCY_O2H "latencyO2H"
#define STATE_DEVICE_LATENCY_O2H_MAX "latencyO2HMax"
#define STATE_DEVICE_LATENCY_O2H_MIN "latencyO2HMin"
#define STATE_DEVICE_LATENCY_H2O "latencyH2O"
#define STATE_DEVICE_LATENCY_H2O_MAX "latencyH2OMax"
#define STATE_DEVICE_LATENCY_H2O_MIN "latencyH2OMin"
#define STATE_DEVICE_RATIO_O2H "ratioO2H"
#define STATE_DEVICE_RATIO_H2O "ratioH2O"

#define STATE_SERVER_SAMPLE_RATE "sampleRate"
#define STATE_SERVER_BUFFER_SIZE "bufferSize"
#define STATE_SERVER_TARGET_DELAY "targetDelay"

JsonBuilder *
message_state_builder_start ()
{
  JsonBuilder *builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, STATE_DEVICES);
  json_builder_begin_array (builder);

  return builder;
}

void
message_state_builder_add_device (JsonBuilder *builder, guint32 id,
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
  json_builder_set_member_name (builder, STATE_DEVICE_STATUS);
  json_builder_add_int_value (builder, state->status);
  json_builder_set_member_name (builder, STATE_DEVICE_LATENCY_O2H);
  json_builder_add_double_value (builder, state->t_latency_o2h);
  json_builder_set_member_name (builder, STATE_DEVICE_LATENCY_O2H_MAX);
  json_builder_add_double_value (builder, state->t_latency_o2h_max);
  json_builder_set_member_name (builder, STATE_DEVICE_LATENCY_O2H_MIN);
  json_builder_add_double_value (builder, state->t_latency_o2h_min);
  json_builder_set_member_name (builder, STATE_DEVICE_LATENCY_H2O);
  json_builder_add_double_value (builder, state->t_latency_h2o);
  json_builder_set_member_name (builder, STATE_DEVICE_LATENCY_H2O_MAX);
  json_builder_add_double_value (builder, state->t_latency_h2o_max);
  json_builder_set_member_name (builder, STATE_DEVICE_LATENCY_H2O_MIN);
  json_builder_add_double_value (builder, state->t_latency_h2o_min);
  json_builder_set_member_name (builder, STATE_DEVICE_RATIO_O2H);
  json_builder_add_double_value (builder, state->ratio_o2h);
  json_builder_set_member_name (builder, STATE_DEVICE_RATIO_H2O);
  json_builder_add_double_value (builder, state->ratio_h2o);
  json_builder_end_object (builder);
}

gchar *
message_state_builder_end (JsonBuilder *builder, guint32 samplerate,
			   guint32 buffer_size, gdouble target_delay_ms)
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

static const char *
get_status_string (ow_resampler_status_t status)
{
  switch (status)
    {
    case OW_RESAMPLER_STATUS_ERROR:
      return _("Error");
    case OW_RESAMPLER_STATUS_STOP:
      return _("Stopped");
    case OW_RESAMPLER_STATUS_READY:
      return _("Ready");
    case OW_RESAMPLER_STATUS_BOOT:
      return _("Booting");
    case OW_RESAMPLER_STATUS_TUNE:
      return _("Tuning");
    case OW_RESAMPLER_STATUS_RUN:
      return _("Running");
    case OW_RESAMPLER_STATUS_RETUNE:
      return _("Retuning");
    }
  return NULL;
}

JsonReader *
message_state_reader_start (const gchar *state, guint32 *devices)
{
  JsonParser *parser;
  JsonReader *reader;

  parser = json_parser_new_immutable ();
  json_parser_load_from_data (parser, state, -1, NULL);
  reader = json_reader_new (json_parser_steal_root (parser));
  g_object_unref (parser);

  if (!json_reader_read_member (reader, STATE_DEVICES))
    {
      g_object_unref (reader);
      return NULL;
    }

  if (!json_reader_is_array (reader))
    {
      g_object_unref (reader);
      return NULL;
    }

  *devices = json_reader_count_elements (reader);

  return reader;
}

OverwitchDevice *
message_state_reader_get_device (JsonReader *reader, guint index)
{
  OverwitchDevice *device = NULL;
  guint32 id, bus, address;
  struct ow_resampler_state state;
  static gchar o2h_latency[OW_LABEL_MAX_LEN];
  static gchar h2o_latency[OW_LABEL_MAX_LEN];
  const gchar *name, *device_name;

  state.t_latency_o2h = 0;
  state.t_latency_h2o = 0;
  state.ratio_o2h = 0;
  state.ratio_o2h = 0;

  if (!json_reader_read_element (reader, index))
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_ID))
    {
      id = json_reader_get_int_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_NAME))
    {
      name = json_reader_get_string_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_DEVICE))
    {
      device_name = json_reader_get_string_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_BUS))
    {
      bus = json_reader_get_int_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_ADDRESS))
    {
      address = json_reader_get_int_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_STATUS))
    {
      state.status = json_reader_get_int_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_LATENCY_O2H))
    {
      state.t_latency_o2h = json_reader_get_double_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_LATENCY_O2H_MAX))
    {
      state.t_latency_o2h_max = json_reader_get_double_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_LATENCY_O2H_MIN))
    {
      state.t_latency_o2h_min = json_reader_get_double_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_RATIO_O2H))
    {
      state.ratio_o2h = json_reader_get_double_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_LATENCY_H2O))
    {
      state.t_latency_h2o = json_reader_get_double_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_LATENCY_H2O_MAX))
    {
      state.t_latency_h2o_max = json_reader_get_double_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_LATENCY_H2O_MIN))
    {
      state.t_latency_h2o_min = json_reader_get_double_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (json_reader_read_member (reader, STATE_DEVICE_RATIO_H2O))
    {
      state.ratio_h2o = json_reader_get_double_value (reader);
      json_reader_end_member (reader);
    }
  else
    {
      goto end;
    }

  if (state.t_latency_o2h >= 0)
    {
      g_snprintf (o2h_latency, OW_LABEL_MAX_LEN,
		  "%.1f [%.1f, %.1f] ms", state.t_latency_o2h,
		  state.t_latency_o2h_min, state.t_latency_o2h_max);
    }
  else
    {
      o2h_latency[0] = '\0';
    }

  if (state.t_latency_h2o >= 0)
    {
      g_snprintf (h2o_latency, OW_LABEL_MAX_LEN,
		  "%.1f [%.1f, %.1f] ms", state.t_latency_h2o,
		  state.t_latency_h2o_min, state.t_latency_h2o_max);
    }
  else
    {
      h2o_latency[0] = '\0';
    }

  device = overwitch_device_new (id, name, device_name, bus, address,
				 get_status_string (state.status),
				 o2h_latency, h2o_latency,
				 state.ratio_o2h, state.ratio_h2o);

end:
  json_reader_end_element (reader);
  return device;
}

void
message_state_reader_end (JsonReader *reader, guint32 *samplerate,
			  guint32 *buffer_size, gdouble *target_delay_ms)
{
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, STATE_SERVER_SAMPLE_RATE))
    {
      *samplerate = json_reader_get_int_value (reader);
      json_reader_end_member (reader);
    }

  if (json_reader_read_member (reader, STATE_SERVER_BUFFER_SIZE))
    {
      *buffer_size = json_reader_get_int_value (reader);
      json_reader_end_member (reader);
    }

  if (json_reader_read_member (reader, STATE_SERVER_TARGET_DELAY))
    {
      *target_delay_ms = json_reader_get_double_value (reader);
      json_reader_end_member (reader);
    }

  g_object_unref (reader);
}
