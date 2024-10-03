/*
 *   overwitch_device.c
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

#include <gtk/gtk.h>
#include "overwitch_device.h"

G_DEFINE_TYPE (OverwitchDevice, overwitch_device, G_TYPE_OBJECT)

enum list_store_columns
{
  PROP_DEVICE = 1,
  PROP_NAME,
  PROP_BUS,
  PROP_ADDRESS,
  PROP_STATUS,
  PROP_O2J_LATENCY,
  PROP_J2O_LATENCY,
  PROP_O2J_RATIO,
  PROP_J2O_RATIO,
  N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void
overwitch_device_init (OverwitchDevice *item)
{
}

static void
overwitch_device_dispose (GObject *gobject)
{
  G_OBJECT_CLASS (overwitch_device_parent_class)->dispose (gobject);
}

static void
overwitch_device_finalize (GObject *gobject)
{
  OverwitchDevice *d = OVERWITCH_DEVICE (gobject);

  g_free (d->name);
  g_free (d->status);
  g_free (d->device);
  g_free (d->o2j_latency);
  g_free (d->j2o_latency);

  G_OBJECT_CLASS (overwitch_device_parent_class)->finalize (gobject);
}

static void
overwitch_device_set_property (GObject *object, guint prop_id,
			       const GValue *value, GParamSpec *pspec)
{
  OverwitchDevice *d = OVERWITCH_DEVICE (object);

  switch (prop_id)
    {
    case PROP_STATUS:
      d->status = g_strdup (g_value_get_string (value));
      break;

    case PROP_O2J_LATENCY:
      d->o2j_latency = g_strdup (g_value_get_string (value));
      break;

    case PROP_J2O_LATENCY:
      d->j2o_latency = g_strdup (g_value_get_string (value));
      break;

    case PROP_O2J_RATIO:
      d->o2j_ratio = g_value_get_double (value);
      break;

    case PROP_J2O_RATIO:
      d->j2o_ratio = g_value_get_double (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
overwitch_device_get_property (GObject *object,
			       guint prop_id,
			       GValue *value, GParamSpec *pspec)
{
  OverwitchDevice *d = OVERWITCH_DEVICE (object);

  switch (prop_id)
    {
    case PROP_DEVICE:
      g_value_set_string (value, d->device);
      break;

    case PROP_NAME:
      g_value_set_string (value, d->name);
      break;

    case PROP_BUS:
      g_value_set_uint (value, d->bus);
      break;

    case PROP_ADDRESS:
      g_value_set_uint (value, d->address);
      break;

    case PROP_STATUS:
      g_value_set_string (value, d->status);
      break;

    case PROP_O2J_LATENCY:
      g_value_set_string (value, d->o2j_latency);
      break;

    case PROP_J2O_LATENCY:
      g_value_set_string (value, d->j2o_latency);
      break;

    case PROP_O2J_RATIO:
      g_value_set_double (value, d->o2j_ratio);
      break;

    case PROP_J2O_RATIO:
      g_value_set_double (value, d->j2o_ratio);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
overwitch_device_class_init (OverwitchDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = overwitch_device_dispose;
  object_class->finalize = overwitch_device_finalize;
  object_class->set_property = overwitch_device_set_property;
  object_class->get_property = overwitch_device_get_property;

  obj_properties[PROP_DEVICE] =
    g_param_spec_string ("device", "Device", "Overbridge device.", NULL,
			 G_PARAM_READABLE);

  obj_properties[PROP_NAME] =
    g_param_spec_string ("name", "Name", "Custom name", NULL,
			 G_PARAM_READABLE);

  obj_properties[PROP_BUS] =
    g_param_spec_uint ("bus", "Bus", "USB bus.", 0, 0, 0, G_PARAM_READABLE);

  obj_properties[PROP_ADDRESS] =
    g_param_spec_uint ("address", "Address", "USB address.", 0, 0, 0,
		       G_PARAM_READABLE);

  obj_properties[PROP_STATUS] =
    g_param_spec_string ("status", "Status", "Resampler status.", NULL,
			 G_PARAM_READWRITE);

  obj_properties[PROP_O2J_LATENCY] =
    g_param_spec_string ("o2j_latency", "O2J latency",
			 "Overwitch to JACK latency.", NULL,
			 G_PARAM_READWRITE);

  obj_properties[PROP_J2O_LATENCY] =
    g_param_spec_string ("j2o_latency", "J2O latency",
			 "JACK to Overwitch latency.", NULL,
			 G_PARAM_READWRITE);

  obj_properties[PROP_O2J_RATIO] =
    g_param_spec_double ("o2j_ratio", "O2J ratio", "Overwitch to JACK ratio.",
			 0, 32, 1, G_PARAM_READWRITE);

  obj_properties[PROP_J2O_RATIO] =
    g_param_spec_double ("j2o_ratio", "J2O ratio", "JACK to Overwitch ratio.",
			 0, 32, 1, G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, N_PROPERTIES,
				     obj_properties);
}

OverwitchDevice *
overwitch_device_new (const gchar *name, const gchar *device, const guint bus,
		      const guint address, const gpointer instance)
{
  OverwitchDevice *d = g_object_new (OVERWITCH_TYPE_DEVICE, NULL);
  d->name = g_strdup (name);
  d->device = g_strdup (device);
  d->bus = bus;
  d->address = address;
  d->instance = instance;
  return d;
}

void
overwitch_device_set_status (OverwitchDevice *d, const gchar *status,
			     const gchar *o2j_latency,
			     const gchar *j2o_latency,
			     const gdouble o2j_ratio, const gdouble j2o_ratio)
{
  g_free (d->status);
  g_free (d->o2j_latency);
  g_free (d->j2o_latency);

  d->status = g_strdup (status);
  d->o2j_latency = g_strdup (o2j_latency);
  d->j2o_latency = g_strdup (j2o_latency);
  d->o2j_ratio = o2j_ratio;
  d->j2o_ratio = j2o_ratio;

  g_object_notify (G_OBJECT (d), "status");
  g_object_notify (G_OBJECT (d), "o2j_latency");
  g_object_notify (G_OBJECT (d), "j2o_latency");
  g_object_notify (G_OBJECT (d), "o2j_ratio");
  g_object_notify (G_OBJECT (d), "j2o_ratio");
}
