/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Input device abstraction */

/*
 * Copyright (C) 2011 Carlos Garnacho
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>
#include "device.h"

G_DEFINE_ABSTRACT_TYPE (MetaDevice, meta_device, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_DEVICE_ID,
  PROP_DISPLAY,
  PROP_PAIRED_DEVICE
};

typedef struct MetaDevicePrivate MetaDevicePrivate;

struct MetaDevicePrivate
{
  MetaDisplay *display;
  MetaDevice *paired_device;
  gint device_id;
};

static void
meta_device_get_property (GObject    *object,
                          guint       param_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  switch (param_id)
    {
    case PROP_DEVICE_ID:
      g_value_set_int (value,
                       meta_device_get_id (META_DEVICE (object)));
      break;
    case PROP_DISPLAY:
      g_value_set_object (value,
                          meta_device_get_display (META_DEVICE (object)));
      break;
    case PROP_PAIRED_DEVICE:
      g_value_set_object (value,
                          meta_device_get_paired_device (META_DEVICE (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
meta_device_set_property (GObject      *object,
                          guint         param_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MetaDevicePrivate *priv = META_DEVICE (object)->priv;

  switch (param_id)
    {
    case PROP_DEVICE_ID:
      priv->device_id = g_value_get_int (value);
      break;
    case PROP_DISPLAY:
      priv->display = g_value_get_object (value);
      break;
    case PROP_PAIRED_DEVICE:
      meta_device_pair_devices (META_DEVICE (object),
                                g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
meta_device_class_init (MetaDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_device_get_property;
  object_class->set_property = meta_device_set_property;

  g_object_class_install_property (object_class,
                                   PROP_DEVICE_ID,
                                   g_param_spec_int ("device-id",
                                                     "Device ID",
                                                     "Device ID",
                                                     2, G_MAXINT, 2,
                                                     G_PARAM_READWRITE |
                                                     G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
                                   PROP_DISPLAY,
                                   g_param_spec_object ("display",
                                                        "Display",
                                                        "Display",
                                                        META_TYPE_DISPLAY,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
                                   PROP_PAIRED_DEVICE,
                                   g_param_spec_object ("paired-device",
                                                        "Paired device",
                                                        "Paired device",
                                                        META_TYPE_DEVICE,
                                                        G_PARAM_READWRITE));

  g_type_class_add_private (klass, sizeof (MetaDevicePrivate));
}

static void
meta_device_init (MetaDevice *device)
{
  device->priv = G_TYPE_INSTANCE_GET_PRIVATE (device,
                                              META_TYPE_DEVICE,
                                              MetaDevicePrivate);
}

int
meta_device_get_id (MetaDevice *device)
{
  MetaDevicePrivate *priv;

  g_return_val_if_fail (META_IS_DEVICE (device), 0);

  priv = device->priv;
  return priv->device_id;
}

MetaDisplay *
meta_device_get_display (MetaDevice *device)
{
  MetaDevicePrivate *priv;

  g_return_val_if_fail (META_IS_DEVICE (device), NULL);

  priv = device->priv;
  return priv->display;
}

void
meta_device_allow_events (MetaDevice  *device,
                          int          mode,
                          Time         time)
{
  MetaDeviceClass *klass;

  g_return_if_fail (META_IS_DEVICE (device));

  klass = META_DEVICE_GET_CLASS (device);

  if (klass->allow_events)
    (klass->allow_events) (device, mode, time);
}

gboolean
meta_device_grab (MetaDevice *device,
                  Window      xwindow,
                  guint       evmask,
                  MetaCursor  cursor,
                  gboolean    owner_events,
                  gboolean    sync,
                  Time        time)
{
  MetaDeviceClass *klass;

  g_return_val_if_fail (META_IS_DEVICE (device), FALSE);
  g_return_val_if_fail (xwindow != None, FALSE);

  klass = META_DEVICE_GET_CLASS (device);

  if (!klass->grab)
    return FALSE;

  return (klass->grab) (device, xwindow, evmask, cursor,
                        owner_events, sync, time);
}

void
meta_device_ungrab (MetaDevice *device,
                    Time        time)
{
  MetaDeviceClass *klass;

  g_return_if_fail (META_IS_DEVICE (device));

  klass = META_DEVICE_GET_CLASS (device);

  if (klass->ungrab)
    (klass->ungrab) (device, time);
}

void
meta_device_pair_devices (MetaDevice *device,
                          MetaDevice *other_device)
{
  MetaDevicePrivate *priv1, *priv2;

  g_return_if_fail (META_IS_DEVICE (device));
  g_return_if_fail (META_IS_DEVICE (other_device));

  priv1 = device->priv;
  priv2 = other_device->priv;

  /* Consider safe multiple calls
   * on already paired devices
   */
  if (priv1->paired_device != NULL &&
      priv2->paired_device != NULL &&
      priv1->paired_device == other_device &&
      priv2->paired_device == device)
    return;

  g_return_if_fail (priv1->paired_device == NULL);
  g_return_if_fail (priv2->paired_device == NULL);

  priv1->paired_device = g_object_ref (other_device);
  priv2->paired_device = g_object_ref (device);

  g_object_notify (G_OBJECT (device), "paired-device");
  g_object_notify (G_OBJECT (other_device), "paired-device");
}

MetaDevice *
meta_device_get_paired_device (MetaDevice *device)
{
  MetaDevicePrivate *priv;

  g_return_val_if_fail (META_IS_DEVICE (device), NULL);

  priv = device->priv;
  return priv->paired_device;
}
