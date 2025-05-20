/*
 * Copyright (C) 2021-2024 Red Hat Inc.
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

#include "mdk-touch.h"

#include <linux/input-event-codes.h>

#include "mdk-monitor.h"
#include "mdk-stream.h"

struct _MdkTouch
{
  MdkDevice parent;

  GHashTable *slots;
};

G_DEFINE_FINAL_TYPE (MdkTouch, mdk_touch, MDK_TYPE_DEVICE)

static void
mdk_touch_finalize (GObject *object)
{
  MdkTouch *touch = MDK_TOUCH (object);

  g_clear_pointer (&touch->slots, g_hash_table_unref);

  G_OBJECT_CLASS (mdk_touch_parent_class)->finalize (object);
}

static void
mdk_touch_class_init (MdkTouchClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mdk_touch_finalize;
}

static void
mdk_touch_init (MdkTouch *touch)
{
  touch->slots = g_hash_table_new (NULL, NULL);
}

MdkTouch *
mdk_touch_new (MdkSeat          *seat,
               struct ei_device *ei_device)
{
  return g_object_new (MDK_TYPE_TOUCH,
                       "seat", seat,
                       "ei-device", ei_device,
                       NULL);
}

void
mdk_touch_release_all (MdkTouch *touch)
{
  MdkDevice *device = MDK_DEVICE (touch);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);
  GHashTableIter iter;
  gpointer key, value;

  g_debug ("Releaseing pressed touches");

  g_hash_table_iter_init (&iter, touch->slots);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      struct ei_touch *ei_touch = value;
      int slot = GPOINTER_TO_INT (key);

      g_hash_table_iter_steal (&iter);

      g_debug ("Emit touch up, slot: %d", slot);
      ei_touch_up (ei_touch);
      ei_device_frame (ei_device, g_get_monotonic_time ());
      ei_touch_unref (ei_touch);
    }
}

void
mdk_touch_notify_down (MdkTouch *touch,
                       int       slot,
                       double    x,
                       double    y)
{
  MdkDevice *device = MDK_DEVICE (touch);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);
  struct ei_touch *ei_touch;

  ei_touch = ei_device_touch_new (ei_device);
  g_hash_table_insert (touch->slots, GINT_TO_POINTER (slot), ei_touch);

  g_debug ("Emit touch down, slot: %d (%p), position: %f, %f",
           slot, ei_touch, x, y);
  ei_touch_down (ei_touch, x, y);
  ei_device_frame (ei_device, g_get_monotonic_time ());
}

void
mdk_touch_notify_motion (MdkTouch *touch,
                         int       slot,
                         double    x,
                         double    y)
{
  MdkDevice *device = MDK_DEVICE (touch);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);
  struct ei_touch *ei_touch;

  ei_touch = g_hash_table_lookup (touch->slots, GINT_TO_POINTER (slot));
  if (!ei_touch)
    return;

  g_debug ("Emit touch motion, slot: %d, position: %f, %f",
           slot, x, y);
  ei_touch_motion (ei_touch, x, y);
  ei_device_frame (ei_device, g_get_monotonic_time ());
}

void
mdk_touch_notify_up (MdkTouch *touch,
                     int       slot)
{
  MdkDevice *device = MDK_DEVICE (touch);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);
  struct ei_touch *ei_touch = NULL;

  g_hash_table_steal_extended (touch->slots, GINT_TO_POINTER (slot),
                               NULL, (gpointer *) &ei_touch);
  if (!ei_touch)
    return;

  g_debug ("Emit touch up, slot: %d (%p)", slot, ei_touch);
  ei_touch_up (ei_touch);
  ei_device_frame (ei_device, g_get_monotonic_time ());
  ei_touch_unref (ei_touch);
}
