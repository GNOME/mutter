/*
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

static struct wl_surface *surface;
static struct wp_viewport *viewport;

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

  if (waiting_for_configure || waiting_for_scale)
    return;

  g_assert_cmpint (logical_width, >, 0);
  g_assert_cmpint (logical_height, >, 0);
  g_assert_cmpfloat (fractional_buffer_scale, >, 0.0);

  buffer_width = ceilf (logical_width * fractional_buffer_scale);
  buffer_height = ceilf (logical_height * fractional_buffer_scale);

  draw_surface (display, surface, buffer_width, buffer_height, 0x1f109f20);
  wp_viewport_set_destination (viewport, logical_width, logical_height);

  callback = wl_surface_frame (surface);
  wl_callback_add_listener (callback, &frame_listener, display);

  wl_surface_commit (surface);

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

static void handle_preferred_scale (void                          *data,
                                    struct wp_fractional_scale_v1 *fractional_scale_obj,
                                    uint32_t                       wire_scale)
{
  WaylandDisplay *display = data;
  float new_fractional_buffer_scale;

  new_fractional_buffer_scale = wire_scale / 120.0;
  if (G_APPROX_VALUE (new_fractional_buffer_scale,
                      fractional_buffer_scale,
                      FLT_EPSILON))
    return;

  fractional_buffer_scale = new_fractional_buffer_scale;
  waiting_for_scale = FALSE;
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

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, display);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);
  xdg_toplevel_set_title (xdg_toplevel, "fractional-scale");
  xdg_toplevel_set_fullscreen (xdg_toplevel, NULL);

  viewport = wp_viewporter_get_viewport (display->viewporter, surface);
  fractional_scale_obj =
    wp_fractional_scale_manager_v1_get_fractional_scale (display->fractional_scale_mgr,
                                                         surface);
  wp_fractional_scale_v1_add_listener (fractional_scale_obj,
                                       &fractional_scale_listener,
                                       display);

  wl_surface_commit (surface);

  waiting_for_configure = TRUE;
  waiting_for_scale = FALSE;

  running = TRUE;
  while (running)
    wayland_display_dispatch (display);

  wl_display_roundtrip (display->display);

  return EXIT_SUCCESS;
}
