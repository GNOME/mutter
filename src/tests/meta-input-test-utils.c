/*
 * Copyright (C) 2025 Red Hat Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "tests/meta-input-test-utils.h"

#include <glib.h>
#include <gudev/gudev.h>
#include <libevdev/libevdev-uinput.h>
#include <linux/input-event-codes.h>
#include <stdio.h>

struct libevdev_uinput *
meta_create_test_keyboard (void)
{
  struct libevdev *evdev = NULL;
  struct libevdev_uinput *evdev_uinput = NULL;
  const int keyboard_keys[] = {
    KEY_ESC, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_0, KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE, KEY_TAB, KEY_Q, KEY_W, KEY_E,
    KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_LEFTBRACE,
    KEY_RIGHTBRACE, KEY_ENTER, KEY_LEFTCTRL, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G,
    KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE,
    KEY_LEFTSHIFT, KEY_BACKSLASH, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N,
    KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_RIGHTSHIFT, KEY_KPASTERISK,
    KEY_LEFTALT, KEY_SPACE, KEY_CAPSLOCK, KEY_F1, KEY_F2, KEY_F3, KEY_F4,
    KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_NUMLOCK,
    KEY_SCROLLLOCK, KEY_KP7, KEY_KP8, KEY_KP9, KEY_KPMINUS, KEY_KP4, KEY_KP5,
    KEY_KP6, KEY_KPPLUS, KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP0, KEY_KPDOT,
  };
  int i;
  int ret;

  evdev = libevdev_new ();
  g_assert_nonnull (evdev);
  libevdev_set_name (evdev, "Test keyboard");

  libevdev_enable_event_type (evdev, EV_KEY);
  for (i = 0; i < G_N_ELEMENTS (keyboard_keys); i++)
    libevdev_enable_event_code (evdev, EV_KEY, keyboard_keys[i], NULL);

  ret = libevdev_uinput_create_from_device (evdev,
                                            LIBEVDEV_UINPUT_OPEN_MANAGED,
                                            &evdev_uinput);

  if (ret == -EACCES)
    {
      g_printerr ("Test skipped: uinput permission denied\n");
      exit (77);
    }

  g_assert_cmpint (ret, ==, 0);
  g_assert_nonnull (evdev_uinput);
  libevdev_free (evdev);

  return evdev_uinput;
}

struct libevdev_uinput *
meta_create_test_mouse (void)
{
  struct libevdev *evdev = NULL;
  struct libevdev_uinput *evdev_uinput = NULL;
  const int mouse_buttons[] = {
    BTN_MOUSE, BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA,
    BTN_FORWARD, BTN_BACK,
  };
  int i;
  int ret;

  evdev = libevdev_new ();
  g_assert_nonnull (evdev);
  libevdev_set_name (evdev, "Test mouse");

  libevdev_enable_event_type (evdev, EV_KEY);
  for (i = 0; i < G_N_ELEMENTS (mouse_buttons); i++)
    libevdev_enable_event_code (evdev, EV_KEY, mouse_buttons[i], NULL);
  libevdev_enable_event_type (evdev, EV_REL);
  libevdev_enable_event_code (evdev, EV_REL, REL_X, NULL);
  libevdev_enable_event_code (evdev, EV_REL, REL_Y, NULL);
  libevdev_enable_event_type (evdev, EV_SYN);

  ret = libevdev_uinput_create_from_device (evdev,
                                            LIBEVDEV_UINPUT_OPEN_MANAGED,
                                            &evdev_uinput);
  g_assert_cmpint (ret, ==, 0);
  g_assert_nonnull (evdev_uinput);
  libevdev_free (evdev);

  return evdev_uinput;
}

void
meta_wait_for_uinput_device (struct libevdev_uinput *evdev_uinput)
{
  g_autoptr (GUdevClient) client = NULL;
  const char *subsystems[] = { "input", NULL };

  client = g_udev_client_new (subsystems);

  while (TRUE)
    {
      g_autolist (GUdevDevice) devices = NULL;
      g_autoptr (GUdevEnumerator) enumerator = NULL;
      GList *l;

      enumerator = g_udev_enumerator_new (client);
      g_udev_enumerator_add_match_subsystem (enumerator, "input");

      devices = g_udev_enumerator_execute (enumerator);

      for (l = devices; l; l = l->next)
        {
          GUdevDevice *device = G_UDEV_DEVICE (l->data);

          if (g_strcmp0 (libevdev_uinput_get_devnode (evdev_uinput),
                         g_udev_device_get_device_file (device)) == 0)
            return;

          usleep (200);
        }
    }
}
