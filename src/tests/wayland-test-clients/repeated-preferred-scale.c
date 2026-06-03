/*
 * Copyright (C) 2023 Collabora, Ltd.
 * Copyright (C) 2026 Red Hat Inc.
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
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

static struct wl_surface *surface;

static gboolean running;
static gboolean waiting_for_scale;
static int sync_point = 0;

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
  xdg_surface_ack_configure (xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
handle_preferred_scale (void                          *data,
                        struct wp_fractional_scale_v1 *fractional_scale,
                        uint32_t                       wire_scale)
{
  waiting_for_scale = FALSE;
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
  xdg_toplevel_set_title (xdg_toplevel, "repeated-preferred-scale");

  fractional_scale_obj =
    wp_fractional_scale_manager_v1_get_fractional_scale (display->fractional_scale_mgr,
                                                         surface);
  wp_fractional_scale_v1_add_listener (fractional_scale_obj,
                                       &fractional_scale_listener,
                                       display);

  wl_surface_commit (surface);

  waiting_for_scale = TRUE;

  running = TRUE;
  while (running && waiting_for_scale)
    wayland_display_dispatch (display);

  g_assert_true (running);
  g_assert_false (waiting_for_scale);

  wp_fractional_scale_v1_destroy (fractional_scale_obj);
  fractional_scale_obj =
    wp_fractional_scale_manager_v1_get_fractional_scale (display->fractional_scale_mgr,
                                                         surface);
  wp_fractional_scale_v1_add_listener (fractional_scale_obj,
                                       &fractional_scale_listener,
                                       display);

  test_driver_sync_point (display->test_driver, sync_point++, NULL);

  waiting_for_scale = TRUE;
  while (running && waiting_for_scale)
    wayland_display_dispatch (display);

  g_assert_false (waiting_for_scale);

  wl_display_roundtrip (display->display);

  return EXIT_SUCCESS;
}
