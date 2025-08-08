/*
 * Copyright (C) 2024 SUSE Software Solutions Germany GmbH
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
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written by:
 *     Joan Torres <joan.torres@suse.com>
 */

#include "config.h"

#include <glib/gstdio.h>
#include <stdint.h>

#include "backends/meta-virtual-monitor.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland-surface-private.h"

#define TEST_COLOR_EPSILON 0.0001f

static MetaContext *test_context;
static MetaVirtualMonitor *virtual_monitor;
static MetaWaylandTestDriver *test_driver;

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

static ClutterColorState *
get_window_color_state (MetaWindow *window)
{
  MetaWaylandSurface *surface;
  MetaSurfaceActor *surface_actor;

  surface = meta_window_get_wayland_surface (window);

  if (surface->color_state)
    return surface->color_state;

  surface_actor = meta_wayland_surface_get_actor (surface);

  return clutter_actor_get_color_state (CLUTTER_ACTOR (surface_actor));
}

static void
color_management (void)
{
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *test_window;
  ClutterColorState *color_state;
  ClutterColorStateParams *color_state_params;
  ClutterColorStateIcc *color_state_icc;
  const ClutterColorimetry *colorimetry;
  const ClutterEOTF *eotf;
  const ClutterLuminance *lum;
  const MtkAnonymousFile *file;

  wayland_test_client = meta_wayland_test_client_new (test_context,
                                                      "color-management");

  test_window = meta_wait_for_client_window (test_context, "color-management");

  wait_for_sync_point (0);
  color_state = get_window_color_state (test_window);
  color_state_params = CLUTTER_COLOR_STATE_PARAMS (color_state);
  colorimetry = clutter_color_state_params_get_colorimetry (color_state_params);
  g_assert_cmpuint (colorimetry->type, ==, CLUTTER_COLORIMETRY_TYPE_COLORSPACE);
  g_assert_cmpuint (colorimetry->colorspace, ==, CLUTTER_COLORSPACE_SRGB);
  eotf = clutter_color_state_params_get_eotf (color_state_params);
  g_assert_cmpuint (eotf->type, ==, CLUTTER_EOTF_TYPE_NAMED);
  g_assert_cmpuint (eotf->tf_name, ==, CLUTTER_TRANSFER_FUNCTION_SRGB);
  lum = clutter_color_state_params_get_luminance (color_state_params);
  g_assert_cmpuint (lum->type, ==, CLUTTER_LUMINANCE_TYPE_DERIVED);
  g_assert_cmpuint (lum->ref_is_1_0, ==, FALSE);
  emit_sync_event (0);

  wait_for_sync_point (1);
  color_state = get_window_color_state (test_window);
  color_state_params = CLUTTER_COLOR_STATE_PARAMS (color_state);
  colorimetry = clutter_color_state_params_get_colorimetry (color_state_params);
  g_assert_cmpuint (colorimetry->type, ==, CLUTTER_COLORIMETRY_TYPE_COLORSPACE);
  g_assert_cmpuint (colorimetry->colorspace, ==, CLUTTER_COLORSPACE_BT2020);
  eotf = clutter_color_state_params_get_eotf (color_state_params);
  g_assert_cmpuint (eotf->type, ==, CLUTTER_EOTF_TYPE_NAMED);
  g_assert_cmpuint (eotf->tf_name, ==, CLUTTER_TRANSFER_FUNCTION_PQ);
  lum = clutter_color_state_params_get_luminance (color_state_params);
  g_assert_cmpuint (lum->type, ==, CLUTTER_LUMINANCE_TYPE_EXPLICIT);
  g_assert_cmpuint (lum->ref_is_1_0, ==, FALSE);
  g_assert_cmpfloat_with_epsilon (lum->min, 0.005f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (lum->max, lum->min + 10000.0f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (lum->ref, 303.0f, TEST_COLOR_EPSILON);
  emit_sync_event (1);

  wait_for_sync_point (2);
  color_state = get_window_color_state (test_window);
  color_state_params = CLUTTER_COLOR_STATE_PARAMS (color_state);
  colorimetry = clutter_color_state_params_get_colorimetry (color_state_params);
  g_assert_cmpuint (colorimetry->type, ==, CLUTTER_COLORIMETRY_TYPE_COLORSPACE);
  g_assert_cmpuint (colorimetry->colorspace, ==, CLUTTER_COLORSPACE_SRGB);
  eotf = clutter_color_state_params_get_eotf (color_state_params);
  g_assert_cmpuint (eotf->type, ==, CLUTTER_EOTF_TYPE_NAMED);
  g_assert_cmpuint (eotf->tf_name, ==, CLUTTER_TRANSFER_FUNCTION_SRGB);
  lum = clutter_color_state_params_get_luminance (color_state_params);
  g_assert_cmpuint (lum->type, ==, CLUTTER_LUMINANCE_TYPE_EXPLICIT);
  g_assert_cmpuint (lum->ref_is_1_0, ==, FALSE);
  g_assert_cmpfloat_with_epsilon (lum->min, 0.2f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (lum->max, 80.0f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (lum->ref, 70.0f, TEST_COLOR_EPSILON);
  emit_sync_event (2);

  wait_for_sync_point (3);
  color_state = get_window_color_state (test_window);
  color_state_params = CLUTTER_COLOR_STATE_PARAMS (color_state);
  colorimetry = clutter_color_state_params_get_colorimetry (color_state_params);
  g_assert_cmpuint (colorimetry->type, ==, CLUTTER_COLORIMETRY_TYPE_PRIMARIES);
  g_assert_cmpfloat_with_epsilon (colorimetry->primaries->r_x, 0.64f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (colorimetry->primaries->r_y, 0.33f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (colorimetry->primaries->g_x, 0.30f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (colorimetry->primaries->g_y, 0.60f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (colorimetry->primaries->b_x, 0.15f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (colorimetry->primaries->b_y, 0.06f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (colorimetry->primaries->w_x, 0.34567f, TEST_COLOR_EPSILON);
  g_assert_cmpfloat_with_epsilon (colorimetry->primaries->w_y, 0.35850f, TEST_COLOR_EPSILON);
  eotf = clutter_color_state_params_get_eotf (color_state_params);
  g_assert_cmpuint (eotf->type, ==, CLUTTER_EOTF_TYPE_GAMMA);
  g_assert_cmpfloat_with_epsilon (eotf->gamma_exp, 2.5f, TEST_COLOR_EPSILON);
  lum = clutter_color_state_params_get_luminance (color_state_params);
  g_assert_cmpuint (lum->type, ==, CLUTTER_LUMINANCE_TYPE_DERIVED);
  g_assert_cmpuint (lum->ref_is_1_0, ==, FALSE);
  emit_sync_event (3);

  wait_for_sync_point (4);
  color_state = get_window_color_state (test_window);
  g_assert_true (CLUTTER_IS_COLOR_STATE_ICC (color_state));
  color_state_icc = CLUTTER_COLOR_STATE_ICC (color_state);

  file = clutter_color_state_icc_get_file (color_state_icc);
  g_assert_nonnull (file);
  emit_sync_event (4);

  meta_wayland_test_client_finish (wayland_test_client);
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);

  test_driver = meta_wayland_test_driver_new (compositor);

  virtual_monitor = meta_create_test_monitor (test_context,
                                              640, 480, 60.0);
}

static void
on_after_tests (void)
{
  g_clear_object (&virtual_monitor);
  g_clear_object (&test_driver);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/color-management",
                   color_management);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  g_setenv ("MUTTER_DEBUG_COLOR_MANAGEMENT_PROTOCOL", "1", TRUE);

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
