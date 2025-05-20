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

#include "mdk-keyboard.h"

#include <linux/input-event-codes.h>

#include "mdk-stream.h"

struct _MdkKeyboard
{
  MdkDevice parent;

  gboolean key_pressed[KEY_CNT];
};

G_DEFINE_FINAL_TYPE (MdkKeyboard, mdk_keyboard, MDK_TYPE_DEVICE)

static void
mdk_keyboard_class_init (MdkKeyboardClass *klass)
{
}

static void
mdk_keyboard_init (MdkKeyboard *keyboard)
{
}

MdkKeyboard *
mdk_keyboard_new (MdkSeat          *seat,
                  struct ei_device *ei_device)
{
  return g_object_new (MDK_TYPE_KEYBOARD,
                       "seat", seat,
                       "ei-device", ei_device,
                       NULL);
}

void
mdk_keyboard_release_all (MdkKeyboard *keyboard)
{
  int i;

  g_debug ("Releasing pressed keyboard keys");

  for (i = 0; i < G_N_ELEMENTS (keyboard->key_pressed); i++)
    {
      if (!keyboard->key_pressed[i])
        continue;

      mdk_keyboard_notify_key (keyboard, i, 0);
    }
}

void
mdk_keyboard_notify_key (MdkKeyboard *keyboard,
                         int32_t      key,
                         int          state)
{
  MdkDevice *device = MDK_DEVICE (keyboard);
  struct ei_device *ei_device = mdk_device_get_ei_device (device);

  if (key > G_N_ELEMENTS (keyboard->key_pressed))
    {
      g_warning ("Unknown key code 0x%x, ignoring", key);
      return;
    }

  if (state)
    {
      g_return_if_fail (!keyboard->key_pressed[key]);
      keyboard->key_pressed[key] = TRUE;
    }
  else
    {
      if (!keyboard->key_pressed[key])
        return;

      keyboard->key_pressed[key] = FALSE;
    }

  g_debug ("Emit keyboard key event, key: 0x%x, state: %s",
           key, state ? "press" : "release");
  ei_device_keyboard_key (ei_device, key, state);
  ei_device_frame (ei_device, g_get_monotonic_time ());
}
