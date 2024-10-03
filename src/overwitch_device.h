/*
 *   overwitch_device.h
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

#include <glib-object.h>

G_BEGIN_DECLS

#define OVERWITCH_TYPE_DEVICE (overwitch_device_get_type())
G_DECLARE_FINAL_TYPE (OverwitchDevice, overwitch_device, OVERWITCH, DEVICE,
		      GObject)

struct _OverwitchDevice
{
  GObject parent_instance;
  gpointer instance;
  //Properties
  gchar *name;
  gchar *device;
  guint bus;
  guint address;
  gchar *status;
  gchar *o2j_latency;
  gchar *j2o_latency;
  gdouble o2j_ratio;
  gdouble j2o_ratio;
};

struct _OverwitchDeviceClass
{
  GObjectClass parent_class;
};

OverwitchDevice *overwitch_device_new (const gchar * name,
				       const gchar * device,
				       const guint bus,
				       const guint address,
				       const gpointer instance);

void overwitch_device_set_status (OverwitchDevice * d, const gchar * status,
				  const gchar * o2j_latency,
				  const gchar * j2o_latency,
				  const gdouble o2j_ratio,
				  const gdouble j2o_ratio);

G_END_DECLS
