/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* XInput2 devices implementation */

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
#include "devices-xi2.h"
#include "display-private.h"
#include "screen-private.h"
#include <X11/extensions/XInput2.h>

/* Common functions */
static void
meta_device_xi2_common_allow_events (MetaDevice *device,
                                     int         mode,
                                     Time        time)
{
  MetaDisplay *display;
  gint device_id;

  display = meta_device_get_display (device);
  device_id = meta_device_get_id (device);

  switch (mode)
    {
    case AsyncPointer:
    case AsyncKeyboard:
      mode = XIAsyncDevice;
      break;
    case SyncPointer:
    case SyncKeyboard:
      mode = XISyncDevice;
      break;
    case ReplayPointer:
    case ReplayKeyboard:
      mode = XIReplayDevice;
      break;
    case AsyncBoth:
      mode = XIAsyncPair;
      break;
    case SyncBoth:
      mode = XISyncPair;
      break;
    }

  XIAllowEvents (display->xdisplay, device_id, mode, time);
}

static guchar *
translate_event_mask (guint  evmask,
                      gint  *len)
{
  guchar *mask;

  *len = XIMaskLen (XI_LASTEVENT);
  mask = g_new0 (guchar, *len);

  if (evmask & KeyPressMask)
    XISetMask (mask, XI_KeyPress);
  if (evmask & KeyReleaseMask)
    XISetMask (mask, XI_KeyRelease);
  if (evmask & ButtonPressMask)
    XISetMask (mask, XI_ButtonPress);
  if (evmask & ButtonReleaseMask)
    XISetMask (mask, XI_ButtonRelease);
  if (evmask & EnterWindowMask)
    XISetMask (mask, XI_Enter);
  if (evmask & LeaveWindowMask)
    XISetMask (mask, XI_Leave);

  /* No motion hints in XI2 at the moment... */
  if (evmask & PointerMotionMask ||
      evmask & PointerMotionHintMask)
    XISetMask (mask, XI_Motion);

  if (evmask & FocusChangeMask)
    {
      XISetMask (mask, XI_FocusIn);
      XISetMask (mask, XI_FocusOut);
    }

  return mask;
}

static gboolean
meta_device_xi2_common_grab (MetaDevice *device,
                             Window      xwindow,
                             guint       evmask,
                             MetaCursor  cursor,
                             gboolean    owner_events,
                             gboolean    sync,
                             Time        time)
{
  MetaDisplay *display;
  XIEventMask mask;
  gint device_id, retval;
  Cursor xcursor;

  display = meta_device_get_display (device);
  device_id = meta_device_get_id (device);
  xcursor = meta_display_create_x_cursor (display, cursor);

  mask.deviceid = device_id;
  mask.mask = translate_event_mask (evmask, &mask.mask_len);

  retval = XIGrabDevice (display->xdisplay,
                         device_id, xwindow,
                         time, xcursor,
                         (sync) ? GrabModeSync : GrabModeAsync,
                         (sync) ? GrabModeSync : GrabModeAsync,
                         owner_events, &mask);

  if (xcursor != None)
    XFreeCursor (display->xdisplay, xcursor);

  return (retval == Success);
}

static void
meta_device_xi2_common_ungrab (MetaDevice *device,
                               Time        time)
{
  MetaDisplay *display;
  gint device_id;

  display = meta_device_get_display (device);
  device_id = meta_device_get_id (device);

  XIUngrabDevice (display->xdisplay, device_id, time);
}

/* Pointer */

G_DEFINE_TYPE (MetaDevicePointerXI2,
               meta_device_pointer_xi2,
               META_TYPE_DEVICE_POINTER)

static void
meta_device_pointer_xi2_warp (MetaDevicePointer *pointer,
                              MetaScreen        *screen,
                              gint               x,
                              gint               y)
{
  MetaDisplay *display;
  int device_id;

  display = meta_device_get_display (META_DEVICE (pointer));
  device_id = meta_device_get_id (META_DEVICE (pointer));

  XIWarpPointer (display->xdisplay,
                 device_id,
                 None, screen->xroot,
                 0, 0, 0, 0, x, y);
}

static void
meta_device_pointer_xi2_set_window_cursor (MetaDevicePointer *pointer,
                                           Window             xwindow,
                                           MetaCursor         cursor)
{
  MetaDisplay *display;
  Cursor xcursor;
  int device_id;

  display = meta_device_get_display (META_DEVICE (pointer));
  device_id = meta_device_get_id (META_DEVICE (pointer));
  xcursor = meta_display_create_x_cursor (display, cursor);

  if (xcursor != None)
    {
      XIDefineCursor (display->xdisplay, device_id, xwindow, xcursor);
      XFreeCursor (display->xdisplay, xcursor);
    }
  else
    XIUndefineCursor (display->xdisplay, device_id, xwindow);
}

static void
meta_device_pointer_xi2_query_position (MetaDevicePointer *pointer,
                                        Window             xwindow,
                                        Window            *root_ret,
                                        Window            *child_ret,
                                        gint              *root_x_ret,
                                        gint              *root_y_ret,
                                        gint              *x_ret,
                                        gint              *y_ret,
                                        guint             *mask_ret)
{
  MetaDisplay *display;
  XIModifierState mods;
  XIGroupState group_unused;
  XIButtonState buttons;
  gdouble root_x, root_y, x, y;
  int device_id;

  display = meta_device_get_display (META_DEVICE (pointer));
  device_id = meta_device_get_id (META_DEVICE (pointer));

  XIQueryPointer (display->xdisplay,
                  device_id, xwindow,
                  root_ret, child_ret,
                  &root_x, &root_y, &x, &y,
                  &buttons, &mods,
                  &group_unused);
  if (mask_ret)
    {
      *mask_ret = mods.effective;

      if (XIMaskIsSet (buttons.mask, 1))
        *mask_ret |= Button1Mask;
      else if (XIMaskIsSet (buttons.mask, 2))
        *mask_ret |= Button2Mask;
      else if (XIMaskIsSet (buttons.mask, 3))
        *mask_ret |= Button3Mask;
    }

  if (root_x_ret)
    *root_x_ret = (int) root_x;

  if (root_y_ret)
    *root_y_ret = (int) root_y;

  if (x_ret)
    *x_ret = (int) x;

  if (y_ret)
    *y_ret = (int) y;
}

static void
meta_device_pointer_xi2_class_init (MetaDevicePointerXI2Class *klass)
{
  MetaDevicePointerClass *pointer_class = META_DEVICE_POINTER_CLASS (klass);
  MetaDeviceClass *device_class = META_DEVICE_CLASS (klass);

  device_class->allow_events = meta_device_xi2_common_allow_events;
  device_class->grab = meta_device_xi2_common_grab;
  device_class->ungrab = meta_device_xi2_common_ungrab;

  pointer_class->warp = meta_device_pointer_xi2_warp;
  pointer_class->set_window_cursor = meta_device_pointer_xi2_set_window_cursor;
  pointer_class->query_position = meta_device_pointer_xi2_query_position;
}

static void
meta_device_pointer_xi2_init (MetaDevicePointerXI2 *pointer)
{
}

MetaDevice *
meta_device_pointer_xi2_new (MetaDisplay *display,
                             gint         device_id)
{
  return g_object_new (META_TYPE_DEVICE_POINTER_XI2,
                       "device-id", device_id,
                       "display", display,
                       NULL);
}

/* Keyboard */

G_DEFINE_TYPE (MetaDeviceKeyboardXI2,
               meta_device_keyboard_xi2,
               META_TYPE_DEVICE_KEYBOARD)

static void
meta_device_keyboard_xi2_class_init (MetaDeviceKeyboardXI2Class *klass)
{
  MetaDeviceClass *device_class = META_DEVICE_CLASS (klass);

  device_class->allow_events = meta_device_xi2_common_allow_events;
  device_class->grab = meta_device_xi2_common_grab;
  device_class->ungrab = meta_device_xi2_common_ungrab;
}

static void
meta_device_keyboard_xi2_init (MetaDeviceKeyboardXI2 *keyboard)
{
}

MetaDevice *
meta_device_keyboard_xi2_new (MetaDisplay *display,
                              gint         device_id)
{
  return g_object_new (META_TYPE_DEVICE_KEYBOARD_XI2,
                       "device-id", device_id,
                       "display", display,
                       NULL);
}
