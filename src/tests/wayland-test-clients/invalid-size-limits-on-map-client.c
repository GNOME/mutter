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

static void
on_configure (WaylandSurface *surface)
{
  g_assert_cmpint (surface->width, ==, 250);
  g_assert_cmpint (surface->height, ==, 250);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  surface = wayland_surface_new (display, "invalid-size-limits-client",
                                 250, 250, 0xffffffff);
  g_signal_connect (surface, "configure", G_CALLBACK (on_configure), NULL);
  xdg_toplevel_set_min_size (surface->xdg_toplevel, 200, 200);
  xdg_toplevel_set_max_size (surface->xdg_toplevel, 200, 200);
  wl_surface_commit (surface->wl_surface);

  while (TRUE)
    g_main_context_iteration (NULL, TRUE);

  g_object_unref (display);

  return EXIT_SUCCESS;
}
