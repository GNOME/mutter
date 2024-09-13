/*
 * Copyright (C) 2024 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "mdk-device.h"

#include "mdk-seat.h"

enum
{
  PROP_0,

  PROP_SEAT,
  PROP_EI_DEVICE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MdkDevicePrivate
{
  MdkSeat *seat;

  struct ei_device *ei_device;

  uint32_t sequence;
} MdkDevicePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (MdkDevice, mdk_device, G_TYPE_OBJECT)

static void
mdk_device_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  MdkDevice *device = MDK_DEVICE (object);
  MdkDevicePrivate *priv = mdk_device_get_instance_private (device);

  switch (prop_id)
    {
    case PROP_SEAT:
      priv->seat = g_value_get_object (value);
      break;
    case PROP_EI_DEVICE:
      priv->ei_device = ei_device_ref (g_value_get_pointer (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_device_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  MdkDevice *device = MDK_DEVICE (object);
  MdkDevicePrivate *priv = mdk_device_get_instance_private (device);

  switch (prop_id)
    {
    case PROP_SEAT:
      g_value_set_object (value, priv->seat);
      break;
    case PROP_EI_DEVICE:
      g_value_set_pointer (value, priv->ei_device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_device_finalize (GObject *object)
{
  MdkDevice *device = MDK_DEVICE (object);
  MdkDevicePrivate *priv = mdk_device_get_instance_private (device);

  g_clear_pointer (&priv->ei_device, ei_device_unref);

  G_OBJECT_CLASS (mdk_device_parent_class)->finalize (object);
}

static void
mdk_device_class_init (MdkDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = mdk_device_set_property;
  object_class->get_property = mdk_device_get_property;
  object_class->finalize = mdk_device_finalize;

  obj_props[PROP_SEAT] =
    g_param_spec_object ("seat", NULL, NULL,
                         MDK_TYPE_SEAT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_EI_DEVICE] =
    g_param_spec_pointer ("ei-device", NULL, NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
mdk_device_init (MdkDevice *device)
{
}

MdkSeat *
mdk_device_get_seat (MdkDevice *device)
{
  MdkDevicePrivate *priv = mdk_device_get_instance_private (device);

  return priv->seat;
}

struct ei_device *
mdk_device_get_ei_device (MdkDevice *device)
{
  MdkDevicePrivate *priv = mdk_device_get_instance_private (device);

  return priv->ei_device;
}

void
mdk_device_process_event (MdkDevice       *device,
                          struct ei_event *ei_event)
{
  MdkDevicePrivate *priv = mdk_device_get_instance_private (device);

  switch (ei_event_get_type (ei_event))
    {
    case EI_EVENT_DEVICE_RESUMED:
      ei_device_start_emulating (ei_event_get_device (ei_event),
                                 ++priv->sequence);
      break;
    case EI_EVENT_DEVICE_PAUSED:
      break;

    default:
      g_assert_not_reached ();
    }
}
