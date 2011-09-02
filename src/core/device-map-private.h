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

#ifndef META_DEVICE_MAP_PRIVATE_H
#define META_DEVICE_MAP_PRIVATE_H

#include <meta/device-map.h>
#include <meta/device.h>
#include "display-private.h"
#include "device-private.h"

/* Device IDs for Virtual Core Pointer/Keyboard,
 * use only in case of emergency.
 */
#define META_CORE_POINTER_ID  2
#define META_CORE_KEYBOARD_ID 3

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
  void     (* grab_touch)     (MetaDeviceMap *pointer,
                               Window         xwindow);
  void     (* ungrab_touch)   (MetaDeviceMap *pointer,
                               Window         xwindow);
};

GType           meta_device_map_get_type (void) G_GNUC_CONST;

MetaDeviceMap * meta_device_map_new    (MetaDisplay   *display,
                                        gboolean       force_core);

void            meta_device_map_add_device    (MetaDeviceMap *device_map,
                                               MetaDevice    *device);
void            meta_device_map_remove_device (MetaDeviceMap *device_map,
                                               MetaDevice    *device);

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

void     meta_device_map_grab_touch      (MetaDeviceMap      *device_map,
                                          Window              xwindow);
void     meta_device_map_ungrab_touch    (MetaDeviceMap      *device_map,
                                          Window              xwindow);

#endif /* META_DEVICE_MAP_PRIVATE_H */
