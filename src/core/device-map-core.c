/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Input device map, core protocol implementation */

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
#include "device-map-core.h"
#include "devices-core.h"

G_DEFINE_TYPE (MetaDeviceMapCore, meta_device_map_core, META_TYPE_DEVICE_MAP)

static gboolean
meta_device_map_core_grab_key (MetaDeviceMap *device_map,
                               Window         xwindow,
                               guint          keycode,
                               guint          modifiers,
                               gboolean       sync)
{
  MetaDisplay *display;
  gint retval;

  display = meta_device_map_get_display (device_map);
  retval = XGrabKey (display->xdisplay, keycode, modifiers,
                     xwindow, True,
                     GrabModeAsync, /* Never care about the other device */
                     (sync) ? GrabModeSync : GrabModeAsync);

  return (retval == Success);
}

static void
meta_device_map_core_ungrab_key (MetaDeviceMap *device_map,
                                 Window         xwindow,
                                 guint          keycode,
                                 guint          modifiers)
{
  MetaDisplay *display;

  display = meta_device_map_get_display (device_map);
  XUngrabKey (display->xdisplay, keycode, modifiers, xwindow);
}

static gboolean
meta_device_map_core_grab_button (MetaDeviceMap *device_map,
                                  Window         xwindow,
                                  guint          n_button,
                                  guint          modifiers,
                                  guint          evmask,
                                  gboolean       sync)
{
  MetaDisplay *display;
  gint retval;

  display = meta_device_map_get_display (device_map);
  retval = XGrabButton (display->xdisplay, n_button,
                        modifiers, xwindow, False,
                        evmask,
                        (sync) ? GrabModeSync : GrabModeAsync,
                        GrabModeAsync, /* Never care about the other device */
                        None, None);

  return (retval == Success);
}

static void
meta_device_map_core_ungrab_button (MetaDeviceMap *device_map,
                                    Window         xwindow,
                                    guint          n_button,
                                    guint          modifiers)
{
  MetaDisplay *display;

  display = meta_device_map_get_display (device_map);
  XUngrabButton (display->xdisplay, n_button, modifiers, xwindow);
}

static void
meta_device_map_core_constructed (GObject *object)
{
  MetaDeviceMap *device_map = META_DEVICE_MAP (object);
  MetaDevice *pointer, *keyboard;
  MetaDisplay *display;

  display = meta_device_map_get_display (device_map);

  /* Insert core devices */
  pointer = meta_device_pointer_core_new (display);
  meta_device_map_add_device (device_map, pointer);

  keyboard = meta_device_keyboard_core_new (display);
  meta_device_map_add_device (device_map, keyboard);

  meta_device_pair_devices (pointer, keyboard);

  g_object_unref (pointer);
  g_object_unref (keyboard);
}

static void
meta_device_map_core_class_init (MetaDeviceMapCoreClass *klass)
{
  MetaDeviceMapClass *device_map_class = META_DEVICE_MAP_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_device_map_core_constructed;

  device_map_class->grab_key = meta_device_map_core_grab_key;
  device_map_class->ungrab_key = meta_device_map_core_ungrab_key;
  device_map_class->grab_button = meta_device_map_core_grab_button;
  device_map_class->ungrab_button = meta_device_map_core_ungrab_button;
}

static void
meta_device_map_core_init (MetaDeviceMapCore *device_map)
{
}
