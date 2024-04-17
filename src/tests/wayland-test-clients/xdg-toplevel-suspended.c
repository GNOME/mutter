/*
 * Copyright (C) 2024 Red Hat Inc.
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

enum
{
  XDG_TOPLEVEL_SUSPENDED_COMMAND_NEXT_WORKSPACE = 0,
  XDG_TOPLEVEL_SUSPENDED_COMMAND_PREV_WORKSPACE = 1,
  XDG_TOPLEVEL_SUSPENDED_COMMAND_ACTIVATE_WINDOW = 2,
};

static void
wait_for_state (WaylandSurface          *surface,
                enum xdg_toplevel_state  state)
{
  while (!wayland_surface_has_state (surface, state))
    wayland_display_dispatch (surface->display);
}

static void
wait_for_no_state (WaylandSurface          *surface,
                   enum xdg_toplevel_state  state)
{
  while (wayland_surface_has_state (surface, state))
    wayland_display_dispatch (surface->display);
}

static void
test_floating (WaylandDisplay *display)
{
  g_autoptr (WaylandSurface) surface = NULL;

  g_debug ("Testing suspended state when mapping floating");

  surface = wayland_surface_new (display, __func__, 100, 100, 0xffffffff);
  wl_surface_commit (surface->wl_surface);

  wait_for_window_shown (display, surface->wl_surface);
  g_assert_false (wayland_surface_has_state (surface,
                                             XDG_TOPLEVEL_STATE_SUSPENDED));
}

static void
test_maximized (WaylandDisplay *display)
{
  g_autoptr (WaylandSurface) surface = NULL;

  g_debug ("Testing suspended state when mapping maximized");

  surface = wayland_surface_new (display, __func__, 100, 100, 0xffffffff);
  xdg_toplevel_set_maximized (surface->xdg_toplevel);
  wl_surface_commit (surface->wl_surface);

  wait_for_window_shown (display, surface->wl_surface);
  g_assert_false (wayland_surface_has_state (surface,
                                             XDG_TOPLEVEL_STATE_SUSPENDED));
}

static void
test_minimized (WaylandDisplay *display)
{
  g_autoptr (WaylandSurface) surface = NULL;

  g_debug ("Testing suspended state when mapping minimized");

  surface = wayland_surface_new (display, __func__, 100, 100, 0xffffffff);
  wl_surface_commit (surface->wl_surface);

  wait_for_window_shown (display, surface->wl_surface);
  g_assert_false (wayland_surface_has_state (surface,
                                             XDG_TOPLEVEL_STATE_SUSPENDED));

  xdg_toplevel_set_minimized (surface->xdg_toplevel);
  wait_for_state (surface, XDG_TOPLEVEL_STATE_SUSPENDED);
}

static void
test_workspace_changes (WaylandDisplay *display)
{
  g_autoptr (WaylandSurface) surface = NULL;

  g_debug ("Testing suspended state when changing workspace");

  surface = wayland_surface_new (display, __func__, 100, 100, 0xffffffff);
  wl_surface_commit (surface->wl_surface);

  wait_for_window_shown (display, surface->wl_surface);
  g_assert_false (wayland_surface_has_state (surface,
                                             XDG_TOPLEVEL_STATE_SUSPENDED));


  test_driver_sync_point (display->test_driver,
                          XDG_TOPLEVEL_SUSPENDED_COMMAND_NEXT_WORKSPACE,
                          NULL);

  wait_for_state (surface, XDG_TOPLEVEL_STATE_SUSPENDED);

  test_driver_sync_point (display->test_driver,
                          XDG_TOPLEVEL_SUSPENDED_COMMAND_PREV_WORKSPACE,
                          NULL);

  wait_for_no_state (surface, XDG_TOPLEVEL_STATE_SUSPENDED);
}

static void
test_obstructed (WaylandDisplay *display)
{
  g_autoptr (WaylandSurface) surface = NULL;
  g_autoptr (WaylandSurface) cover_surface = NULL;

  g_debug ("Testing suspended state when obstructed");

  surface = wayland_surface_new (display, __func__,
                                 100, 100, 0xffffffff);
  wl_surface_commit (surface->wl_surface);

  wait_for_window_shown (display, surface->wl_surface);
  g_assert_false (wayland_surface_has_state (surface,
                                             XDG_TOPLEVEL_STATE_SUSPENDED));

  cover_surface = wayland_surface_new (display, "obstruction",
                                       100, 100, 0xffffffff);
  xdg_toplevel_set_maximized (cover_surface->xdg_toplevel);
  wl_surface_commit (cover_surface->wl_surface);

  wait_for_window_shown (display, cover_surface->wl_surface);
  test_driver_sync_point (display->test_driver,
                          XDG_TOPLEVEL_SUSPENDED_COMMAND_ACTIVATE_WINDOW,
                          cover_surface->wl_surface);

  wait_for_state (surface, XDG_TOPLEVEL_STATE_SUSPENDED);

  g_clear_object (&cover_surface);

  wait_for_no_state (surface, XDG_TOPLEVEL_STATE_SUSPENDED);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER |
                                 WAYLAND_DISPLAY_CAPABILITY_XDG_SHELL_V6);

  test_floating (display);
  test_maximized (display);
  test_minimized (display);
  test_workspace_changes (display);
  test_obstructed (display);

  return EXIT_SUCCESS;
}
