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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "wayland-test-client-utils.h"

static struct wl_pointer *pointer;
static uint32_t serial;

static gboolean waiting_for_pointer_enter = FALSE;

static void
on_pointer_enter (WaylandSurface    *surface,
                  struct wl_pointer *pointer_l,
                  uint32_t           serial_l)
{
  waiting_for_pointer_enter = FALSE;
  pointer = pointer_l;
  serial = serial_l;
}

static void
wait_for_pointer_enter (WaylandSurface *surface)
{
  gulong handler_id;

  handler_id = g_signal_connect (surface, "pointer-enter",
                                 G_CALLBACK (on_pointer_enter), NULL);

  waiting_for_pointer_enter = TRUE;
  while (waiting_for_pointer_enter)
    wayland_display_dispatch (surface->display);

  g_clear_signal_handler (&handler_id, surface);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;
  struct wp_cursor_shape_device_v1 *cursor_shape_device;
  WaylandDisplayCapabilities caps = WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER |
    WAYLAND_DISPLAY_CAPABILITY_CURSOR_SHAPE_V2;

  display = wayland_display_new (caps);
  surface = wayland_surface_new (display,
                                 argv[1],
                                 100, 100,
                                 0xffffffff);
  wl_surface_commit (surface->wl_surface);

  wait_for_effects_completed (display, surface->wl_surface);

  g_assert (display->cursor_shape_mgr);
  cursor_shape_device =
    wp_cursor_shape_manager_v1_get_pointer (display->cursor_shape_mgr,
                                            display->wl_pointer);

  if (g_strcmp0 (argv[1], "src") == 0)
    {
      test_driver_sync_point (display->test_driver, 0, NULL);

      wait_for_pointer_enter (surface);

      /* make sure the surface cursor is still visible */
      wait_for_view_verified (display, 0);

      wp_cursor_shape_device_v1_set_shape (cursor_shape_device,
                                           serial,
                                           WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE);
      wait_for_view_verified (display, 1);

      test_driver_sync_point (display->test_driver, 2, NULL);

      wait_for_sync_event (display, 0);

      return EXIT_SUCCESS;
    }
  else if (g_strcmp0 (argv[1], "dst") == 0)
    {
      test_driver_sync_point (display->test_driver, 1, NULL);

      wait_for_pointer_enter (surface);

      /* make sure the surface cursor is still visible */
      wait_for_view_verified (display, 2);

      wp_cursor_shape_device_v1_set_shape (cursor_shape_device,
                                           serial,
                                           WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
      wait_for_view_verified (display, 3);

      test_driver_sync_point (display->test_driver, 3, NULL);

      wait_for_sync_event (display, 0);

      return EXIT_SUCCESS;
    }

  return EXIT_FAILURE;
}
