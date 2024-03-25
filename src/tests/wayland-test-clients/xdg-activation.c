/*
 * Copyright (C) 2021 Red Hat, Inc.
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

#include "xdg-activation-v1-client-protocol.h"

static struct xdg_activation_v1 *activation;

static struct wl_surface *surface;

static gboolean running;

static void
init_surface (struct xdg_toplevel *xdg_toplevel,
              const char          *token)
{
  xdg_toplevel_set_title (xdg_toplevel, "startup notification client");
  xdg_activation_v1_activate (activation, token, surface);
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

  draw_main (display);
  wl_surface_commit (surface);

  g_assert_cmpint (wl_display_roundtrip (display->display), !=, -1);
  running = FALSE;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
handle_registry_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  if (strcmp (interface, "xdg_activation_v1") == 0)
    {
      activation = wl_registry_bind (registry,
                                     id, &xdg_activation_v1_interface, 1);
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
token_done (gpointer                        user_data,
            struct xdg_activation_token_v1 *provider,
            const char                     *token)
{
  char **token_ptr = user_data;

  *token_ptr = g_strdup (token);
}

static const struct xdg_activation_token_v1_listener token_listener = {
  token_done,
};

static char *
get_token (WaylandDisplay *display)
{
  struct xdg_activation_token_v1 *token;
  char *token_string = NULL;

  token = xdg_activation_v1_get_activation_token (activation);

  xdg_activation_token_v1_add_listener (token,
                                        &token_listener,
                                        &token_string);
  xdg_activation_token_v1_commit (token);

  while (!token_string)
    {
      if (wl_display_roundtrip (display->display) == -1)
        break;
    }
  xdg_activation_token_v1_destroy (token);

  return token_string;
}

static void
test_startup_notifications (void)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wl_registry *registry;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_surface *xdg_surface;

  g_autofree char *token = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_NONE);
  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display->display);

  g_assert_nonnull (activation);

  wl_display_roundtrip (display->display);

  token = get_token (display);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, display);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);

  init_surface (xdg_toplevel, token);

  running = TRUE;
  while (running)
    wayland_display_dispatch (display);

  wl_display_roundtrip (display->display);

  g_clear_pointer (&xdg_toplevel, xdg_toplevel_destroy);
  g_clear_pointer (&xdg_surface, xdg_surface_destroy);
  g_clear_pointer (&activation, xdg_activation_v1_destroy);
  g_clear_pointer (&registry, wl_registry_destroy);
}

int
main (int    argc,
      char **argv)
{
  test_startup_notifications ();

  return EXIT_SUCCESS;
}
