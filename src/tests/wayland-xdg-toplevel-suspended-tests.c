/*
 * Copyright (C) 2019-2026 Red Hat, Inc.
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

#include "compositor/meta-window-actor-private.h"
#include "core/display-private.h"
#include "core/meta-workspace-manager-private.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-runner.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland-surface-private.h"

enum
{
  XDG_TOPLEVEL_SUSPENDED_COMMAND_NEXT_WORKSPACE = 0,
  XDG_TOPLEVEL_SUSPENDED_COMMAND_PREV_WORKSPACE = 1,
  XDG_TOPLEVEL_SUSPENDED_COMMAND_ACTIVATE_WINDOW = 2,
  XDG_TOPLEVEL_SUSPENDED_COMMAND_CLONE = 3,
  XDG_TOPLEVEL_SUSPENDED_COMMAND_SHOW_SCREEN_SHIELD = 4,
  XDG_TOPLEVEL_SUSPENDED_COMMAND_HIDE_SCREEN_SHIELD = 5,
};

static void
on_toplevel_suspended_sync_point (MetaWaylandTestDriver *driver,
                                  unsigned int           sequence,
                                  struct wl_resource    *surface_resource,
                                  struct wl_client      *wl_client)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaWorkspaceManager *workspace_manager =
    meta_display_get_workspace_manager (display);
  MetaWorkspace *current_workspace;
  int index;
  MetaWorkspace *workspace;
  MetaWaylandSurface *surface;
  uint32_t now_ms;

  current_workspace =
    meta_workspace_manager_get_active_workspace (workspace_manager);
  index = meta_workspace_index (current_workspace);
  switch (sequence)
    {
    case XDG_TOPLEVEL_SUSPENDED_COMMAND_NEXT_WORKSPACE:
      workspace =
        meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                       index + 1);
      now_ms = meta_display_get_current_time_roundtrip (display);
      meta_workspace_activate (workspace, now_ms);
      break;
    case XDG_TOPLEVEL_SUSPENDED_COMMAND_PREV_WORKSPACE:
      workspace =
        meta_workspace_manager_get_workspace_by_index (workspace_manager,
                                                       index - 1);
      now_ms = meta_display_get_current_time_roundtrip (display);
      meta_workspace_activate (workspace, now_ms);
      break;
    case XDG_TOPLEVEL_SUSPENDED_COMMAND_ACTIVATE_WINDOW:
      surface = wl_resource_get_user_data (surface_resource);
      now_ms = meta_display_get_current_time_roundtrip (display);
      meta_window_activate (meta_wayland_surface_get_window (surface), now_ms);
      break;
    case XDG_TOPLEVEL_SUSPENDED_COMMAND_CLONE:
      {
        MetaBackend *backend = meta_context_get_backend (test_context);
        ClutterActor *stage = meta_backend_get_stage (backend);
        MetaWindow *window;
        MetaWindowActor *window_actor;
        ClutterActor *clone;

        surface = wl_resource_get_user_data (surface_resource);
        window = meta_wayland_surface_get_window (surface);
        window_actor = meta_window_actor_from_window (window);

        clone = clutter_clone_new (CLUTTER_ACTOR (window_actor));
        clutter_actor_show (clone);
        clutter_actor_add_child (stage, clone);

        g_object_set_data_full (G_OBJECT (window), "suspend-test-clone",
                                clone, (GDestroyNotify) clutter_actor_destroy);

        break;
      }
    case XDG_TOPLEVEL_SUSPENDED_COMMAND_SHOW_SCREEN_SHIELD:
      {
        MetaCompositor *compositor = meta_display_get_compositor (display);
        ClutterActor *window_group;
        ClutterActor *top_window_group;

        /* Imitate what the screen shield does to the window groups. */
        window_group = meta_compositor_get_window_group (compositor);
        top_window_group = meta_compositor_get_top_window_group (compositor);

        clutter_actor_hide (window_group);
        clutter_actor_hide (top_window_group);
        break;
      }
    case XDG_TOPLEVEL_SUSPENDED_COMMAND_HIDE_SCREEN_SHIELD:
      {
        MetaCompositor *compositor = meta_display_get_compositor (display);
        ClutterActor *window_group;
        ClutterActor *top_window_group;

        /* Imitate what the screen shield does to the window groups. */
        window_group = meta_compositor_get_window_group (compositor);
        top_window_group = meta_compositor_get_top_window_group (compositor);

        clutter_actor_show (window_group);
        clutter_actor_show (top_window_group);
        break;
      }
    }
}

static void
toplevel_suspended (void)
{
  MetaWaylandTestClient *wayland_test_client;
  gulong sync_point_id;
  MetaDisplay *display = meta_context_get_display (test_context);
  uint32_t now_ms = meta_display_get_current_time_roundtrip (display);
  MetaWorkspaceManager *workspace_manager =
    meta_display_get_workspace_manager (display);

  sync_point_id =
    g_signal_connect (test_driver, "sync-point",
                      G_CALLBACK (on_toplevel_suspended_sync_point),
                      NULL);

  meta_workspace_manager_update_num_workspaces (workspace_manager, now_ms, 2);

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-toplevel-suspended");
  meta_wayland_test_client_finish (wayland_test_client);

  g_signal_handler_disconnect (test_driver, sync_point_id);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/toplevel/suspended",
                   toplevel_suspended);
}

int
main (int   argc,
      char *argv[])
{
  meta_run_wayland_tests (argc, argv, init_tests);
}
