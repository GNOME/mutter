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

#include "backends/meta-backend-private.h"
#include "tests/meta-test/meta-context-test.h"

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
meta_test_native_keyboard_map_set_async (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  struct xkb_keymap *xkb_keymap;
  struct xkb_keymap *new_xkb_keymap;
  gboolean done = FALSE;

  xkb_keymap = xkb_keymap_ref (meta_backend_get_keymap (backend));
  g_assert_cmpuint (xkb_keymap_num_layouts (xkb_keymap), ==, 1);
  g_assert_cmpstr (xkb_keymap_layout_get_name (xkb_keymap, 0),
                   ==,
                   "English (US)");

  meta_backend_set_keymap_async (backend,
                                 "us",
                                 "dvorak-alt-intl",
                                 NULL, NULL, NULL,
                                 set_keymap_cb, &done);

  g_assert_true (xkb_keymap == meta_backend_get_keymap (backend));

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  new_xkb_keymap = meta_backend_get_keymap (backend);
  g_assert_true (new_xkb_keymap != xkb_keymap);
  g_assert_cmpuint (xkb_keymap_num_layouts (new_xkb_keymap), ==, 1);
  g_assert_cmpstr (xkb_keymap_layout_get_name (new_xkb_keymap, 0),
                   ==,
                   "English (Dvorak, alt. intl.)");

  xkb_keymap_unref (xkb_keymap);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/keyboard-map/set-async",
                   meta_test_native_keyboard_map_set_async);
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
