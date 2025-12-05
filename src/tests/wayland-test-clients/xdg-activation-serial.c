/*
 * Copyright (C) 2021-2025 Red Hat, Inc.
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

typedef enum _SerialSource
{
  SERIAL_SOURCE_BUTTON_PRESS,
  SERIAL_SOURCE_BUTTON_RELEASE,
  SERIAL_SOURCE_KEY_PRESS,
  SERIAL_SOURCE_KEY_RELEASE,
} SerialSource;

static struct xdg_activation_v1 *activation;
static WaylandSurface *parent_surface;
static SerialSource serial_source;
static gboolean running;

static WaylandDisplay *child_display;
static struct wl_registry *child_registry;
static struct xdg_activation_v1 *child_activation;
static WaylandSurface *child_surface;

static void
handle_registry_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  if (strcmp (interface, "xdg_activation_v1") == 0)
    {
      if (registry == child_registry)
        {
          child_activation = wl_registry_bind (registry,
                                               id, &xdg_activation_v1_interface,
                                               1);
        }
      else
        {
          activation = wl_registry_bind (registry,
                                         id, &xdg_activation_v1_interface, 1);
        }
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
get_token (WaylandSurface *surface,
           uint32_t        serial)
{
  WaylandDisplay *display = surface->display;
  struct xdg_activation_token_v1 *token;
  char *token_string = NULL;

  token = xdg_activation_v1_get_activation_token (activation);

  xdg_activation_token_v1_set_serial (token, serial, display->wl_seat);
  xdg_activation_token_v1_set_surface (token, surface->wl_surface);
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
on_button_event (WaylandSurface    *surface,
                 struct wl_pointer *wl_pointer,
                 uint32_t           serial,
                 uint32_t           button,
                 gboolean           state)
{
  g_autofree char *token_string = NULL;

  if (serial_source == SERIAL_SOURCE_BUTTON_PRESS && state)
    token_string = get_token (surface, serial);
  else if (serial_source == SERIAL_SOURCE_BUTTON_RELEASE && !state)
    token_string = get_token (surface, serial);

  if (!token_string)
    return;

  child_surface = wayland_surface_new (child_display,
                                       "xdg-activation-child",
                                       100, 100, 0xff00ffff);
  xdg_activation_v1_activate (child_activation,
                              token_string,
                              child_surface->wl_surface);
  wl_surface_commit (child_surface->wl_surface);
}

static void
on_key_event (WaylandSurface    *surface,
              struct wl_pointer *wl_pointer,
              uint32_t           serial,
              uint32_t           key,
              gboolean           state)
{
  WaylandDisplay *display = surface->display;
  g_autofree char *token_string = NULL;

  if (serial_source == SERIAL_SOURCE_KEY_PRESS && state)
    token_string = get_token (surface, serial);
  else if (serial_source == SERIAL_SOURCE_KEY_RELEASE && !state)
    token_string = get_token (surface, serial);

  if (!token_string)
    return;

  child_surface = wayland_surface_new (display,
                                       "xdg-activation-child",
                                       100, 100, 0xff00ffff);
  xdg_activation_v1_activate (activation,
                              token_string,
                              child_surface->wl_surface);
  wl_surface_commit (child_surface->wl_surface);
}

static void
test_startup_notifications (void)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wl_registry *registry;

  g_autofree char *token = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display->display);

  child_display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  child_registry = wl_display_get_registry (child_display->display);
  wl_registry_add_listener (child_registry, &registry_listener, NULL);
  wl_display_roundtrip (child_display->display);

  g_assert_nonnull (activation);
  g_assert_nonnull (child_activation);

  parent_surface = wayland_surface_new (display,
                                        "xdg-activation-parent",
                                        100, 100, 0xffff00ff);

  g_signal_connect (parent_surface, "button-event",
                    G_CALLBACK (on_button_event), NULL);
  g_signal_connect (parent_surface, "key-event",
                    G_CALLBACK (on_key_event), NULL);

  wl_surface_commit (parent_surface->wl_surface);

  running = TRUE;
  while (running)
    g_main_context_iteration (NULL, TRUE);

  wl_display_roundtrip (display->display);

  g_clear_pointer (&activation, xdg_activation_v1_destroy);
  g_clear_pointer (&registry, wl_registry_destroy);
}

int
main (int    argc,
      char **argv)
{
  g_assert_cmpint (argc, ==, 2);

  if (strcmp (argv[1], "button-press") == 0)
    serial_source = SERIAL_SOURCE_BUTTON_PRESS;
  else if (strcmp (argv[1], "button-release") == 0)
    serial_source = SERIAL_SOURCE_BUTTON_RELEASE;
  else if (strcmp (argv[1], "key-press") == 0)
    serial_source = SERIAL_SOURCE_KEY_PRESS;
  else if (strcmp (argv[1], "key-release") == 0)
    serial_source = SERIAL_SOURCE_KEY_RELEASE;
  else
    g_assert_not_reached ();

  test_startup_notifications ();

  return EXIT_SUCCESS;
}
