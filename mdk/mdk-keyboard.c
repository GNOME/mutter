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

#include "mdk-dbus-remote-desktop.h"

typedef struct _MdkSessionKeyboard
{
  grefcount ref_count;
  int key_count[KEY_CNT];
} MdkSessionKeyboard;

struct _MdkKeyboard
{
  GObject parent;

  MdkSession *session;
  MdkDBusRemoteDesktopSession *session_proxy;

  MdkSessionKeyboard *session_keyboard;

  gboolean key_pressed[KEY_CNT];
};

static GQuark quark_session_keyboard = 0;

G_DEFINE_FINAL_TYPE (MdkKeyboard, mdk_keyboard, G_TYPE_OBJECT)

static MdkSessionKeyboard *
mdk_keyboard_ensure_session_keyboard (MdkKeyboard *keyboard)
{
  MdkSessionKeyboard *session_keyboard;

  if (keyboard->session_keyboard)
    return keyboard->session_keyboard;

  session_keyboard = g_object_get_qdata (G_OBJECT (keyboard->session),
                                        quark_session_keyboard);

  if (session_keyboard)
    {
      keyboard->session_keyboard = session_keyboard;
      g_ref_count_inc (&session_keyboard->ref_count);
      return session_keyboard;
    }

  session_keyboard = g_new0 (MdkSessionKeyboard, 1);
  g_ref_count_init (&session_keyboard->ref_count);
  keyboard->session_keyboard = session_keyboard;
  g_object_set_qdata (G_OBJECT (keyboard->session),
                      quark_session_keyboard,
                      session_keyboard);
  return session_keyboard;
}

static void
mdk_keyboard_finalize (GObject *object)
{
  MdkKeyboard *keyboard = MDK_KEYBOARD (object);

  if (keyboard->session_keyboard)
    {
      if (g_ref_count_dec (&keyboard->session_keyboard->ref_count))
        {
          g_object_set_qdata (G_OBJECT (keyboard->session),
                              quark_session_keyboard,
                              NULL);
          g_clear_pointer (&keyboard->session_keyboard, g_free);
        }
    }

  G_OBJECT_CLASS (mdk_keyboard_parent_class)->finalize (object);
}

static void
mdk_keyboard_class_init (MdkKeyboardClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mdk_keyboard_finalize;

  quark_session_keyboard = g_quark_from_static_string ("-mdk-session-keyboard");
}

static void
mdk_keyboard_init (MdkKeyboard *keyboard)
{
}

MdkKeyboard *
mdk_keyboard_new (MdkSession                  *session,
                  MdkDBusRemoteDesktopSession *session_proxy)
{
  MdkKeyboard *keyboard;

  keyboard = g_object_new (MDK_TYPE_KEYBOARD, NULL);
  keyboard->session = session;
  keyboard->session_proxy = session_proxy;

  return keyboard;
}

void
mdk_keyboard_release_all (MdkKeyboard *keyboard)
{
  int i;

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
  MdkSessionKeyboard *session_keyboard;

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

  session_keyboard = mdk_keyboard_ensure_session_keyboard (keyboard);

  if (state)
    {
      session_keyboard->key_count[key]++;
    }
  else
    {
      g_return_if_fail (session_keyboard->key_count[key] > 0);
      session_keyboard->key_count[key]--;
    }

  if (session_keyboard->key_count[key] < 0)
    {
      g_warning ("Key count for 0x%x reached below zero, ignoring", key);
      return;
    }

  if (session_keyboard->key_count[key] > 1)
    return;

  mdk_dbus_remote_desktop_session_call_notify_keyboard_keycode (
    keyboard->session_proxy,
    key, state,
    NULL, NULL, NULL);
}
