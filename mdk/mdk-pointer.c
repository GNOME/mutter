/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include "mdk-pointer.h"

#include <linux/input-event-codes.h>

#include "mdk-monitor.h"
#include "mdk-stream.h"

/* discrete scroll unit in libei, see ei_device_scroll_discrete() */
#define SCROLL_UNIT 120

struct _MdkPointer
{
  MdkDevice parent;

  gboolean button_pressed[KEY_CNT];
};

G_DEFINE_FINAL_TYPE (MdkPointer, mdk_pointer, MDK_TYPE_DEVICE)

static void
mdk_pointer_class_init (MdkPointerClass *klass)
{
}

static void
mdk_pointer_init (MdkPointer *pointer)
{
}

MdkPointer *
mdk_pointer_new (MdkSeat          *seat,
                 struct ei_device *ei_device)
{
  return g_object_new (MDK_TYPE_POINTER,
                       "seat", seat,
                       "ei-device", ei_device,
                       NULL);
}

void
mdk_pointer_release_all (MdkPointer *pointer)
{
  int i;

  g_debug ("Releasing pressed pointer buttons");

  for (i = 0; i < G_N_ELEMENTS (pointer->button_pressed); i++)
    {
      if (!pointer->button_pressed[i])
        continue;

      mdk_pointer_notify_button (pointer, i, 0);
    }
}

void
mdk_pointer_notify_motion (MdkPointer *pointer,
                           double      x,
                           double      y)
{
  MdkDevice *device = MDK_DEVICE (pointer);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);

  g_debug ("Emit absolute pointer motion %f, %f", x, y);

  ei_device_pointer_motion_absolute (ei_device, x, y);
  ei_device_frame (ei_device, g_get_monotonic_time ());
}

void
mdk_pointer_notify_button (MdkPointer *pointer,
                           int32_t     button,
                           int         state)
{
  MdkDevice *device = MDK_DEVICE (pointer);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);

  if (button > G_N_ELEMENTS (pointer->button_pressed))
    {
      g_warning ("Unknown button key code 0x%x, ignoring", button);
      return;
    }

  if (state)
    {
      g_return_if_fail (!pointer->button_pressed[button]);
      pointer->button_pressed[button] = TRUE;
    }
  else
    {
      if (!pointer->button_pressed[button])
        return;

      pointer->button_pressed[button] = FALSE;
    }

  g_debug ("Emit pointer button 0x%x %s",
           button,
           state ? "pressed" : "released");

  ei_device_button_button (ei_device, button, state);
  ei_device_frame (ei_device, g_get_monotonic_time ());
}

void
mdk_pointer_notify_scroll (MdkPointer *pointer,
                           double      dx,
                           double      dy)
{
  MdkDevice *device = MDK_DEVICE (pointer);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);

  g_debug ("Emit scroll delta %f, %f", dx, dy);

  ei_device_scroll_delta (ei_device, dx, dy);
  ei_device_frame (ei_device, g_get_monotonic_time ());
}

void
mdk_pointer_notify_scroll_end (MdkPointer *pointer)
{
  MdkDevice *device = MDK_DEVICE (pointer);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);

  g_debug ("Emit scroll stop");

  ei_device_scroll_stop (ei_device, true, true);
  ei_device_frame (ei_device, g_get_monotonic_time ());
}

void
mdk_pointer_notify_scroll_discrete (MdkPointer         *pointer,
                                    GdkScrollDirection  direction)
{
  MdkDevice *device = MDK_DEVICE (pointer);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);
  int32_t x;
  int32_t y;

  switch (direction)
    {
    case GDK_SCROLL_UP:
      x = 0;
      y = -SCROLL_UNIT;
      break;
    case GDK_SCROLL_DOWN:
      x = 0;
      y = SCROLL_UNIT;
      break;
    case GDK_SCROLL_LEFT:
      x = -SCROLL_UNIT;
      y = 0;
      break;
    case GDK_SCROLL_RIGHT:
      x = SCROLL_UNIT;
      y = 0;
      break;
    case GDK_SCROLL_SMOOTH:
    default:
      g_assert_not_reached ();
    }

  g_debug ("Emit discreete scroll %d, %d", x, y);

  ei_device_scroll_discrete (ei_device, x, y);
  ei_device_frame (ei_device, g_get_monotonic_time ());
}
