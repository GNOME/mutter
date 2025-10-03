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
#include "backends/meta-keymap-description-private.h"
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
set_true_cb (gboolean *value)
{
  *value = TRUE;
}

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
set_keymap_expect_error_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  MetaBackend *backend = META_BACKEND (source_object);
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;

  g_assert_false (meta_backend_set_keymap_finish (backend, result, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);

  *done = TRUE;
}

static void
reset_keymap_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  MetaBackend *backend = META_BACKEND (source_object);
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;

  g_assert_true (meta_backend_reset_keymap_finish (backend, result, &error));
  g_assert_no_error (error);

  *done = TRUE;
}

static void
reset_keymap_expect_error_cb (GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  MetaBackend *backend = META_BACKEND (source_object);
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;

  g_assert_false (meta_backend_reset_keymap_finish (backend, result, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);

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
on_keymap_changed2 (MetaBackend           *backend,
                    MetaKeymapDescription *expected_keymap_description)
{
  g_assert_true (expected_keymap_description ==
                 meta_backend_get_keymap_description (backend));
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
  g_autoptr (MetaKeymapDescription) keymap_description = NULL;
  struct xkb_keymap *new_xkb_keymap;
  gboolean done = FALSE;
  gpointer expected_next_handler;
  gulong await_mod_mask_handler_id;
  gulong keymap_changed_handler_id;
  gulong keymap_changed_handler_id2;
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
  keymap_description =
    meta_keymap_description_new_from_rules (NULL,
                                            "us",
                                            "dvorak-alt-intl",
                                            NULL,
                                            NULL,
                                            NULL);

  keymap_changed_handler_id2 =
    g_signal_connect (backend,
                      "keymap-changed",
                      G_CALLBACK (on_keymap_changed2),
                      keymap_description);

  meta_backend_set_keymap_async (backend, keymap_description, 0,
                                 NULL, set_keymap_cb, &done);

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
  g_signal_handler_disconnect (backend, keymap_changed_handler_id2);
  g_signal_handler_disconnect (keymap, keymap_state_changed_handler_id);
}

static void
meta_test_native_keyboard_map_change_layout (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  struct xkb_keymap *xkb_keymap = meta_backend_get_keymap (backend);
  g_autoptr (MetaKeymapDescription) keymap_description = NULL;
  struct xkb_keymap *new_xkb_keymap;
  gboolean done = FALSE;

  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  xkb_keymap_ref (xkb_keymap);

  keymap_description =
    meta_keymap_description_new_from_rules (NULL,
                                            "us,ua",
                                            NULL,
                                            "grp:caps_select",
                                            NULL,
                                            NULL);
  meta_backend_set_keymap_async (backend, keymap_description, 0,
                                 NULL, set_keymap_cb, &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  new_xkb_keymap = meta_backend_get_keymap (backend);
  g_assert_true (new_xkb_keymap != xkb_keymap);
  g_assert_cmpuint (xkb_keymap_num_layouts (new_xkb_keymap), ==, 2);
  g_assert_cmpstr (xkb_keymap_layout_get_name (new_xkb_keymap, 0),
                   ==,
                   "English (US)");
  g_assert_cmpstr (xkb_keymap_layout_get_name (new_xkb_keymap, 1),
                   ==,
                   "Ukrainian");

  xkb_keymap_unref (xkb_keymap);

  /* Test layout switching with Caps Lock */
  /* First verify we start with layout 0 (English US) */
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 0);

  /* Press Shift key */
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTSHIFT,
                                           CLUTTER_KEY_STATE_PRESSED);

  /* Press Caps Lock while Shift is held (Shift+Caps Lock) */
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_CAPSLOCK,
                                           CLUTTER_KEY_STATE_PRESSED);

  /* Release Caps Lock */
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_CAPSLOCK,
                                           CLUTTER_KEY_STATE_RELEASED);

  /* Release Shift key */
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTSHIFT,
                                           CLUTTER_KEY_STATE_RELEASED);
  meta_flush_input (test_context);
  meta_wait_for_update (test_context);

  /* Verify that layout switched to Ukrainian (layout 1) */
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 1);
}

static void
set_keymap_layout_group_cb (GObject      *source_object,
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
trigger_error (const char *message)
{
  g_error ("%s", message);
}

static void
meta_test_native_keyboard_map_set_layout_index (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  g_autoptr (MetaKeymapDescription) keymap_description = NULL;
  gboolean done = FALSE;
  struct xkb_keymap *keymap;
  gulong keymap_changed_handler_id;

  keymap_description =
    meta_keymap_description_new_from_rules (NULL,
                                            "us,se",
                                            "dvorak-alt-intl,svdvorak",
                                            NULL,
                                            NULL,
                                            NULL);
  meta_backend_set_keymap_async (backend, keymap_description, 0,
                                 NULL, set_keymap_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  keymap_changed_handler_id =
    g_signal_connect_swapped (backend,
                              "keymap-changed",
                              G_CALLBACK (trigger_error),
                              (gpointer) "Unexpected keymap-changed emission");

  done = FALSE;
  meta_backend_set_keymap_async (backend, keymap_description, 0,
                                 NULL, set_keymap_layout_group_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  keymap = meta_backend_get_keymap (backend);
  g_assert_cmpuint (xkb_keymap_num_layouts (keymap), ==, 2);
  g_assert_cmpstr (xkb_keymap_layout_get_name (keymap, 0),
                   ==,
                   "English (Dvorak, alt. intl.)");
  g_assert_cmpstr (xkb_keymap_layout_get_name (keymap, 1),
                   ==,
                   "Swedish (Svdvorak)");

  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 0);
  done = FALSE;
  meta_backend_set_keymap_async (backend, keymap_description, 1,
                                 NULL, set_keymap_layout_group_cb, &done);
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 0);
  while (!done)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 1);

  g_signal_handler_disconnect (backend, keymap_changed_handler_id);
}

static void
meta_test_native_keyboard_map_lock_layout (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  g_autoptr (MetaKeymapDescription) keymap_description1 = NULL;
  g_autoptr (MetaKeymapDescription) keymap_description2 = NULL;
  g_autoptr (MetaKeymapDescription) keymap_description3 = NULL;
  MetaKeymapDescriptionOwner *owner;
  gboolean done = FALSE;
  gboolean was_signalled;
  struct xkb_keymap *keymap;
  gulong keymap_changed_handler_id;
  gulong keymap_layout_group_changed_handler_id;

  owner = meta_keymap_description_owner_new ();

  /*
   * Set a locking keymap.
   */

  keymap_description1 =
    meta_keymap_description_new_from_rules (NULL,
                                            "us,se",
                                            "dvorak-alt-intl,svdvorak",
                                            NULL,
                                            NULL,
                                            NULL);
  meta_keymap_description_lock (keymap_description1, owner);
  meta_backend_set_keymap_async (backend, keymap_description1, 0,
                                 NULL, set_keymap_cb, &done);
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

  /*
   * Set a new keymap without an owner. Should cause an error and not take
   * effect.
   */

  keymap_changed_handler_id =
    g_signal_connect_swapped (backend,
                              "keymap-changed",
                              G_CALLBACK (trigger_error),
                              (gpointer) "Unexpected keymap-changed emission");

  keymap_description2 =
    meta_keymap_description_new_from_rules (NULL,
                                            "se,us",
                                            "svdvorak,dvorak-alt-intl",
                                            NULL,
                                            NULL,
                                            NULL);
  done = FALSE;
  meta_backend_set_keymap_async (backend, keymap_description2, 0,
                                 NULL, set_keymap_expect_error_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  g_assert_true (keymap == meta_backend_get_keymap (backend));

  /*
   * Set the same keymap with a different layout index. Should take effect.
   */

  was_signalled = FALSE;
  keymap_layout_group_changed_handler_id =
    g_signal_connect_swapped (backend,
                              "keymap-layout-group-changed",
                              G_CALLBACK (set_true_cb),
                              &was_signalled);

  done = FALSE;
  meta_backend_set_keymap_async (backend, keymap_description1, 1,
                                 NULL, set_keymap_layout_group_cb, &done);
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 0);
  while (!done)
    g_main_context_iteration (NULL, TRUE);
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 1);
  g_assert_true (was_signalled);

  xkb_keymap_unref (keymap);
  g_signal_handler_disconnect (backend, keymap_changed_handler_id);
  g_signal_handler_disconnect (backend, keymap_layout_group_changed_handler_id);

  /*
   * Set another keymap with the same owner. Should take effect.
   */

  keymap_description3 =
    meta_keymap_description_new_from_rules (NULL,
                                            "ua",
                                            "",
                                            NULL,
                                            NULL,
                                            NULL);
  meta_keymap_description_unlock (keymap_description3, owner);
  done = FALSE;
  meta_backend_set_keymap_async (backend, keymap_description3, 0,
                                 NULL, set_keymap_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  keymap = meta_backend_get_keymap (backend);
  g_assert_cmpuint (xkb_keymap_num_layouts (keymap), ==, 1);
  g_assert_cmpstr (xkb_keymap_layout_get_name (keymap, 0),
                   ==,
                   "Ukrainian");
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 0);

  /*
   * Set keymap again without owner. Should take effect.
   */

  done = FALSE;
  meta_backend_set_keymap_async (backend, keymap_description2, 0,
                                 NULL, set_keymap_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  keymap = meta_backend_get_keymap (backend);
  g_assert_cmpuint (xkb_keymap_num_layouts (keymap), ==, 2);
  g_assert_cmpstr (xkb_keymap_layout_get_name (keymap, 0),
                   ==,
                   "Swedish (Svdvorak)");
  g_assert_cmpstr (xkb_keymap_layout_get_name (keymap, 1),
                   ==,
                   "English (Dvorak, alt. intl.)");
  g_assert_cmpuint (meta_backend_get_keymap_layout_group (backend), ==, 0);

  meta_keymap_description_owner_unref (owner);
}

static MetaKeymapDescription *
on_reset_keymap_description (MetaBackend           *backend,
                             MetaKeymapDescription *keymap_description)
{
  return meta_keymap_description_ref (keymap_description);
}

static void
meta_test_native_keyboard_map_lock_layout_reset (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  g_autoptr (MetaKeymapDescription) keymap_description1 = NULL;
  g_autoptr (MetaKeymapDescription) keymap_description2 = NULL;
  MetaKeymapDescriptionOwner *owner;
  MetaKeymapDescriptionOwner *other_owner;
  gboolean done = FALSE;
  gboolean was_signalled = FALSE;
  gulong keymap_changed_handler_id;
  gulong reset_keymap_handler_id;

  owner = meta_keymap_description_owner_new ();
  other_owner = meta_keymap_description_owner_new ();

  keymap_description1 =
    meta_keymap_description_new_from_rules (NULL,
                                            "us,se",
                                            "dvorak-alt-intl,svdvorak",
                                            NULL,
                                            NULL,
                                            NULL);
  meta_keymap_description_lock (keymap_description1, owner);
  meta_backend_set_keymap_async (backend, keymap_description1, 0,
                                 NULL, set_keymap_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  keymap_changed_handler_id =
    g_signal_connect_swapped (backend,
                              "keymap-changed",
                              G_CALLBACK (trigger_error),
                              (gpointer) "Unexpected keymap-changed emission");

  keymap_description2 =
    meta_keymap_description_new_from_rules (NULL,
                                            "se,us",
                                            "svdvorak,dvorak-alt-intl",
                                            NULL,
                                            NULL,
                                            NULL);
  done = FALSE;
  meta_backend_set_keymap_async (backend, keymap_description2, 0,
                                 NULL, set_keymap_expect_error_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  reset_keymap_handler_id =
    g_signal_connect (backend,
                      "reset-keymap-description",
                      G_CALLBACK (on_reset_keymap_description),
                      keymap_description2);

  done = FALSE;
  meta_backend_reset_keymap_async (backend, other_owner,
                                   NULL, reset_keymap_expect_error_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (backend, keymap_changed_handler_id);
  keymap_changed_handler_id =
    g_signal_connect_swapped (backend,
                              "keymap-changed",
                              G_CALLBACK (set_true_cb),
                              &was_signalled);

  done = FALSE;
  meta_backend_reset_keymap_async (backend, owner,
                                   NULL, reset_keymap_cb, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);

  g_assert_true (meta_backend_get_keymap_description (backend) ==
                 keymap_description2);

  g_assert_true (was_signalled);

  g_signal_handler_disconnect (backend, keymap_changed_handler_id);
  g_signal_handler_disconnect (backend, reset_keymap_handler_id);
  meta_keymap_description_owner_unref (owner);
  meta_keymap_description_owner_unref (other_owner);
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
  g_test_add_func ("/backends/native/keyboard-map/change-layout",
                   meta_test_native_keyboard_map_change_layout);
  g_test_add_func ("/backends/native/keyboard-map/set-layout-index",
                   meta_test_native_keyboard_map_set_layout_index);
  g_test_add_func ("/backends/native/keyboard-map/lock-layout",
                   meta_test_native_keyboard_map_lock_layout);
  g_test_add_func ("/backends/native/keyboard-map/lock-layout-reset",
                   meta_test_native_keyboard_map_lock_layout_reset);
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
