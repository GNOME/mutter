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
 *
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
  struct wl_surface *cursor_surface;
  struct wp_cursor_shape_device_v1 *cursor_shape_device;
  WaylandDisplayCapabilities caps = WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER;

  if (g_strcmp0 (argv[1], "v2-shape-on-v1") != 0)
    caps |= WAYLAND_DISPLAY_CAPABILITY_CURSOR_SHAPE_V2;

  display = wayland_display_new (caps);
  surface = wayland_surface_new (display,
                                 "cursor-shape",
                                 100, 100, 0xffffffff);
  xdg_toplevel_set_fullscreen (surface->xdg_toplevel, NULL);
  wl_surface_commit (surface->wl_surface);

  wait_for_pointer_enter (surface);
  wait_for_effects_completed (display, surface->wl_surface);

  cursor_surface = wl_compositor_create_surface (display->compositor);
  draw_surface (display, cursor_surface, 10, 10, 0xff00ff00);
  wl_surface_damage_buffer (cursor_surface, 0, 0, 10, 10);
  wl_surface_commit (cursor_surface);
  wl_pointer_set_cursor (pointer, serial, cursor_surface, 0, 0);

  g_assert (display->cursor_shape_mgr);
  cursor_shape_device =
    wp_cursor_shape_manager_v1_get_pointer (display->cursor_shape_mgr, pointer);

  if (g_strcmp0 (argv[1], "v2-shape-on-v1") == 0)
    {
      wp_cursor_shape_device_v1_set_shape (cursor_shape_device,
                                           serial,
                                           WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_RESIZE);

      if (wl_display_dispatch (display->display) != -1)
        return 1;

      return EXIT_SUCCESS;
    }
  else if (g_strcmp0 (argv[1], "bad-shape") == 0)
    {
      wp_cursor_shape_device_v1_set_shape (cursor_shape_device,
                                           serial,
                                           3333);

      if (wl_display_dispatch (display->display) != -1)
        return 1;

      return EXIT_SUCCESS;
    }
  else if (g_strcmp0 (argv[1], "ref-test") == 0)
    {
      /* make sure the surface cursor is still visible */
      wait_for_view_verified (display, 0);
      /* make sure the default shape is visible */
      wp_cursor_shape_device_v1_set_shape (cursor_shape_device,
                                           serial,
                                           WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
      wait_for_view_verified (display, 1);
      /* make sure switching back to the surface cursor works */
      wl_pointer_set_cursor (pointer, serial, cursor_surface, 0, 0);
      wait_for_view_verified (display, 0);
      /* make sure another shape works */
      wp_cursor_shape_device_v1_set_shape (cursor_shape_device,
                                           serial,
                                           WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE);
      wait_for_view_verified (display, 2);
      /* destroy the wp_cursor_shape_device and make sure the shape persists */
      wp_cursor_shape_device_v1_set_shape (cursor_shape_device,
                                           serial,
                                           WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
      wp_cursor_shape_device_v1_destroy (cursor_shape_device);
      wait_for_view_verified (display, 1);
      /* make sure disabling the cursor works */
      wl_pointer_set_cursor (pointer, serial, NULL, 0, 0);
      wait_for_view_verified (display, 3);

      return EXIT_SUCCESS;
    }


  return 1;
}
