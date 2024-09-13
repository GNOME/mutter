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

#include "mdk-seat.h"

#include "mdk-ei.h"
#include "mdk-keyboard.h"
#include "mdk-pointer.h"
#include "mdk-touch.h"

struct _MdkSeat
{
  GObject parent;

  MdkEi *ei;

  struct ei_seat *ei_seat;
  GHashTable *devices;

  MdkPointer *pointer;
  MdkKeyboard *keyboard;
  MdkTouch *touch;
};

G_DEFINE_FINAL_TYPE (MdkSeat, mdk_seat, G_TYPE_OBJECT)

static void
mdk_seat_finalize (GObject *object)
{
  MdkSeat *seat = MDK_SEAT (object);

  g_clear_pointer (&seat->devices, g_hash_table_unref);
  g_clear_pointer (&seat->ei_seat, ei_seat_unref);

  G_OBJECT_CLASS (mdk_seat_parent_class)->finalize (object);
}

static void
mdk_seat_class_init (MdkSeatClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mdk_seat_finalize;
}

static void
mdk_seat_init (MdkSeat *seat)
{
  seat->devices = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);
}

MdkSeat *
mdk_seat_new (MdkEi          *ei,
              struct ei_seat *ei_seat)
{
  MdkSeat *seat;

  seat = g_object_new (MDK_TYPE_SEAT, NULL);
  seat->ei = ei;
  seat->ei_seat = ei_seat_ref (ei_seat);

  return seat;
}

void
mdk_seat_process_event (MdkSeat         *seat,
                        struct ei_event *ei_event)
{
  switch (ei_event_get_type (ei_event))
    {
    case EI_EVENT_DEVICE_ADDED:
      {
        struct ei_device *ei_device = ei_event_get_device (ei_event);

        if (ei_device_has_capability (ei_device, EI_DEVICE_CAP_POINTER_ABSOLUTE))
          {
            MdkPointer *pointer;

            g_warn_if_fail (!seat->pointer);

            g_debug ("Device %s added as a pointer device",
                     ei_device_get_name (ei_device));
            pointer = mdk_pointer_new (seat, ei_device);
            g_hash_table_insert (seat->devices, ei_device, pointer);
            g_object_add_weak_pointer (G_OBJECT (pointer),
                                       (gpointer *) &seat->pointer);
            seat->pointer = pointer;
          }
        else if (ei_device_has_capability (ei_device, EI_DEVICE_CAP_KEYBOARD))
          {
            MdkKeyboard *keyboard;

            g_warn_if_fail (!seat->keyboard);

            g_debug ("Device %s added as a keyboard device",
                     ei_device_get_name (ei_device));
            keyboard = mdk_keyboard_new (seat, ei_device);
            g_hash_table_insert (seat->devices, ei_device, keyboard);
            g_object_add_weak_pointer (G_OBJECT (keyboard),
                                       (gpointer *) &seat->keyboard);
            seat->keyboard = keyboard;
          }
        else if (ei_device_has_capability (ei_device, EI_DEVICE_CAP_TOUCH))
          {
            MdkTouch *touch;

            g_warn_if_fail (!seat->touch);

            g_debug ("Device %s added as a touch device",
                     ei_device_get_name (ei_device));
            touch = mdk_touch_new (seat, ei_device);
            g_hash_table_insert (seat->devices, ei_device, touch);
            g_object_add_weak_pointer (G_OBJECT (touch),
                                       (gpointer *) &seat->touch);
            seat->touch = touch;
          }
        else
          {
            g_warning ("Unhandled device %s", ei_device_get_name (ei_device));
          }
        break;
      }
    case EI_EVENT_DEVICE_REMOVED:
      g_hash_table_remove (seat->devices, ei_event_get_device (ei_event));
      break;
    case EI_EVENT_DEVICE_RESUMED:
    case EI_EVENT_DEVICE_PAUSED:
      {
        MdkDevice *device;

        device = g_hash_table_lookup (seat->devices,
                                      ei_event_get_device (ei_event));
        g_return_if_fail (device);

        mdk_device_process_event (device, ei_event);
        break;
      }
    default:
      g_assert_not_reached ();
    }
}

void
mdk_seat_bind_pointer (MdkSeat *seat)
{
  g_debug ("Binding pointer capability");
  ei_seat_bind_capabilities (seat->ei_seat,
                             EI_DEVICE_CAP_POINTER_ABSOLUTE,
                             EI_DEVICE_CAP_BUTTON,
                             EI_DEVICE_CAP_SCROLL,
                             NULL);
}

void
mdk_seat_unbind_pointer (MdkSeat *seat)
{
  g_debug ("Unbinding pointer capability");
  ei_seat_unbind_capabilities (seat->ei_seat,
                               EI_DEVICE_CAP_POINTER_ABSOLUTE,
                               EI_DEVICE_CAP_BUTTON,
                               EI_DEVICE_CAP_SCROLL,
                               NULL);
}

MdkPointer *
mdk_seat_get_pointer (MdkSeat *seat)
{
  return seat->pointer;
}

void
mdk_seat_bind_keyboard (MdkSeat *seat)
{
  g_debug ("Binding keyboard capability");
  ei_seat_bind_capabilities (seat->ei_seat,
                             EI_DEVICE_CAP_KEYBOARD,
                             NULL);
}

void
mdk_seat_unbind_keyboard (MdkSeat *seat)
{
  g_debug ("Unbinding keyboard capability");
  ei_seat_unbind_capabilities (seat->ei_seat,
                               EI_DEVICE_CAP_KEYBOARD,
                               NULL);
}

MdkKeyboard *
mdk_seat_get_keyboard (MdkSeat *seat)
{
  return seat->keyboard;
}

void
mdk_seat_bind_touch (MdkSeat *seat)
{
  g_debug ("Binding touch capability");
  ei_seat_bind_capabilities (seat->ei_seat,
                             EI_DEVICE_CAP_TOUCH,
                             NULL);
}

void
mdk_seat_unbind_touch (MdkSeat *seat)
{
  g_debug ("Unbinding touch capability");
  ei_seat_unbind_capabilities (seat->ei_seat,
                               EI_DEVICE_CAP_TOUCH,
                               NULL);
}

MdkTouch *
mdk_seat_get_touch (MdkSeat *seat)
{
  return seat->touch;
}
