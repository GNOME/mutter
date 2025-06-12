/*
 * Copyright (C) 2025 Canonical Ltd.
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
token_done (gpointer                        user_data,
            struct xdg_activation_token_v1 *provider,
            const char                     *token)
{
  char **out_token_string = user_data;

  *out_token_string = g_strdup (token);
  xdg_activation_token_v1_destroy (provider);
}

static const struct xdg_activation_token_v1_listener token_listener = {
  token_done,
};

static void
request_token (WaylandSurface     *surface,
               struct wl_keyboard *keyboard,
               uint32_t            serial,
               char              **out_token_string)
{
  WaylandDisplay *display = surface->display;
  struct xdg_activation_token_v1 *token;

  token = xdg_activation_v1_get_activation_token (display->xdg_activation);
  xdg_activation_token_v1_set_serial (token, serial, display->wl_seat);
  xdg_activation_token_v1_set_surface (token, surface->wl_surface);
  xdg_activation_token_v1_add_listener (token, &token_listener, out_token_string);
  xdg_activation_token_v1_commit (token);
}

static void
wait_for_token (WaylandSurface *surface,
                char          **token) {
  g_signal_connect (surface, "keyboard-enter",
                    G_CALLBACK (request_token),
                    token);
  wl_surface_commit (surface->wl_surface);

  while (!*token)
    wayland_display_dispatch (surface->display);
}

static void
on_window_focused (WaylandSurface     *surface,
                   struct wl_keyboard *keyboard,
                   uint32_t            serial,
                   gboolean           *done)
{
  *done = TRUE;
}

static void
wait_for_keyboard_enter (WaylandSurface *surface)
{
  gboolean done = FALSE;

  g_signal_connect (surface, "keyboard-enter",
                    G_CALLBACK (on_window_focused),
                    &done);
  wl_surface_commit (surface->wl_surface);

  while (!done)
    wayland_display_dispatch (surface->display);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface1 = NULL;
  g_autoptr (WaylandSurface) surface2 = NULL;
  g_autofree char *token = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  surface1 = wayland_surface_new (display, "activating-window",
                                  10, 10, 0xffffffff);

  wait_for_token (surface1, &token);
  test_driver_sync_point (display->test_driver, 0, NULL);

  wait_for_sync_event (display, 0);
  surface2 = wayland_surface_new (display, "activated-window",
                                  10, 10, 0xffff00ff);
  xdg_activation_v1_activate (display->xdg_activation, token, surface2->wl_surface);

  wait_for_keyboard_enter (surface2);
  test_driver_sync_point (display->test_driver, 1, NULL);
  wayland_display_dispatch (display);

  return EXIT_SUCCESS;
}
