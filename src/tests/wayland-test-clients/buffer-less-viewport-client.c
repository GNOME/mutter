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
 */

#include "config.h"

#include "wayland-test-client-utils.h"

static WaylandDisplay *display;
static struct wl_surface *wl_surface;
static struct wp_viewport *wp_viewport;

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  xdg_surface_ack_configure (xdg_surface, serial);
  wl_surface_commit (wl_surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *states)
{
  draw_surface (display, wl_surface, 1, 1, 0x1f109f20);
  wp_viewport_set_destination (wp_viewport, 200, 200);
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

int
main (int    argc,
      char **argv)
{
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  wl_surface = wl_compositor_create_surface (display->compositor);
  wp_viewport = wp_viewporter_get_viewport (display->viewporter, wl_surface);
  wp_viewport_set_destination (wp_viewport, 200, 200);
  wl_surface_commit (wl_surface);

  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base,
                                             wl_surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener,
                            wl_surface);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener,
                             wl_surface);
  xdg_toplevel_set_title (xdg_toplevel, "buffer-less-viewport");
  wl_surface_commit (wl_surface);

  while (TRUE)
    g_main_context_iteration (NULL, TRUE);

  g_object_unref (display);

  return EXIT_SUCCESS;
}
