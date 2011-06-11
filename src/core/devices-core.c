/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Core input devices implementation */

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
#include "screen-private.h"
#include "devices-core.h"

/* Common functions */
static void
meta_device_core_common_allow_events (MetaDevice *device,
                                      int         mode,
                                      Time        time)
{
  MetaDisplay *display;

  display = meta_device_get_display (device);
  XAllowEvents (display->xdisplay, mode, time);
}

/* Core pointer */

G_DEFINE_TYPE (MetaDevicePointerCore,
               meta_device_pointer_core,
               META_TYPE_DEVICE_POINTER)

static gboolean
meta_device_pointer_core_grab (MetaDevice *device,
                               Window      xwindow,
                               guint       evmask,
                               MetaCursor  cursor,
                               gboolean    owner_events,
                               gboolean    sync,
                               Time        time)
{
  MetaDisplay *display;
  Cursor xcursor;
  int retval;

  display = meta_device_get_display (device);
  xcursor = meta_display_create_x_cursor (display, cursor);

  retval = XGrabPointer (display->xdisplay,
                         xwindow, owner_events,
                         evmask,
                         (sync) ? GrabModeSync : GrabModeAsync,
                         (sync) ? GrabModeSync : GrabModeAsync,
                         None, xcursor, time);

  if (xcursor != None)
    XFreeCursor (display->xdisplay, xcursor);

  return (retval == Success);
}

static void
meta_device_pointer_core_ungrab (MetaDevice *device,
                                 Time        time)
{
  MetaDisplay *display;

  display = meta_device_get_display (device);
  XUngrabPointer (display->xdisplay, time);
}

static void
meta_device_pointer_core_warp (MetaDevicePointer *pointer,
                               MetaScreen        *screen,
                               gint               x,
                               gint               y)
{
  MetaDisplay *display;

  display = meta_device_get_display (META_DEVICE (pointer));
  XWarpPointer (display->xdisplay,
                None, screen->xroot,
                0, 0, 0, 0, x, y);
}

static void
meta_device_pointer_core_set_window_cursor (MetaDevicePointer *pointer,
                                            Window             xwindow,
                                            MetaCursor         cursor)
{
  MetaDisplay *display;
  Cursor xcursor;

  display = meta_device_get_display (META_DEVICE (pointer));
  xcursor = meta_display_create_x_cursor (display, cursor);

  XDefineCursor (display->xdisplay, xwindow, xcursor);

  if (xcursor != None)
    XFreeCursor (display->xdisplay, xcursor);
}

static void
meta_device_pointer_core_query_position (MetaDevicePointer *pointer,
                                         Window             xwindow,
                                         Window            *root,
                                         Window            *child,
                                         gint              *root_x,
                                         gint              *root_y,
                                         gint              *x,
                                         gint              *y,
                                         guint             *mask)
{
  MetaDisplay *display;

  display = meta_device_get_display (META_DEVICE (pointer));
  XQueryPointer (display->xdisplay, xwindow,
                 root, child, root_x, root_y,
                 x, y, mask);
}

static void
meta_device_pointer_core_class_init (MetaDevicePointerCoreClass *klass)
{
  MetaDevicePointerClass *pointer_class = META_DEVICE_POINTER_CLASS (klass);
  MetaDeviceClass *device_class = META_DEVICE_CLASS (klass);

  device_class->allow_events = meta_device_core_common_allow_events;
  device_class->grab = meta_device_pointer_core_grab;
  device_class->ungrab = meta_device_pointer_core_ungrab;

  pointer_class->warp = meta_device_pointer_core_warp;
  pointer_class->set_window_cursor = meta_device_pointer_core_set_window_cursor;
  pointer_class->query_position = meta_device_pointer_core_query_position;
}

static void
meta_device_pointer_core_init (MetaDevicePointerCore *pointer)
{
}

MetaDevice *
meta_device_pointer_core_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_DEVICE_POINTER_CORE,
                       "device-id", META_CORE_POINTER_ID,
                       "display", display,
                       NULL);
}


/* Core Keyboard */

G_DEFINE_TYPE (MetaDeviceKeyboardCore,
               meta_device_keyboard_core,
               META_TYPE_DEVICE_KEYBOARD)

static gboolean
meta_device_keyboard_core_grab (MetaDevice *device,
                                Window      xwindow,
                                guint       evmask,
                                MetaCursor  cursor,
                                gboolean    owner_events,
                                gboolean    sync,
                                Time        time)
{
  MetaDisplay *display;
  gint retval;

  display = meta_device_get_display (device);
  retval = XGrabKeyboard (display->xdisplay, xwindow, owner_events,
                          (sync) ? GrabModeSync : GrabModeAsync,
                          (sync) ? GrabModeSync : GrabModeAsync,
                          time);

  return (retval == Success);
}

static void
meta_device_keyboard_core_ungrab (MetaDevice *device,
                                  Time        time)
{
  MetaDisplay *display;

  display = meta_device_get_display (device);
  XUngrabKeyboard (display->xdisplay, time);
}

static void
meta_device_keyboard_core_class_init (MetaDeviceKeyboardCoreClass *klass)
{
  MetaDeviceClass *device_class = META_DEVICE_CLASS (klass);

  device_class->allow_events = meta_device_core_common_allow_events;
  device_class->grab = meta_device_keyboard_core_grab;
  device_class->ungrab = meta_device_keyboard_core_ungrab;
}

static void
meta_device_keyboard_core_init (MetaDeviceKeyboardCore *keyboard)
{
}

MetaDevice *
meta_device_keyboard_core_new (MetaDisplay *display)
{
  return g_object_new (META_TYPE_DEVICE_KEYBOARD_CORE,
                       "device-id", META_CORE_KEYBOARD_ID,
                       "display", display,
                       NULL);
}
