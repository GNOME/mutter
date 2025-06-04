/*
 * Copyright (C) 2023 Red Hat, Inc.
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

#include <glib.h>

#include "wayland-test-client-utils.h"

static void
draw_main (WaylandDisplay *display,
           WaylandSurface *surface,
           uint32_t        format)
{
  WaylandBuffer *buffer;

  buffer = wayland_buffer_create (display, NULL,
                                  surface->width,
                                  surface->height,
                                  format,
                                  NULL, 0,
                                  GBM_BO_USE_LINEAR);

  if (!buffer)
    g_error ("Failed to create buffer");

  /* we leave the buffer empty, which is fine because we do not ref-test it */
  wl_surface_attach (surface->wl_surface,
                     wayland_buffer_get_wl_buffer (buffer),
                     0, 0);
}

int
main (int          argc,
      const char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;
  static struct wp_color_representation_surface_v1 *color_repr;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  surface = wayland_surface_new (display,
                                 "color-representation",
                                 100, 100, 0xffffffff);
  surface->manual_paint = TRUE;
  surface->has_alpha = TRUE;

  wl_surface_commit (surface->wl_surface);

  color_repr = wp_color_representation_manager_v1_get_surface (
    display->color_representation,
    surface->wl_surface);

  wait_for_window_configured (display, surface);

  if (g_strcmp0 (argv[1], "state") == 0)
    {
      draw_main (display, surface, DRM_FORMAT_YUV420);
      wayland_surface_commit (surface);
      wl_display_flush (display->display);

      test_driver_sync_point (display->test_driver, 0, NULL);
      wait_for_sync_event (display, 0);

      wp_color_representation_surface_v1_set_alpha_mode (
        color_repr,
        WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT);
      wp_color_representation_surface_v1_set_coefficients_and_range (
        color_repr,
        WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709,
        WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED);
      wp_color_representation_surface_v1_set_chroma_location (
        color_repr,
        WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_2);

      wl_surface_commit (surface->wl_surface);
      wl_display_flush (display->display);

      test_driver_sync_point (display->test_driver, 1, NULL);
      wait_for_sync_event (display, 1);

      wp_color_representation_surface_v1_destroy (color_repr);
      wl_display_flush (display->display);

      test_driver_sync_point (display->test_driver, 2, NULL);
      wait_for_sync_event (display, 2);

      wl_surface_commit (surface->wl_surface);
      wl_display_flush (display->display);

      test_driver_sync_point (display->test_driver, 3, NULL);
      wait_for_sync_event (display, 3);
    }
  else if (g_strcmp0 (argv[1], "bad-state") == 0)
    {
      /* use an YUV non-4:2:0 subsampled buffer */
      draw_main (display, surface, DRM_FORMAT_YUYV);
      wayland_surface_commit (surface);
      wl_display_flush (display->display);

      wp_color_representation_surface_v1_set_alpha_mode (
        color_repr,
        WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT);
      /* center
       * not 4:2:0 subsampled buffer and chroma location should raise an error */
      wp_color_representation_surface_v1_set_chroma_location (
        color_repr,
        WP_COLOR_REPRESENTATION_SURFACE_V1_CHROMA_LOCATION_TYPE_2);

      wl_surface_commit (surface->wl_surface);
      wl_display_flush (display->display);
      /* should apply immediately */
      wl_display_roundtrip (display->display);
    }
  else if (g_strcmp0 (argv[1], "bad-state-2") == 0)
    {
      /* use an RGB buffer */
      draw_main (display, surface, DRM_FORMAT_ARGB8888);
      wayland_surface_commit (surface);
      wl_display_flush (display->display);

      wp_color_representation_surface_v1_set_alpha_mode (
        color_repr,
        WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT);
      /* BT.709 limited range
       * rgb buffer and coefficients should raise an error  */
      wp_color_representation_surface_v1_set_coefficients_and_range (
        color_repr,
        WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709,
        WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED);

      wl_surface_commit (surface->wl_surface);
      wl_display_flush (display->display);
      /* should apply immediately */
      wl_display_roundtrip (display->display);
    }
  else if (g_strcmp0 (argv[1], "premult-reftest") == 0)
    {
      draw_surface (display, surface->wl_surface, 100, 100, 0x7F003F00);
      wayland_surface_commit (surface);
      wait_for_effects_completed (display, surface->wl_surface);
      wait_for_view_verified (display, 0);

      /* premult electrical should be the default */
      wp_color_representation_surface_v1_set_alpha_mode (
        color_repr,
        WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_PREMULTIPLIED_ELECTRICAL);
      wl_surface_commit (surface->wl_surface);
      wait_for_effects_completed (display, surface->wl_surface);
      wait_for_view_verified (display, 0);

      /* check that the premult matches the corresponding straight alpha */
      draw_surface (display, surface->wl_surface, 100, 100, 0x7F007F00);
      wl_surface_damage_buffer (surface->wl_surface,
                                0, 0, surface->width, surface->height);
      wp_color_representation_surface_v1_set_alpha_mode (
        color_repr,
        WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT);
      wl_surface_commit (surface->wl_surface);
      wait_for_effects_completed (display, surface->wl_surface);
      wait_for_view_verified (display, 0);
    }
  return EXIT_SUCCESS;
}
