/*
 * Copyright (C) 2020 Red Hat, Inc.
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

static void
init_surface (struct xdg_toplevel *xdg_toplevel)
{
  xdg_toplevel_set_title (xdg_toplevel, "bogus window geometry");
  wl_surface_commit (surface);
}

static void
draw_main (WaylandDisplay *display)
{
  draw_surface (display, surface, 700, 500, 0xff00ff00);
}

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *state)
{
}

static void
handle_xdg_toplevel_close (void                *data,
                           struct xdg_toplevel *xdg_toplevel)
{
  g_assert_not_reached ();
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
  static gboolean sent_invalid_once = FALSE;

  if (sent_invalid_once)
    return;

  xdg_surface_set_window_geometry (xdg_surface, 0, 0, 0, 0);
  draw_main (display);
  wl_surface_commit (surface);

  sent_invalid_once = TRUE;

  g_assert_cmpint (wl_display_roundtrip (display->display), !=, -1);
  running = FALSE;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
test_empty_window_geometry (void)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_surface *xdg_surface;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_NONE);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, display);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);

  init_surface (xdg_toplevel);

  running = TRUE;
  while (running)
    {
      if (wl_display_dispatch (display->display) == -1)
        return;
    }

  g_clear_pointer (&xdg_toplevel, xdg_toplevel_destroy);
  g_clear_pointer (&xdg_surface, xdg_surface_destroy);
}

int
main (int    argc,
      char **argv)
{
  test_empty_window_geometry ();

  return EXIT_SUCCESS;
}
