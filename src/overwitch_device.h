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
#include "overwitch.h"

G_BEGIN_DECLS
#define OVERWITCH_TYPE_DEVICE (overwitch_device_get_type())
G_DECLARE_FINAL_TYPE (OverwitchDevice, overwitch_device, OVERWITCH, DEVICE,
		      GObject);

struct _OverwitchDevice
{
  GObject parent_instance;
  //Properties
  guint32 id;
  gchar name[OW_LABEL_MAX_LEN];
  gchar device[OW_LABEL_MAX_LEN];
  guint8 bus;
  guint8 address;
  gchar status[OW_LABEL_MAX_LEN];
  gchar o2j_latency[OW_LABEL_MAX_LEN];
  gchar j2o_latency[OW_LABEL_MAX_LEN];
  gdouble o2j_ratio;
  gdouble j2o_ratio;
};

struct _OverwitchDeviceClass
{
  GObjectClass parent_class;
};

OverwitchDevice *overwitch_device_new (const guint32 id,
				       const gchar * overbridge_name,
				       const gchar * name, const guint8 bus,
				       const guint8 address,
				       const gchar * status,
				       const gchar * o2j_latency,
				       const gchar * j2o_latency,
				       const gdouble o2j_ratio,
				       const gdouble j2o_ratio);

G_END_DECLS
