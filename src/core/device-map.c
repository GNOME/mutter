/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Input device map */

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

#include "config.h"
#include "device-map-private.h"
#include "device-map-core.h"

#ifdef HAVE_XINPUT2
#include <X11/extensions/XInput2.h>
#include "device-map-xi2.h"

#define XINPUT2_VERSION_MAJOR 2
#define XINPUT2_VERSION_MINOR 2
#endif

G_DEFINE_TYPE (MetaDeviceMap, meta_device_map, G_TYPE_OBJECT)

typedef struct MetaDeviceMapPrivate MetaDeviceMapPrivate;

struct MetaDeviceMapPrivate
{
  MetaDisplay *display;
  GHashTable *devices;
};

enum {
  PROP_0,
  PROP_DISPLAY
};

enum {
  DEVICE_ADDED,
  DEVICE_REMOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
meta_device_map_get_property (GObject    *object,
                              guint       param_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  MetaDeviceMapPrivate *priv;

  priv = META_DEVICE_MAP (object)->priv;

  switch (param_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, priv->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
meta_device_map_set_property (GObject      *object,
                              guint         param_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  MetaDeviceMapPrivate *priv;

  priv = META_DEVICE_MAP (object)->priv;

  switch (param_id)
    {
    case PROP_DISPLAY:
      priv->display = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
meta_device_map_finalize (GObject *object)
{
  MetaDeviceMapPrivate *priv;
  GHashTableIter iter;
  MetaDevice *device;

  priv = META_DEVICE_MAP (object)->priv;
  g_hash_table_iter_init (&iter, priv->devices);

  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &device))
    {
      /* Detach the device */
      g_hash_table_iter_steal (&iter);

      g_signal_emit (object, signals[DEVICE_REMOVED], 0, device);
      g_object_unref (device);
    }

  g_hash_table_destroy (priv->devices);
  G_OBJECT_CLASS (meta_device_map_parent_class)->finalize (object);
}

static void
meta_device_map_class_init (MetaDeviceMapClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_device_map_get_property;
  object_class->set_property = meta_device_map_set_property;
  object_class->finalize = meta_device_map_finalize;

  g_object_class_install_property (object_class,
                                   PROP_DISPLAY,
                                   g_param_spec_object ("display",
                                                        "Display",
                                                        "Display",
                                                        META_TYPE_DISPLAY,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY));
  signals[DEVICE_ADDED] =
    g_signal_new ("device-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, META_TYPE_DEVICE);
  signals[DEVICE_REMOVED] =
    g_signal_new ("device-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, META_TYPE_DEVICE);

  g_type_class_add_private (klass, sizeof (MetaDeviceMapPrivate));
}

static void
meta_device_map_init (MetaDeviceMap *device_map)
{
  MetaDeviceMapPrivate *priv;

  priv = device_map->priv = G_TYPE_INSTANCE_GET_PRIVATE (device_map,
                                                         META_TYPE_DEVICE_MAP,
                                                         MetaDeviceMapPrivate);
  priv->devices = g_hash_table_new_full (NULL, NULL, NULL,
                                         (GDestroyNotify) g_object_unref);
}

void
meta_device_map_add_device (MetaDeviceMap *device_map,
                            MetaDevice    *device)
{
  MetaDeviceMapPrivate *priv;

  priv = device_map->priv;
  g_hash_table_insert (priv->devices,
                       GINT_TO_POINTER (meta_device_get_id (device)),
                       g_object_ref (device));

  g_signal_emit (device_map, signals[DEVICE_ADDED], 0, device);
}

void
meta_device_map_remove_device (MetaDeviceMap *device_map,
                               MetaDevice    *device)
{
  MetaDeviceMapPrivate *priv;

  priv = device_map->priv;

  if (g_hash_table_steal (priv->devices,
                          GINT_TO_POINTER (meta_device_get_id (device))))
    {
      g_signal_emit (device_map, signals[DEVICE_REMOVED], 0, device);
      g_object_unref (device);
    }
}


#ifdef HAVE_XINPUT2

static gboolean
initialize_xinput (MetaDisplay *display)
{
  int major, minor, opcode;
  int unused;

  if (!XQueryExtension (display->xdisplay,
                        "XInputExtension",
                        &opcode, &unused, &unused))
    return FALSE;

  major = XINPUT2_VERSION_MAJOR;
  minor = XINPUT2_VERSION_MINOR;

  XIQueryVersion (display->xdisplay, &major, &minor);

  if (major == XINPUT2_VERSION_MAJOR &&
      minor == XINPUT2_VERSION_MINOR)
    {
      display->have_xinput2 = TRUE;
      display->xinput2_opcode = opcode;

      return TRUE;
    }

  return FALSE;
}

#endif /* HAVE_XINPUT2 */

MetaDeviceMap *
meta_device_map_new (MetaDisplay *display,
                     gboolean     force_core)
{
  GType type = META_TYPE_DEVICE_MAP_CORE;

#ifdef HAVE_XINPUT2
  if (!force_core &&
      initialize_xinput (display))
    type = META_TYPE_DEVICE_MAP_XI2;
#endif

  return g_object_new (type,
                       "display", display,
                       NULL);
}

/**
 * meta_device_map_lookup:
 * @device_map: a #MetaDeviceMap
 * @device_id: ID for a device
 *
 * returns the device corresponding to @device_id
 *
 * Returns: (transfer none): (allow-none): The matching device, or %NULL.
 **/
MetaDevice *
meta_device_map_lookup (MetaDeviceMap *device_map,
                        gint           device_id)
{
  MetaDeviceMapPrivate *priv;

  g_return_val_if_fail (META_IS_DEVICE_MAP (device_map), NULL);

  priv = device_map->priv;
  return g_hash_table_lookup (priv->devices,
                              GINT_TO_POINTER (device_id));
}

/**
 * meta_device_map_get_display:
 * @device_map: a #MetaDeviceMap
 *
 * Returns the #MetaDisplay to which @device_map belongs to.
 *
 * Returns: (transfer none): The #MetaDisplay.
 **/
MetaDisplay *
meta_device_map_get_display (MetaDeviceMap *device_map)
{
  MetaDeviceMapPrivate *priv;

  g_return_val_if_fail (META_IS_DEVICE_MAP (device_map), NULL);

  priv = device_map->priv;
  return priv->display;
}

/**
 * meta_device_map_list_devices:
 * @device_map: a #MetaDeviceMap
 *
 * Returns the list of devices that @device_map holds.
 *
 * Returns: (element-type Meta.Device) (transfer container): the list
 *          of devices, the contained objects are owned by @device_map
 *          and should not be unref'ed. The list must be freed with
 *          g_list_free().
 **/
GList *
meta_device_map_list_devices (MetaDeviceMap *device_map)
{
  MetaDeviceMapPrivate *priv;

  g_return_val_if_fail (META_IS_DEVICE_MAP (device_map), NULL);

  priv = device_map->priv;
  return g_hash_table_get_values (priv->devices);
}

gboolean
meta_device_map_grab_key (MetaDeviceMap *device_map,
                          Window         xwindow,
                          guint          keycode,
                          guint          modifiers,
                          gboolean       sync)
{
  MetaDeviceMapClass *klass;

  g_return_val_if_fail (META_IS_DEVICE_MAP (device_map), FALSE);
  g_return_val_if_fail (xwindow != None, FALSE);

  klass = META_DEVICE_MAP_GET_CLASS (device_map);

  if (!klass->grab_key)
    return FALSE;

  return (klass->grab_key) (device_map, xwindow, keycode, modifiers, sync);
}

void
meta_device_map_ungrab_key (MetaDeviceMap *device_map,
                            Window         xwindow,
                            guint          keycode,
                            guint          modifiers)
{
  MetaDeviceMapClass *klass;

  g_return_if_fail (META_IS_DEVICE_MAP (device_map));
  g_return_if_fail (xwindow != None);

  klass = META_DEVICE_MAP_GET_CLASS (device_map);

  if (klass->ungrab_key)
    (klass->ungrab_key) (device_map, xwindow, keycode, modifiers);
}

gboolean
meta_device_map_grab_button (MetaDeviceMap *device_map,
                             Window         xwindow,
                             guint          n_button,
                             guint          modifiers,
                             guint          evmask,
                             gboolean       sync)
{
  MetaDeviceMapClass *klass;

  g_return_val_if_fail (META_IS_DEVICE_MAP (device_map), FALSE);
  g_return_val_if_fail (xwindow != None, FALSE);

  klass = META_DEVICE_MAP_GET_CLASS (device_map);

  if (!klass->grab_button)
    return FALSE;

  return (klass->grab_button) (device_map, xwindow, n_button,
                               modifiers, evmask, sync);
}

void
meta_device_map_ungrab_button (MetaDeviceMap *device_map,
                               Window         xwindow,
                               guint          n_button,
                               guint          modifiers)
{
  MetaDeviceMapClass *klass;

  g_return_if_fail (META_IS_DEVICE_MAP (device_map));
  g_return_if_fail (xwindow != None);

  klass = META_DEVICE_MAP_GET_CLASS (device_map);

  if (klass->ungrab_button)
    (klass->ungrab_button) (device_map, xwindow, n_button, modifiers);
}
