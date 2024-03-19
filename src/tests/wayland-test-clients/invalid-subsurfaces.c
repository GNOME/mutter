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
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

static void
test_circular_subsurfaces1 (void)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wl_surface *surface1;
  struct wl_subsurface *subsurface1;
  struct wl_surface *surface2;
  struct wl_subsurface *subsurface2;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_NONE);
  surface1 = wl_compositor_create_surface (display->compositor);
  surface2 = wl_compositor_create_surface (display->compositor);
  g_assert_nonnull (surface1);
  g_assert_nonnull (surface2);

  subsurface1 = wl_subcompositor_get_subsurface (display->subcompositor,
                                                 surface1,
                                                 surface2);
  subsurface2 = wl_subcompositor_get_subsurface (display->subcompositor,
                                                 surface2,
                                                 surface1);
  g_assert_nonnull (subsurface1);
  g_assert_nonnull (subsurface2);

  g_assert_cmpint (wl_display_roundtrip (display->display), ==, -1);
}

static void
test_circular_subsurfaces2 (void)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wl_surface *surface1;
  struct wl_subsurface *subsurface1;
  struct wl_surface *surface2;
  struct wl_subsurface *subsurface2;
  struct wl_surface *surface3;
  struct wl_subsurface *subsurface3;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_NONE);

  surface1 = wl_compositor_create_surface (display->compositor);
  surface2 = wl_compositor_create_surface (display->compositor);
  surface3 = wl_compositor_create_surface (display->compositor);
  g_assert_nonnull (surface1);
  g_assert_nonnull (surface2);
  g_assert_nonnull (surface3);

  subsurface1 = wl_subcompositor_get_subsurface (display->subcompositor,
                                                 surface1,
                                                 surface2);
  subsurface2 = wl_subcompositor_get_subsurface (display->subcompositor,
                                                 surface2,
                                                 surface3);
  subsurface3 = wl_subcompositor_get_subsurface (display->subcompositor,
                                                 surface3,
                                                 surface1);
  g_assert_nonnull (subsurface1);
  g_assert_nonnull (subsurface2);
  g_assert_nonnull (subsurface3);

  g_assert_cmpint (wl_display_roundtrip (display->display), ==, -1);
}

int
main (int    argc,
      char **argv)
{
  test_circular_subsurfaces1 ();
  test_circular_subsurfaces2 ();

  return EXIT_SUCCESS;
}
