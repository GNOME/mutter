/*
 * Copyright (C) 2025 Red Hat, Inc.
 * Copyright (C) 2023 Collabora, Ltd.
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
#include <math.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

static struct wl_surface *surface[3];
static struct wl_subsurface *subsurface[2];
static struct wp_viewport *viewport[3];

static gboolean running;
static gboolean waiting_for_configure;
static gboolean waiting_for_scale;
static uint32_t logical_width = 0;
static uint32_t logical_height = 0;
static float fractional_buffer_scale = 1.0;
static int sync_point = 0;

static void
handle_frame_callback (void               *data,
                       struct wl_callback *callback,
                       uint32_t            time)
{
  WaylandDisplay *display = data;

  wl_callback_destroy (callback);
  test_driver_sync_point (display->test_driver, sync_point++, NULL);
}

static const struct wl_callback_listener frame_listener = {
  handle_frame_callback,
};

static void
maybe_redraw (WaylandDisplay *display)
{
  struct wl_callback *callback;
  uint32_t buffer_width;
  uint32_t buffer_height;
  WaylandBuffer *buffer;
  int x, y;

  if (waiting_for_configure || waiting_for_scale)
    return;

  g_assert_cmpint (logical_width, >, 0);
  g_assert_cmpint (logical_height, >, 0);
  g_assert_cmpfloat (fractional_buffer_scale, >, 0.0);

  /* Parent surface */
  draw_surface (display, surface[0], 1, 1, 0xffffffff);
  wp_viewport_set_destination (viewport[0], logical_width, logical_height);

  buffer_width = (int) ceilf (logical_width * fractional_buffer_scale / 2);
  buffer_height = (int) ceilf (logical_height * fractional_buffer_scale / 2);

  buffer = wayland_buffer_create (display, NULL,
                                  buffer_width, buffer_height,
                                  DRM_FORMAT_XRGB8888,
                                  NULL, 0,
                                  GBM_BO_USE_LINEAR);
  g_assert_nonnull (buffer);

  for (x = 0; x < buffer_width; x++)
    {
      uint32_t current_color;

      if (x & 0x1)
        current_color = 0xffff0000;
      else
        current_color = 0xff0000ff;

      for (y = 0; y < buffer_height; y++)
        wayland_buffer_draw_pixel (buffer, x, y, current_color);
    }

  /* Sub-surface for top-left quadrant */
  wl_surface_attach (surface[1], wayland_buffer_get_wl_buffer (buffer), 0, 0);
  wp_viewport_set_destination (viewport[1], logical_width / 2, logical_height / 2);
  wl_surface_commit (surface[1]);

  /* Sub-surface for top-right quadrant */
  wl_surface_attach (surface[2], wayland_buffer_get_wl_buffer (buffer), 0, 0);
  wp_viewport_set_source (viewport[2], wl_fixed_from_int (1), 0,
                          wl_fixed_from_int (buffer_width - 1),
                          wl_fixed_from_int (buffer_height));
  wp_viewport_set_destination (viewport[2], logical_width / 2, logical_height / 2);
  wl_subsurface_set_position (subsurface[1], logical_width / 2, 0);
  wl_surface_commit (surface[2]);

  callback = wl_surface_frame (surface[0]);
  wl_callback_add_listener (callback, &frame_listener, display);

  wl_surface_commit (surface[0]);

  waiting_for_configure = TRUE;
  waiting_for_scale = TRUE;
}

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *states)
{
  if (width > 0 && height > 0)
    {
      if (width == 640)
        logical_width = 638;
      else
        logical_width = width;

      logical_height = height;
      waiting_for_configure = TRUE;
    }
}

static void
handle_xdg_toplevel_close (void                *data,
                           struct xdg_toplevel *xdg_toplevel)
{
  running = FALSE;
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
  WaylandDisplay *display = data;
  xdg_surface_ack_configure (xdg_surface, serial);
  waiting_for_configure = FALSE;

  maybe_redraw (display);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
handle_preferred_scale (void                          *data,
                        struct wp_fractional_scale_v1 *fractional_scale,
                        uint32_t                       wire_scale)
{
  WaylandDisplay *display = data;
  float new_fractional_buffer_scale;

  waiting_for_scale = FALSE;

  new_fractional_buffer_scale = wire_scale / 120.0f;
  if (G_APPROX_VALUE (new_fractional_buffer_scale,
                      fractional_buffer_scale,
                      FLT_EPSILON))
    return;

  fractional_buffer_scale = new_fractional_buffer_scale;
  maybe_redraw (display);
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
  .preferred_scale = handle_preferred_scale,
};

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_surface *xdg_surface;
  struct wp_fractional_scale_v1 *fractional_scale_obj;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  test_driver_sync_point (display->test_driver, sync_point++, NULL);

  surface[0] = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface[0]);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, display);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);
  xdg_toplevel_set_title (xdg_toplevel, "stable-rounding");
  xdg_toplevel_set_fullscreen (xdg_toplevel, NULL);

  viewport[0] = wp_viewporter_get_viewport (display->viewporter, surface[0]);

  surface[1] = wl_compositor_create_surface (display->compositor);
  subsurface[0] = wl_subcompositor_get_subsurface (display->subcompositor,
                                                   surface[1], surface[0]);
  viewport[1] = wp_viewporter_get_viewport (display->viewporter, surface[1]);

  surface[2] = wl_compositor_create_surface (display->compositor);
  subsurface[1] = wl_subcompositor_get_subsurface (display->subcompositor,
                                                   surface[2], surface[0]);
  viewport[2] = wp_viewporter_get_viewport (display->viewporter, surface[2]);

  fractional_scale_obj =
    wp_fractional_scale_manager_v1_get_fractional_scale (display->fractional_scale_mgr,
                                                         surface[0]);
  wp_fractional_scale_v1_add_listener (fractional_scale_obj,
                                       &fractional_scale_listener,
                                       display);

  wl_surface_commit (surface[0]);

  waiting_for_configure = TRUE;
  waiting_for_scale = FALSE;

  running = TRUE;
  while (running)
    wayland_display_dispatch (display);

  wl_display_roundtrip (display->display);

  return EXIT_SUCCESS;
}
