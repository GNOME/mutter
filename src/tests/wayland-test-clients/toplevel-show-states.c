/*
 * Copyright (C) 2024 Red Hat Inc.
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

static gboolean running;

static void
on_surface_painted (WaylandDisplay *display,
                    WaylandSurface *surface)
{
  test_driver_sync_point (display->test_driver, 1, NULL);
  wl_display_roundtrip (display->display);
  running = FALSE;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER |
                                 WAYLAND_DISPLAY_CAPABILITY_XDG_SHELL_V6);
  surface = wayland_surface_new (display, "showing-states", 100, 100, 0xffffffff);
  wl_surface_commit (surface->wl_surface);

  test_driver_sync_point (display->test_driver, 0, NULL);
  wait_for_sync_event (display, 0);

  g_signal_connect (display, "surface-painted",
                    G_CALLBACK (on_surface_painted), NULL);

  running = TRUE;
  while (running)
    wayland_display_dispatch (display);

  return EXIT_SUCCESS;
}
