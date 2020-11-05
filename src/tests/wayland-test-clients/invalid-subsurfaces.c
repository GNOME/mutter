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

#include "xdg-shell-client-protocol.h"

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct wl_subcompositor *subcompositor;

static void
handle_registry_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  if (strcmp (interface, "wl_compositor") == 0)
    {
      compositor = wl_registry_bind (registry, id, &wl_compositor_interface, 1);
    }
  else if (strcmp (interface, "wl_subcompositor") == 0)
    {
      subcompositor = wl_registry_bind (registry,
                                        id, &wl_subcompositor_interface, 1);
    }
}

static void
handle_registry_global_remove (void               *data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
  handle_registry_global,
  handle_registry_global_remove
};

static void
connect_to_display (void)
{
  g_assert_null (display);
  g_assert_null (registry);
  g_assert_null (subcompositor);

  display = wl_display_connect (NULL);
  g_assert_nonnull (display);
  registry = wl_display_get_registry (display);
  g_assert_nonnull (registry);
  wl_registry_add_listener (registry, &registry_listener, NULL);

  g_assert_cmpint (wl_display_roundtrip (display), !=, -1);

  g_assert_nonnull (subcompositor);
}

static void
clean_up_display (void)
{
  wl_display_disconnect (display);
  display = NULL;
  registry = NULL;
  subcompositor = NULL;
}

static void
test_circular_subsurfaces1 (void)
{
  struct wl_surface *surface1;
  struct wl_subsurface *subsurface1;
  struct wl_surface *surface2;
  struct wl_subsurface *subsurface2;

  connect_to_display ();

  surface1 = wl_compositor_create_surface (compositor);
  surface2 = wl_compositor_create_surface (compositor);
  g_assert_nonnull (surface1);
  g_assert_nonnull (surface2);

  subsurface1 = wl_subcompositor_get_subsurface (subcompositor,
                                                 surface1,
                                                 surface2);
  subsurface2 = wl_subcompositor_get_subsurface (subcompositor,
                                                 surface2,
                                                 surface1);
  g_assert_nonnull (subsurface1);
  g_assert_nonnull (subsurface2);

  g_assert_cmpint (wl_display_roundtrip (display), ==, -1);

  clean_up_display ();
}

static void
test_circular_subsurfaces2 (void)
{
  struct wl_surface *surface1;
  struct wl_subsurface *subsurface1;
  struct wl_surface *surface2;
  struct wl_subsurface *subsurface2;
  struct wl_surface *surface3;
  struct wl_subsurface *subsurface3;

  connect_to_display ();

  surface1 = wl_compositor_create_surface (compositor);
  surface2 = wl_compositor_create_surface (compositor);
  surface3 = wl_compositor_create_surface (compositor);
  g_assert_nonnull (surface1);
  g_assert_nonnull (surface2);
  g_assert_nonnull (surface3);

  subsurface1 = wl_subcompositor_get_subsurface (subcompositor,
                                                 surface1,
                                                 surface2);
  subsurface2 = wl_subcompositor_get_subsurface (subcompositor,
                                                 surface2,
                                                 surface3);
  subsurface3 = wl_subcompositor_get_subsurface (subcompositor,
                                                 surface3,
                                                 surface1);
  g_assert_nonnull (subsurface1);
  g_assert_nonnull (subsurface2);
  g_assert_nonnull (subsurface3);

  g_assert_cmpint (wl_display_roundtrip (display), ==, -1);

  clean_up_display ();
}

int
main (int    argc,
      char **argv)
{
  test_circular_subsurfaces1 ();
  test_circular_subsurfaces2 ();

  return EXIT_SUCCESS;
}
