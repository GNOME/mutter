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

#include <linux/input-event-codes.h>

#include "backends/meta-backend-private.h"
#include "backends/native/meta-keymap-native.h"
#include "backends/native/meta-seat-native.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-test/meta-context-test.h"

typedef struct
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;
} ModMaskTuple;

static MetaContext *test_context;

static void
set_keymap_cb (GObject      *source_object,
               GAsyncResult *result,
               gpointer      user_data)
{
  MetaBackend *backend = META_BACKEND (source_object);
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;

  g_assert_true (meta_backend_set_keymap_finish (backend, result, &error));
  g_assert_no_error (error);

  *done = TRUE;
}

static void
await_mod_mask (MetaKeymapNative  *keymap_native,
                ModMaskTuple     **awaited_mod_mask)
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;

  if (!*awaited_mod_mask)
    return;

  clutter_keymap_get_modifier_state (CLUTTER_KEYMAP (keymap_native),
                                     &depressed_mods,
                                     &latched_mods,
                                     &locked_mods);

  if (depressed_mods == (*awaited_mod_mask)->depressed_mods &&
      latched_mods == (*awaited_mod_mask)->latched_mods &&
      locked_mods == (*awaited_mod_mask)->locked_mods)
    *awaited_mod_mask = NULL;
}

static void
on_keymap_state_changed (MetaKeymapNative *keymap_native,
                         gpointer         *expected_next_handler)
{
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;

  clutter_keymap_get_modifier_state (CLUTTER_KEYMAP (keymap_native),
                                     &depressed_mods,
                                     &latched_mods,
                                     &locked_mods);
  g_assert_true (*expected_next_handler == on_keymap_state_changed);

  *expected_next_handler = NULL;
}

static void
on_keymap_changed (MetaBackend *backend,
                   gpointer    *expected_next_handler)
{
  g_assert_true (*expected_next_handler == on_keymap_changed);

  *expected_next_handler = on_keymap_state_changed;
}

static void
meta_test_native_keyboard_map_set_async (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  ClutterKeymap *keymap = clutter_seat_get_keymap (seat);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  struct xkb_keymap *xkb_keymap = meta_backend_get_keymap (backend);
  xkb_mod_mask_t alt_mask =
    1 << xkb_keymap_mod_get_index (xkb_keymap, XKB_MOD_NAME_ALT);
  ModMaskTuple expected_mods = { alt_mask, 0, 0 };
  ModMaskTuple *expected_mods_ptr = &expected_mods;
  struct xkb_keymap *new_xkb_keymap;
  gboolean done = FALSE;
  gpointer expected_next_handler;
  gulong await_mod_mask_handler_id;
  gulong keymap_changed_handler_id;
  gulong keymap_state_changed_handler_id;

  await_mod_mask_handler_id =
    g_signal_connect (keymap,
                      "state-changed",
                      G_CALLBACK (await_mod_mask),
                      &expected_mods_ptr);
  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTALT,
                                           CLUTTER_KEY_STATE_PRESSED);
  while (expected_mods_ptr)
    g_main_context_iteration (NULL, TRUE);

  meta_flush_input (test_context);
  meta_wait_for_update (test_context);

  g_signal_handler_disconnect (keymap, await_mod_mask_handler_id);

  xkb_keymap_ref (xkb_keymap);
  g_assert_cmpuint (xkb_keymap_num_layouts (xkb_keymap), ==, 1);
  g_assert_cmpstr (xkb_keymap_layout_get_name (xkb_keymap, 0),
                   ==,
                   "English (US)");

  keymap_changed_handler_id =
    g_signal_connect (backend,
                      "keymap-changed",
                      G_CALLBACK (on_keymap_changed),
                      &expected_next_handler);
  keymap_state_changed_handler_id =
    g_signal_connect (keymap,
                      "state-changed",
                      G_CALLBACK (on_keymap_state_changed),
                      &expected_next_handler);

  expected_next_handler = (gpointer) on_keymap_changed;
  meta_backend_set_keymap_async (backend,
                                 "us",
                                 "dvorak-alt-intl",
                                 NULL, NULL, NULL,
                                 set_keymap_cb, &done);

  g_assert_true (xkb_keymap == meta_backend_get_keymap (backend));

  while (!done || expected_next_handler)
    g_main_context_iteration (NULL, TRUE);

  new_xkb_keymap = meta_backend_get_keymap (backend);
  g_assert_true (new_xkb_keymap != xkb_keymap);
  g_assert_cmpuint (xkb_keymap_num_layouts (new_xkb_keymap), ==, 1);
  g_assert_cmpstr (xkb_keymap_layout_get_name (new_xkb_keymap, 0),
                   ==,
                   "English (Dvorak, alt. intl.)");

  xkb_keymap_unref (xkb_keymap);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTALT,
                                           CLUTTER_KEY_STATE_RELEASED);
  meta_flush_input (test_context);
  meta_wait_for_update (test_context);

  g_signal_handler_disconnect (backend, keymap_changed_handler_id);
  g_signal_handler_disconnect (keymap, keymap_state_changed_handler_id);
}

static void
set_keymap_layout_group_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  MetaBackend *backend = META_BACKEND (source_object);
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;

  g_assert_true (meta_backend_set_keymap_layout_group_finish (backend,
                                                              result,
                                                              &error));
  g_assert_no_error (error);

  *done = TRUE;
}

static void
meta_test_native_keyboard_map_set_layout_index (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  gboolean done = FALSE;
  struct xkb_keymap *keymap;

  meta_backend_set_keymap_async (backend,
                                 "us,se",
                                 "dvorak-alt-intl,svdvorak",
                                 NULL, NULL, NULL,
                                 set_keymap_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  keymap = xkb_keymap_ref (meta_backend_get_keymap (backend));
  g_assert_cmpuint (xkb_keymap_num_layouts (keymap), ==, 2);
  g_assert_cmpstr (xkb_keymap_layout_get_name (keymap, 0),
                   ==,
                   "English (Dvorak, alt. intl.)");
  g_assert_cmpstr (xkb_keymap_layout_get_name (keymap, 1),
                   ==,
                   "Swedish (Svdvorak)");

  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 0);
  done = FALSE;
  meta_backend_set_keymap_layout_group_async (backend, 1, NULL,
                                              set_keymap_layout_group_cb,
                                              &done);
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 0);
  while (!done)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 1);
}

static void
record_modifier_state (ClutterKeymap  *keymap,
                       ModMaskTuple  **expected_mods)
{
  MetaKeymapNative *keymap_native = META_KEYMAP_NATIVE (keymap);
  xkb_mod_mask_t depressed_mods;
  xkb_mod_mask_t latched_mods;
  xkb_mod_mask_t locked_mods;

  clutter_keymap_get_modifier_state (CLUTTER_KEYMAP (keymap_native),
                                     &depressed_mods,
                                     &latched_mods,
                                     &locked_mods);

  g_assert_cmpuint ((*expected_mods)->depressed_mods, ==, depressed_mods);
  g_assert_cmpuint ((*expected_mods)->latched_mods, ==, latched_mods);
  g_assert_cmpuint ((*expected_mods)->locked_mods, ==, locked_mods);

  (*expected_mods)++;
}

static void
meta_test_native_keyboard_map_modifiers (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  MetaSeatNative *seat_native = META_SEAT_NATIVE (seat);
  struct xkb_keymap *xkb_keymap =
    meta_seat_native_get_keyboard_map (seat_native);
  xkb_mod_mask_t shift_mask =
    1 << xkb_keymap_mod_get_index (xkb_keymap, XKB_MOD_NAME_SHIFT);
  xkb_mod_mask_t alt_mask =
    1 << xkb_keymap_mod_get_index (xkb_keymap, XKB_MOD_NAME_ALT);
  xkb_mod_mask_t num_mask =
    1 << xkb_keymap_mod_get_index (xkb_keymap, XKB_MOD_NAME_NUM);
  ClutterKeymap *keymap = clutter_seat_get_keymap (seat);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  ModMaskTuple expected_mods[] = {
    { shift_mask, 0, 0, },
    { shift_mask | alt_mask, 0, 0, },
    { alt_mask, 0, 0, },
    { 0, 0, 0, },
    { num_mask, 0, num_mask, },
    { 0, 0, num_mask, },
    { alt_mask, 0, num_mask, },
    { 0, 0, num_mask, },
    { num_mask, 0, num_mask, },
    { 0, 0, 0, },
  };
  ModMaskTuple *received_mods = expected_mods;
  gulong keymap_state_changed_handler_id;

  meta_flush_input (test_context);
  meta_wait_for_update (test_context);

  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);
  keymap_state_changed_handler_id =
    g_signal_connect (keymap,
                      "state-changed",
                      G_CALLBACK (record_modifier_state),
                      &received_mods);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTSHIFT,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTALT,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTSHIFT,
                                           CLUTTER_KEY_STATE_RELEASED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTALT,
                                           CLUTTER_KEY_STATE_RELEASED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_NUMLOCK,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_NUMLOCK,
                                           CLUTTER_KEY_STATE_RELEASED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTALT,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTALT,
                                           CLUTTER_KEY_STATE_RELEASED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_NUMLOCK,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_NUMLOCK,
                                           CLUTTER_KEY_STATE_RELEASED);

  while (received_mods < expected_mods + G_N_ELEMENTS (expected_mods))
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (keymap, keymap_state_changed_handler_id);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/keyboard-map/set-async",
                   meta_test_native_keyboard_map_set_async);
  g_test_add_func ("/backends/native/keyboard-map/set-layout-index",
                   meta_test_native_keyboard_map_set_layout_index);
  g_test_add_func ("/backends/native/keyboard-map/modifiers",
                   meta_test_native_keyboard_map_modifiers);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      META_CONTEXT_TEST_FLAG_NONE);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
