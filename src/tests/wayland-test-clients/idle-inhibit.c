/*
 * Copyright (C) 2023 Red Hat Inc.
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

#include "idle-inhibit-unstable-v1-client-protocol.h"

struct zwp_idle_inhibit_manager_v1 *idle_inhibit_manager;

static void
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  if (strcmp (interface, "zwp_idle_inhibit_manager_v1") == 0)
    {
      idle_inhibit_manager = wl_registry_bind (registry, id,
                                               &zwp_idle_inhibit_manager_v1_interface,
                                               1);
    }
}

static void
handle_registry_global_remove (void               *user_data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
  handle_registry_global,
  handle_registry_global_remove
};

int
main (int    argc,
      char **argv)
{
  struct wl_registry *registry;
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;
  struct zwp_idle_inhibitor_v1 *inhibitor;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display->display);

  surface = wayland_surface_new (display,
                                 "idle-inhibit-client",
                                 20, 20, 0x11223344);

  inhibitor =
    zwp_idle_inhibit_manager_v1_create_inhibitor (idle_inhibit_manager,
                                                  surface->wl_surface);
  zwp_idle_inhibitor_v1_destroy (inhibitor);

  wl_display_roundtrip (display->display);

  return EXIT_SUCCESS;
}
