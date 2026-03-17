/*
 * Copyright (C) 2019 Red Hat, Inc.
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

#include <gio/gio.h>
#include <wayland-client.h>
#include <gdesktop-enums.h>

#include "backends/meta-virtual-monitor.h"
#include "compositor/meta-window-actor-private.h"
#include "core/display-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "meta/meta-later.h"
#include "meta/meta-workspace-manager.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-ref-test.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-runner.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland-client-private.h"
#include "wayland/meta-wayland-filter-manager.h"
#include "wayland/meta-wayland-surface-private.h"

#include "dummy-client-protocol.h"
#include "dummy-server-protocol.h"

static void
wait_for_sync_point (unsigned int sync_point)
{
  meta_wayland_test_driver_wait_for_sync_point (test_driver, sync_point);
}

static MetaWindow *
find_client_window (const char *title)
{
  return meta_find_client_window (test_context, title);
}

static MetaWaylandAccess
dummy_global_filter (const struct wl_client *client,
                     const struct wl_global *global,
                     gpointer                user_data)
{
  MetaWaylandClient *allowed_client = META_WAYLAND_CLIENT (user_data);

  if (g_object_get_data (G_OBJECT (allowed_client),
                         "test-client-destroyed"))
    return META_WAYLAND_ACCESS_DENIED;
  else if (meta_wayland_client_matches (allowed_client, client))
    return META_WAYLAND_ACCESS_ALLOWED;
  else
    return META_WAYLAND_ACCESS_DENIED;
}

static void
dummy_bind (struct wl_client *client,
            void             *data,
            uint32_t          version,
            uint32_t          id)

{
  g_assert_not_reached ();
}

static void
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  gboolean *global_seen = user_data;

  if (strcmp (interface, dummy_interface.name) == 0)
    *global_seen = TRUE;
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

static gpointer
test_client_thread_func (gpointer user_data)
{
  int fd = GPOINTER_TO_INT (user_data);
  struct wl_display *wl_display;
  struct wl_registry *wl_registry;
  gboolean global_seen = FALSE;

  wl_display = wl_display_connect_to_fd (fd);
  g_assert_nonnull (wl_display);

  wl_registry = wl_display_get_registry (wl_display);
  wl_registry_add_listener (wl_registry, &registry_listener, &global_seen);
  wl_display_roundtrip (wl_display);
  wl_registry_destroy (wl_registry);

  wl_display_disconnect (wl_display);

  return GINT_TO_POINTER (global_seen);
}

static void
on_client_destroyed (MetaWaylandClient *client,
                     gboolean          *client_destroyed)
{
  *client_destroyed = TRUE;
  g_object_set_data (G_OBJECT (client), "test-client-destroyed",
                     GINT_TO_POINTER (TRUE));
}

static void
registry_filter (void)
{
  g_autoptr (GError) error = NULL;
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (test_context);
  MetaWaylandFilterManager *filter_manager =
    meta_wayland_compositor_get_filter_manager (wayland_compositor);
  struct wl_display *wayland_display =
    meta_wayland_compositor_get_wayland_display (wayland_compositor);
  struct wl_global *dummy_global;
  int fd1;
  int fd2;
  int fd3;
  g_autoptr (MetaWaylandClient) client1 = NULL;
  g_autoptr (MetaWaylandClient) client2 = NULL;
  g_autoptr (MetaWaylandClient) client3 = NULL;
  GThread *thread1;
  GThread *thread2;
  GThread *thread3;
  gboolean client1_destroyed = FALSE;
  gboolean client2_destroyed = FALSE;
  gboolean client3_destroyed = FALSE;
  gboolean client1_saw_global;
  gboolean client2_saw_global;
  gboolean client3_saw_global;

  client1 = meta_wayland_client_new_create (test_context, getpid (), &error);
  g_assert_nonnull (client1);
  g_assert_null (error);
  fd1 = meta_wayland_client_take_client_fd (client1);
  g_assert_cmpint (fd1, >=, 0);
  client2 = meta_wayland_client_new_create (test_context, getpid (), &error);
  g_assert_nonnull (client2);
  g_assert_null (error);
  fd2 = meta_wayland_client_take_client_fd (client2);
  g_assert_cmpint (fd2, >=, 0);

  g_signal_connect (client1, "client-destroyed",
                    G_CALLBACK (on_client_destroyed), &client1_destroyed);
  g_signal_connect (client2, "client-destroyed",
                    G_CALLBACK (on_client_destroyed), &client2_destroyed);

  dummy_global = wl_global_create (wayland_display,
                                   &dummy_interface,
                                   1, NULL, dummy_bind);
  meta_wayland_filter_manager_add_global (filter_manager,
                                          dummy_global,
                                          dummy_global_filter,
                                          client1);

  thread1 = g_thread_new ("test client thread 1",
                          test_client_thread_func,
                          GINT_TO_POINTER (fd1));

  thread2 = g_thread_new ("test client thread 2",
                          test_client_thread_func,
                          GINT_TO_POINTER (fd2));

  while (!client1_destroyed || !client2_destroyed)
    g_main_context_iteration (NULL, TRUE);

  client1_saw_global = GPOINTER_TO_INT (g_thread_join (thread1));
  client2_saw_global = GPOINTER_TO_INT (g_thread_join (thread2));

  g_assert_true (client1_saw_global);
  g_assert_false (client2_saw_global);

  meta_wayland_filter_manager_remove_global (filter_manager, dummy_global);
  wl_global_destroy (dummy_global);

  client3 = meta_wayland_client_new_create (test_context, getpid (), &error);
  g_assert_nonnull (client3);
  g_assert_null (error);
  fd3 = meta_wayland_client_take_client_fd (client3);
  g_assert_cmpint (fd3, >=, 0);

  g_signal_connect (client3, "client-destroyed",
                    G_CALLBACK (on_client_destroyed), &client3_destroyed);

  thread3 = g_thread_new ("test client thread 3",
                          test_client_thread_func,
                          GINT_TO_POINTER (fd3));

  while (!client3_destroyed)
    g_main_context_iteration (NULL, TRUE);

  client3_saw_global = GPOINTER_TO_INT (g_thread_join (thread3));
  g_assert_false (client3_saw_global);
}

static gboolean
mark_later_as_done (gpointer user_data)
{
  gboolean *done = user_data;

  *done = TRUE;

  return G_SOURCE_REMOVE;
}

static void
wait_until_after_paint (void)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaLaters *laters = meta_compositor_get_laters (compositor);
  gboolean done;

  done = FALSE;
  meta_laters_add (laters,
                   META_LATER_BEFORE_REDRAW,
                   mark_later_as_done,
                   &done,
                   NULL);
  while (!done)
    g_main_context_iteration (NULL, FALSE);

  done = FALSE;
  meta_laters_add (laters,
                   META_LATER_IDLE,
                   mark_later_as_done,
                   &done,
                   NULL);
  while (!done)
    g_main_context_iteration (NULL, FALSE);
}

static void
xdg_foreign_set_parent_of (void)
{
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window1;
  MetaWindow *window2;
  MetaWindow *window3;
  MetaWindow *window4;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-foreign");

  wait_for_sync_point (0);
  wait_until_after_paint ();

  window1 = find_client_window ("xdg-foreign-window1");
  window2 = find_client_window ("xdg-foreign-window2");
  window3 = find_client_window ("xdg-foreign-window3");
  window4 = find_client_window ("xdg-foreign-window4");

  g_assert_true (meta_window_get_transient_for (window4) ==
                 window3);
  g_assert_true (meta_window_get_transient_for (window3) ==
                 window2);
  g_assert_true (meta_window_get_transient_for (window2) ==
                 window1);
  g_assert_null (meta_window_get_transient_for (window1));

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);

  meta_wayland_test_client_finish (wayland_test_client);
}

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
toplevel_tag (void)
{
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-toplevel-tag");
  window = meta_wait_for_client_window (test_context, "toplevel-tag");
  g_assert_null (meta_window_get_tag (window));

  wait_for_sync_point (0);
  g_assert_cmpstr (meta_window_get_tag (window), ==, "topleveltag-test");
  meta_wayland_test_driver_emit_sync_event (test_driver, 0);

  meta_wayland_test_client_finish (wayland_test_client);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/registry/filter",
                   registry_filter);
  g_test_add_func ("/wayland/xdg-foreign/set-parent-of",
                   xdg_foreign_set_parent_of);
  g_test_add_func ("/wayland/toplevel/suspended",
                   toplevel_suspended);
  g_test_add_func ("/wayland/toplevel/tag",
                   toplevel_tag);
}

int
main (int   argc,
      char *argv[])
{
  meta_run_wayland_tests (argc, argv, init_tests);
}
