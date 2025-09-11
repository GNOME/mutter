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
  xdg_surface_set_window_geometry (surface->xdg_surface,
                                   0, 0,
                                   surface->width, surface->height);
  wl_surface_commit (surface->wl_surface);

  wayland_surface_commit (surface);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  surface = wayland_surface_new (display, "invalid-geometry",
                                 200, 200, 0xffffffff);

  g_signal_connect (surface, "configure", G_CALLBACK (on_configure), NULL);
  wl_surface_commit (surface->wl_surface);

  /* Imitate a common Wayland protocol violation. */
  if (argc == 2 && g_strcmp0 (argv[1], "with-subsurface") == 0)
    {
      struct wl_surface *subsurface_surface;

      /* A floating window hiding subsurface window decorations, while setting
       * bogus window geometry each step.
       */

      subsurface_surface = wl_compositor_create_surface (display->compositor);
      wl_subcompositor_get_subsurface (display->subcompositor,
                                       subsurface_surface,
                                       surface->wl_surface);
      draw_surface (display, subsurface_surface, 100, 100, 0xff00ffff);
      wl_surface_commit (subsurface_surface);
      xdg_surface_set_window_geometry (surface->xdg_surface, 0, 0, 150, 150);
      wl_surface_commit (surface->wl_surface);

      wl_surface_attach (subsurface_surface, NULL, 0, 0);
      wl_surface_commit (subsurface_surface);
    }

  xdg_surface_set_window_geometry (surface->xdg_surface, 0, 0, 150, 150);
  wl_surface_commit (surface->wl_surface);

  while (TRUE)
    wayland_display_dispatch (display);

  return EXIT_SUCCESS;
}
