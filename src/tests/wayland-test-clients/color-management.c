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

#include <fcntl.h>
#include <sys/stat.h>

#include "color-management-v1-client-protocol.h"
#include "wayland-test-client-utils.h"

typedef struct _ImageDescriptionContext
{
  uint32_t image_description_id;
  gboolean creation_failed;
} ImageDescriptionContext;

typedef struct _Primaries
{
  float r_x, r_y;
  float g_x, g_y;
  float b_x, b_y;
  float w_x, w_y;
} Primaries;

static gboolean waiting_for_configure = FALSE;
static Primaries custom_primaries = {
  .r_x = 0.64f, .r_y = 0.33f,
  .g_x = 0.30f, .g_y = 0.60f,
  .b_x = 0.15f, .b_y = 0.06f,
  .w_x = 0.34567f, .w_y = 0.35850f,
};

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *states)
{
}

static void
handle_xdg_toplevel_close (void                *data,
                           struct xdg_toplevel *xdg_toplevel)
{
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
};

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{

  xdg_surface_ack_configure (xdg_surface, serial);

  waiting_for_configure = FALSE;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
wait_for_configure (WaylandDisplay *display)
{
  waiting_for_configure = TRUE;
  while (waiting_for_configure)
    wayland_display_dispatch (display);
}

static uint32_t
float_to_scaled_uint32_chromaticity (float value)
{
  return (uint32_t) (value * 1000000);
}

static uint32_t
float_to_scaled_uint32 (float value)
{
  return (uint32_t) (value * 10000);
}

static void
handle_image_description_failed (void                           *data,
                                 struct wp_image_description_v1 *image_description_v4,
                                 uint32_t                        cause,
                                 const char                     *msg)

{
  ImageDescriptionContext *image_description_context = data;

  image_description_context->creation_failed = TRUE;
}

static void
handle_image_description_ready (void                           *data,
                                struct wp_image_description_v1 *image_description_v4,
                                uint32_t                        identity)
{
  ImageDescriptionContext *image_description_context = data;

  image_description_context->image_description_id = identity;
}

static const struct wp_image_description_v1_listener image_description_listener = {
  handle_image_description_failed,
  handle_image_description_ready,
};

static void
wait_for_image_description_ready (ImageDescriptionContext *image_description,
                                  WaylandDisplay          *display)
{
  while (image_description->image_description_id == 0 &&
         !image_description->creation_failed)
    wayland_display_dispatch (display);
}

static void
create_image_description_from_params (WaylandDisplay                  *display,
                                      struct wp_image_description_v1 **image_description,
                                      int                              primaries_named,
                                      Primaries                       *primaries,
                                      int                              tf_named,
                                      float                            tf_power,
                                      float                            min_lum,
                                      float                            max_lum,
                                      float                            ref_lum)

{
  struct wp_image_description_creator_params_v1 *creator_params;
  ImageDescriptionContext image_description_context;

  creator_params =
    wp_color_manager_v1_create_parametric_creator (display->color_management_mgr);

  if (primaries_named != -1)
    wp_image_description_creator_params_v1_set_primaries_named (
      creator_params,
      primaries_named);

  if (primaries)
    wp_image_description_creator_params_v1_set_primaries (
      creator_params,
      float_to_scaled_uint32_chromaticity (primaries->r_x),
      float_to_scaled_uint32_chromaticity (primaries->r_y),
      float_to_scaled_uint32_chromaticity (primaries->g_x),
      float_to_scaled_uint32_chromaticity (primaries->g_y),
      float_to_scaled_uint32_chromaticity (primaries->b_x),
      float_to_scaled_uint32_chromaticity (primaries->b_y),
      float_to_scaled_uint32_chromaticity (primaries->w_x),
      float_to_scaled_uint32_chromaticity (primaries->w_y));

  if (tf_named != -1)
    wp_image_description_creator_params_v1_set_tf_named (
      creator_params,
      tf_named);

  if (tf_power >= 1.0f)
    wp_image_description_creator_params_v1_set_tf_power (
      creator_params,
      float_to_scaled_uint32 (tf_power));

  if (min_lum >= 0.0f && max_lum > 0.0f && ref_lum >= 0.0f)
    wp_image_description_creator_params_v1_set_luminances (
      creator_params,
      float_to_scaled_uint32 (min_lum),
      (uint32_t) max_lum,
      (uint32_t) ref_lum);

  image_description_context.image_description_id = 0;
  image_description_context.creation_failed = FALSE;

  *image_description =
    wp_image_description_creator_params_v1_create (creator_params);
  wp_image_description_v1_add_listener (
    *image_description,
    &image_description_listener,
    &image_description_context);

  wait_for_image_description_ready (&image_description_context, display);

  g_assert_false (image_description_context.creation_failed);
  g_assert_cmpint (image_description_context.image_description_id, >, 0);
}

static void
create_image_description_from_icc (WaylandDisplay                  *display,
                                   struct wp_image_description_v1 **image_description,
                                   const char                      *icc_path)

{
  struct wp_image_description_creator_icc_v1 *creator_icc;
  ImageDescriptionContext image_description_context;
  int32_t icc_fd;
  struct stat stat = { 0 };

  creator_icc =
    wp_color_manager_v1_create_icc_creator (display->color_management_mgr);

  icc_fd = open (icc_path, O_RDONLY);

  g_assert_cmpint (icc_fd, !=, -1);

  fstat (icc_fd, &stat);

  g_assert_cmpuint (stat.st_size, >, 0);

  wp_image_description_creator_icc_v1_set_icc_file (creator_icc,
                                                    icc_fd,
                                                    0,
                                                    stat.st_size);

  image_description_context.image_description_id = 0;
  image_description_context.creation_failed = FALSE;

  *image_description =
    wp_image_description_creator_icc_v1_create (creator_icc);
  wp_image_description_v1_add_listener (
    *image_description,
    &image_description_listener,
    &image_description_context);

  wait_for_image_description_ready (&image_description_context, display);

  g_assert_false (image_description_context.creation_failed);
  g_assert_cmpint (image_description_context.image_description_id, >, 0);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autofree char *icc_path = NULL;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_surface *xdg_surface;
  struct wl_surface *surface;
  struct wp_color_management_surface_v1 *color_surface;
  struct wp_image_description_v1 *image_description;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, display);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);
  xdg_toplevel_set_title (xdg_toplevel, "color-management");
  color_surface =
    wp_color_manager_v1_get_surface (display->color_management_mgr, surface);

  wl_surface_commit (surface);
  wait_for_configure (display);

  test_driver_sync_point (display->test_driver, 0, NULL);
  wait_for_sync_event (display, 0);

  create_image_description_from_params (display,
                                        &image_description,
                                        WP_COLOR_MANAGER_V1_PRIMARIES_BT2020,
                                        NULL,
                                        WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ,
                                        -1.0f,
                                        0.005f,
                                        10000.0f,
                                        303.0f);
  wp_color_management_surface_v1_set_image_description (
    color_surface,
    image_description,
    WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);

  wl_surface_commit (surface);

  wp_image_description_v1_destroy (image_description);

  test_driver_sync_point (display->test_driver, 1, NULL);
  wait_for_sync_event (display, 1);

  create_image_description_from_params (display,
                                        &image_description,
                                        WP_COLOR_MANAGER_V1_PRIMARIES_SRGB,
                                        NULL,
                                        WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB,
                                        -1.0f,
                                        0.2f,
                                        80.0f,
                                        70.0f);
  wp_color_management_surface_v1_set_image_description (
    color_surface,
    image_description,
    WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);

  wl_surface_commit (surface);

  wp_image_description_v1_destroy (image_description);

  test_driver_sync_point (display->test_driver, 2, NULL);
  wait_for_sync_event (display, 2);

  create_image_description_from_params (display,
                                        &image_description,
                                        -1,
                                        &custom_primaries,
                                        -1,
                                        2.5f,
                                        -1.0f,
                                        -1.0f,
                                        -1.0f);
  wp_color_management_surface_v1_set_image_description (
    color_surface,
    image_description,
    WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);

  wl_surface_commit (surface);

  wp_image_description_v1_destroy (image_description);

  test_driver_sync_point (display->test_driver, 3, NULL);
  wait_for_sync_event (display, 3);

  icc_path = g_build_filename (g_getenv ("G_TEST_SRCDIR"),
                               "icc-profiles",
                               "sRGB.icc",
                               NULL);

  create_image_description_from_icc (display, &image_description, icc_path);

  wp_color_management_surface_v1_set_image_description (
    color_surface,
    image_description,
    WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);

  wl_surface_commit (surface);

  wp_image_description_v1_destroy (image_description);

  test_driver_sync_point (display->test_driver, 4, NULL);
  wait_for_sync_event (display, 4);

  wp_color_management_surface_v1_destroy (color_surface);

  return EXIT_SUCCESS;
}
