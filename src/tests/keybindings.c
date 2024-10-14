/*
 * Copyright (C) 2024 Red Hat Inc.
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

#include <linux/input-event-codes.h>

#include "backends/meta-backend-private.h"
#include "meta/keybindings.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"

static MetaContext *test_context;

static void
test_handler (MetaDisplay        *display,
              MetaWindow         *window,
              const ClutterEvent *event,
              MetaKeyBinding     *binding,
              gpointer            user_data)
{
  gboolean *triggered = user_data;

  *triggered = TRUE;
}

static gboolean
set_true_cb (gpointer user_data)
{
  gboolean *done = user_data;

  *done = TRUE;

  return G_SOURCE_REMOVE;
}

static void
test_keybinding_remove_trigger (void)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat;
  g_autoptr (GSettings) settings = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  gboolean triggered = FALSE;
  gboolean done = FALSE;

  seat = meta_backend_get_default_seat (backend);
  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  settings = g_settings_new ("org.gnome.mutter.test");
  meta_display_add_keybinding (display,
                               "test-binding",
                               settings,
                               META_KEY_BINDING_NONE,
                               test_handler,
                               &triggered, NULL);

  g_idle_add_full (G_PRIORITY_LOW,
                   set_true_cb,
                   &done,
                   NULL);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_T,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_T,
                                           CLUTTER_KEY_STATE_RELEASED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_RELEASED);

  while (!triggered)
    g_main_context_iteration (NULL, TRUE);

  meta_display_remove_keybinding (display, "test-binding");

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_T,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_T,
                                           CLUTTER_KEY_STATE_RELEASED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_RELEASED);

  meta_flush_input (test_context);
  while (g_main_context_iteration (NULL, FALSE)) {}
}

static void
init_tests (void)
{
  g_test_add_func ("/core/keybindings/remove-trigger", test_keybinding_remove_trigger);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;
  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
