/*
 * Copyright (C) 2019-2026 Red Hat, Inc.
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
 */

#include "config.h"

#include "tests/meta-wayland-test-runner.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-window-wayland.h"

static const MetaWaylandSurface *
get_surface_from_window (const char *title)
{
  MetaWindow *window;
  MetaWaylandSurface *surface;

  window = meta_find_client_window (test_context, "color-representation");
  g_assert_nonnull (window);
  surface = meta_window_get_wayland_surface (window);
  g_assert_nonnull (surface);
  return surface;
}

static void
wait_for_sync_point (unsigned int sync_point)
{
  meta_wayland_test_driver_wait_for_sync_point (test_driver, sync_point);
}

static void
emit_sync_event (unsigned int sync_point)
{
  meta_wayland_test_driver_emit_sync_event (test_driver, sync_point);
}

static void
color_representation_state (void)
{
  MetaWaylandTestClient *wayland_test_client;
  const MetaWaylandSurface *surface;

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "color-representation",
                                            "state",
                                            NULL);

  wait_for_sync_point (0);
  surface = get_surface_from_window ("color-representation");
  g_assert_cmpint (surface->committed_state.premult, ==,
                   META_MULTI_TEXTURE_ALPHA_MODE_NONE);
  g_assert_cmpint (surface->committed_state.coeffs, ==,
                   META_MULTI_TEXTURE_COEFFICIENTS_NONE);
  emit_sync_event (0);

  wait_for_sync_point (1);
  g_assert_cmpint (surface->committed_state.premult, ==,
                   META_MULTI_TEXTURE_ALPHA_MODE_STRAIGHT);
  g_assert_cmpint (surface->committed_state.coeffs, ==,
                   META_MULTI_TEXTURE_COEFFICIENTS_BT709_LIMITED);
  emit_sync_event (1);

  wait_for_sync_point (2);
  g_assert_cmpint (surface->committed_state.premult, ==,
                   META_MULTI_TEXTURE_ALPHA_MODE_STRAIGHT);
  g_assert_cmpint (surface->committed_state.coeffs, ==,
                   META_MULTI_TEXTURE_COEFFICIENTS_BT709_LIMITED);
  emit_sync_event (2);

  wait_for_sync_point (3);
  g_assert_cmpint (surface->committed_state.premult, ==,
                   META_MULTI_TEXTURE_ALPHA_MODE_NONE);
  g_assert_cmpint (surface->committed_state.coeffs, ==,
                   META_MULTI_TEXTURE_COEFFICIENTS_NONE);
  emit_sync_event (3);

  meta_wayland_test_client_finish (wayland_test_client);
}

static void
color_representation_bad_state (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "color-representation",
                                            "bad-state",
                                            NULL);
  /* we wait for the window to flush out all the messages */
  meta_wait_for_client_window (test_context, "color-representation");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
color_representation_bad_state2 (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "color-representation",
                                            "bad-state-2",
                                            NULL);
  /* we wait for the window to flush out all the messages */
  meta_wait_for_client_window (test_context, "color-representation");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
color_representation_premult_reftest (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "color-representation",
                                            "premult-reftest",
                                            NULL);
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/color-representation/state",
                   color_representation_state);
  g_test_add_func ("/wayland/color-representation/bad-state",
                   color_representation_bad_state);
  g_test_add_func ("/wayland/color-representation/bad-state2",
                   color_representation_bad_state2);
  g_test_add_func ("/wayland/color-representation/premult-reftest",
                   color_representation_premult_reftest);
}

int
main (int   argc,
      char *argv[])
{
  return meta_run_wayland_tests (argc, argv, init_tests);
}
