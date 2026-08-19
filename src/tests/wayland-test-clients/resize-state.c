/*
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

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_NONE);

  surface = wayland_surface_new (display, "resize-state", 100, 100, 0xff00ffff);
  wl_surface_commit (surface->wl_surface);

  /* Wait for being resized. */
  while (!wayland_surface_has_state (surface,
                                     XDG_TOPLEVEL_STATE_RESIZING))
    g_main_context_iteration (NULL, TRUE);

  /* Wait for being not resized. */
  while (wayland_surface_has_state (surface,
                                    XDG_TOPLEVEL_STATE_RESIZING))
    g_main_context_iteration (NULL, TRUE);

  return EXIT_SUCCESS;
}
