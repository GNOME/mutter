/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * \file device-map.h  object containing input devices
 *
 * Input devices.
 * This file contains the device map, used to find out the device behind
 * XInput2/core events.
 */

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

#ifndef META_DEVICE_MAP_H
#define META_DEVICE_MAP_H

typedef struct _MetaDeviceMap MetaDeviceMap;
typedef struct _MetaDeviceMapClass MetaDeviceMapClass;

#include "display-private.h"
#include "device.h"

#define META_TYPE_DEVICE_MAP            (meta_device_map_get_type ())
#define META_DEVICE_MAP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEVICE_MAP, MetaDeviceMap))
#define META_DEVICE_MAP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEVICE_MAP, MetaDeviceMapClass))
#define META_IS_DEVICE_MAP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_TYPE_DEVICE_MAP))
#define META_IS_DEVICE_MAP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEVICE_MAP))
#define META_DEVICE_MAP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEVICE_MAP, MetaDeviceMapClass))

struct _MetaDeviceMap
{
  GObject parent_instance;
  gpointer priv;
};

struct _MetaDeviceMapClass
{
  GObjectClass parent_instance;

  void (* device_added)   (MetaDeviceMap *device_map,
                           MetaDevice    *device);
  void (* device_removed) (MetaDeviceMap *device_map,
                           MetaDevice    *device);

  gboolean (* grab_key)       (MetaDeviceMap *device_map,
                               Window         xwindow,
                               guint          keycode,
                               guint          modifiers,
                               gboolean       sync);
  void     (* ungrab_key)     (MetaDeviceMap *device_map,
                               Window         xwindow,
                               guint          keycode,
                               guint          modifiers);

  gboolean (* grab_button)    (MetaDeviceMap *device_map,
                               Window         xwindow,
                               guint          n_button,
                               guint          modifiers,
                               guint          evmask,
                               gboolean       sync);
  void     (* ungrab_button)  (MetaDeviceMap *pointer,
                               Window         xwindow,
                               guint          n_button,
                               guint          modifiers);
};

GType           meta_device_map_get_type (void) G_GNUC_CONST;

MetaDeviceMap * meta_device_map_new    (MetaDisplay   *display,
                                        gboolean       force_core);

void            meta_device_map_add_device    (MetaDeviceMap *device_map,
                                               MetaDevice    *device);
void            meta_device_map_remove_device (MetaDeviceMap *device_map,
                                               MetaDevice    *device);

MetaDevice *    meta_device_map_lookup (MetaDeviceMap *device_map,
                                        gint           device_id);

MetaDisplay *   meta_device_map_get_display (MetaDeviceMap *device_map);

gboolean meta_device_map_grab_key        (MetaDeviceMap      *device_map,
                                          Window              xwindow,
                                          guint               keycode,
                                          guint               modifiers,
                                          gboolean            sync);
void     meta_device_map_ungrab_key      (MetaDeviceMap      *device_map,
                                          Window              xwindow,
                                          guint               keycode,
                                          guint               modifiers);
gboolean meta_device_map_grab_button     (MetaDeviceMap      *device_map,
                                          Window              xwindow,
                                          guint               n_button,
                                          guint               modifiers,
                                          guint               evmask,
                                          gboolean            sync);
void     meta_device_map_ungrab_button   (MetaDeviceMap      *device_map,
                                          Window              xwindow,
                                          guint               n_button,
                                          guint               modifiers);


#endif /* META_DEVICE_MAP_H */
