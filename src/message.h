/*
 *   state.h
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

#pragma once

#include <glib.h>
#include "utils.h"
#include "overwitch.h"
#include "overwitch_device.h"

JsonBuilder *message_state_builder_start ();

void message_state_builder_add_device (JsonBuilder * builder, guint32 id,
				       const gchar * overbridge_name,
				       const struct ow_device *device,
				       struct ow_resampler_state *state);

gchar *message_state_builder_end (JsonBuilder * builder, guint32 samplerate,
				  guint32 buffer_size);

JsonReader *message_state_reader_start (const gchar * state,
					guint32 * devices);

OverwitchDevice *message_state_reader_get_device (JsonReader * reader,
						  guint index);

void message_state_reader_end (JsonReader * reader, guint32 * samplerate,
			       guint32 * buffer_size);
