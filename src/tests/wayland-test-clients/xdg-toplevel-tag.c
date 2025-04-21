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

#include <glib.h>
#include <wayland-client.h>
#include "wayland-test-client-utils.h"

static void
on_toplevel_configured (WaylandSurface *surface,
                        gpointer        user_data)
{
  WaylandDisplay *display = surface->display;

  xdg_toplevel_tag_manager_v1_set_toplevel_tag (display->toplevel_tag_manager,
                                                surface->xdg_toplevel,
                                                "topleveltag-test");
  test_driver_sync_point (display->test_driver, 0, NULL);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  surface = wayland_surface_new (display, "toplevel-tag",
                                 10, 10, 0xffffffff);
  g_signal_connect (surface, "configure",
                    G_CALLBACK (on_toplevel_configured),
                    NULL);
  wl_surface_commit (surface->wl_surface);

  wait_for_sync_event (display, 0);

  return EXIT_SUCCESS;
}
