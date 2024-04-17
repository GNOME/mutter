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

#include "backends/meta-virtual-monitor.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
#include "compositor/meta-window-actor-private.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "meta/meta-later.h"
#include "meta/meta-workspace-manager.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland-client-private.h"
#include "wayland/meta-wayland-filter-manager.h"
#include "wayland/meta-wayland-surface-private.h"

#include "dummy-client-protocol.h"
#include "dummy-server-protocol.h"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;
static ClutterVirtualInputDevice *virtual_pointer;

static MetaWindow *
find_client_window (const char *title)
{
  return meta_find_client_window (test_context, title);
}

static void
subsurface_remap_toplevel (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "subsurface-remap-toplevel");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
buffer_transform (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "buffer-transform");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
buffer_single_pixel_buffer (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "single-pixel-buffer");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
buffer_ycbcr_basic (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "ycbcr");
  meta_wayland_test_client_finish (wayland_test_client);
}

static gboolean
set_true (gpointer user_data)
{
  gboolean *done = user_data;

  *done = TRUE;

  return G_SOURCE_REMOVE;
}

static void
idle_inhibit_instant_destroy (void)
{
  MetaWaylandTestClient *wayland_test_client;
  gboolean done;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "idle-inhibit");
  meta_wayland_test_client_finish (wayland_test_client);

  done = FALSE;
  g_timeout_add_seconds (1, set_true, &done);
  while (!done)
    g_main_context_iteration (NULL, TRUE);
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
  int fd;
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

  client1 = meta_wayland_client_new_indirect (test_context, &error);
  g_assert_nonnull (client1);
  g_assert_null (error);
  client2 = meta_wayland_client_new_indirect (test_context, &error);
  g_assert_nonnull (client2);
  g_assert_null (error);
  client3 = meta_wayland_client_new_indirect (test_context, &error);
  g_assert_nonnull (client3);
  g_assert_null (error);

  g_signal_connect (client1, "client-destroyed",
                    G_CALLBACK (on_client_destroyed), &client1_destroyed);
  g_signal_connect (client2, "client-destroyed",
                    G_CALLBACK (on_client_destroyed), &client2_destroyed);
  g_signal_connect (client3, "client-destroyed",
                    G_CALLBACK (on_client_destroyed), &client3_destroyed);

  dummy_global = wl_global_create (wayland_display,
                                   &dummy_interface,
                                   1, NULL, dummy_bind);
  meta_wayland_filter_manager_add_global (filter_manager,
                                          dummy_global,
                                          dummy_global_filter,
                                          client1);

  fd = meta_wayland_client_setup_fd (client1, &error);
  g_assert_cmpint (fd, >=, 0);
  g_assert_null (error);
  thread1 = g_thread_new ("test client thread 1",
                          test_client_thread_func,
                          GINT_TO_POINTER (fd));

  fd = meta_wayland_client_setup_fd (client2, &error);
  g_assert_cmpint (fd, >=, 0);
  g_assert_null (error);
  thread2 = g_thread_new ("test client thread 2",
                          test_client_thread_func,
                          GINT_TO_POINTER (fd));

  while (!client1_destroyed || !client2_destroyed)
    g_main_context_iteration (NULL, TRUE);

  client1_saw_global = GPOINTER_TO_INT (g_thread_join (thread1));
  client2_saw_global = GPOINTER_TO_INT (g_thread_join (thread2));

  g_assert_true (client1_saw_global);
  g_assert_false (client2_saw_global);

  meta_wayland_filter_manager_remove_global (filter_manager, dummy_global);
  wl_global_destroy (dummy_global);

  fd = meta_wayland_client_setup_fd (client3, &error);
  g_assert_cmpint (fd, >=, 0);
  g_assert_null (error);
  thread3 = g_thread_new ("test client thread 3",
                          test_client_thread_func,
                          GINT_TO_POINTER (fd));
  while (!client3_destroyed)
    g_main_context_iteration (NULL, TRUE);

  client3_saw_global = GPOINTER_TO_INT (g_thread_join (thread3));
  g_assert_false (client3_saw_global);
}

static void
subsurface_corner_cases (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "subsurface-corner-cases");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
subsurface_reparenting (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "subsurface-reparenting");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
subsurface_invalid_subsurfaces (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "invalid-subsurfaces");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
subsurface_invalid_xdg_shell_actions (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "invalid-xdg-shell-actions");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Invalid geometry * set on xdg_surface*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
on_after_paint (ClutterStage     *stage,
                ClutterStageView *view,
                ClutterFrame     *frame,
                gboolean         *was_painted)
{
  *was_painted = TRUE;
}

static void
wait_for_paint (ClutterActor *stage)
{
  gboolean was_painted = FALSE;
  gulong was_painted_id;

  was_painted_id = g_signal_connect (CLUTTER_STAGE (stage),
                                     "after-paint",
                                     G_CALLBACK (on_after_paint),
                                     &was_painted);

  while (!was_painted)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (stage, was_painted_id);
}

static gboolean
on_effects_completed_idle (gpointer user_data)
{
  MetaWindowActor *actor = user_data;
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaWindow *window = meta_window_actor_get_meta_window (actor);
  MtkRectangle buffer_rect;

  /* Move the window to a known position and perform a mouse click, allowing a
   * popup to be mapped. */

  meta_window_move_frame (window, FALSE, 0, 0);

  clutter_actor_queue_redraw (stage);
  clutter_stage_schedule_update (CLUTTER_STAGE (stage));

  wait_for_paint (stage);

  meta_window_get_buffer_rect (window, &buffer_rect);
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       CLUTTER_CURRENT_TIME,
                                                       buffer_rect.x + 10,
                                                       buffer_rect.y + 10);

  clutter_virtual_input_device_notify_button (virtual_pointer,
                                              CLUTTER_CURRENT_TIME,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);
  clutter_virtual_input_device_notify_button (virtual_pointer,
                                              CLUTTER_CURRENT_TIME,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_RELEASED);

  return G_SOURCE_REMOVE;
}

static void
on_effects_completed (MetaWindowActor *actor)
{
  g_idle_add (on_effects_completed_idle, actor);
}

static void
on_window_added (MetaStack  *stack,
                 MetaWindow *window)
{
  MetaWindowActor *actor = meta_window_actor_from_window (window);

  g_assert_nonnull (actor);

  if (g_strcmp0 (meta_window_get_title (window),
                 "subsurface-parent-unmapped") != 0)
    return;

  g_signal_connect (actor, "effects-completed",
                    G_CALLBACK (on_effects_completed),
                    NULL);
}

static void
on_window_actor_destroyed (MetaWindowActor       *actor,
                           MetaWaylandTestDriver *test_driver)
{
  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
}

static void
on_unmap_sync_point (MetaWaylandTestDriver *test_driver,
                     unsigned int           sequence,
                     struct wl_resource    *surface_resource,
                     struct wl_client      *wl_client)
{
  if (sequence == 0)
    {
      /* Dismiss popup by clicking outside. */

      clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                           CLUTTER_CURRENT_TIME,
                                                           390, 390);

      clutter_virtual_input_device_notify_button (virtual_pointer,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_PRESSED);
      clutter_virtual_input_device_notify_button (virtual_pointer,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_RELEASED);

      MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
      ClutterActor *actor = CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));
      MetaWindowActor *window_actor = meta_window_actor_from_actor (actor);
      g_signal_connect (window_actor, "destroy",
                        G_CALLBACK (on_window_actor_destroyed),
                        test_driver);
    }
  else if (sequence == 1)
    {
      MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
      ClutterActor *actor = CLUTTER_ACTOR (meta_wayland_surface_get_actor (surface));
      MetaWindowActor *window_actor = meta_window_actor_from_actor (actor);
      MetaWindow *window = meta_window_actor_get_meta_window (window_actor);
      MtkRectangle buffer_rect;

      /* Click inside the window to allow mapping a popup. */

      meta_window_get_buffer_rect (window, &buffer_rect);
      clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                           CLUTTER_CURRENT_TIME,
                                                           buffer_rect.x + 10,
                                                           buffer_rect.y + 10);

      clutter_virtual_input_device_notify_button (virtual_pointer,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_PRESSED);
      clutter_virtual_input_device_notify_button (virtual_pointer,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_RELEASED);
    }
}

static void
subsurface_parent_unmapped (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaWaylandTestClient *wayland_test_client;
  ClutterSeat *seat;
  gulong window_added_id;
  gulong sync_point_id;

  seat = meta_backend_get_default_seat (backend);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "subsurface-parent-unmapped");

  window_added_id =
    g_signal_connect (display->stack, "window-added",
                      G_CALLBACK (on_window_added),
                      virtual_pointer);
  sync_point_id =
    g_signal_connect (test_driver, "sync-point",
                      G_CALLBACK (on_unmap_sync_point),
                      NULL);

  meta_wayland_test_client_finish (wayland_test_client);

  g_clear_object (&virtual_pointer);
  g_signal_handler_disconnect (test_driver, sync_point_id);
  g_signal_handler_disconnect (display->stack, window_added_id);
}

static void
wait_for_sync_point (unsigned int sync_point)
{
  meta_wayland_test_driver_wait_for_sync_point (test_driver, sync_point);
}

static void
toplevel_apply_limits (void)
{
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-apply-limits");

  wait_for_sync_point (0);

  window = find_client_window ("toplevel-limits-test");
  g_assert_nonnull (window);
  g_assert_cmpint (window->size_hints.max_width, ==, 700);
  g_assert_cmpint (window->size_hints.max_height, ==, 500);
  g_assert_cmpint (window->size_hints.min_width, ==, 700);
  g_assert_cmpint (window->size_hints.min_height, ==, 500);

  wait_for_sync_point (1);

  window = find_client_window ("toplevel-limits-test");
  g_assert_null (window);

  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
toplevel_activation (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-activation");
  meta_wayland_test_client_finish (wayland_test_client);
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
set_struts (MtkRectangle  rect,
            MetaSide      side)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaWorkspaceManager *workspace_manager =
    meta_display_get_workspace_manager (display);
  GList *workspaces =
    meta_workspace_manager_get_workspaces (workspace_manager);
  MetaStrut strut;
  g_autoptr (GSList) struts = NULL;
  GList *l;

  strut = (MetaStrut) { .rect = rect, .side = side };
  struts = g_slist_append (NULL, &strut);

  for (l = workspaces; l; l = l->next)
    {
      MetaWorkspace *workspace = l->data;

      meta_workspace_set_builtin_struts (workspace, struts);
    }
}

static void
clear_struts (void)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaWorkspaceManager *workspace_manager =
    meta_display_get_workspace_manager (display);
  GList *workspaces =
    meta_workspace_manager_get_workspaces (workspace_manager);
  GList *l;

  for (l = workspaces; l; l = l->next)
    {
      MetaWorkspace *workspace = l->data;

      meta_workspace_set_builtin_struts (workspace, NULL);
    }
}

static MtkRectangle
get_primary_logical_monitor_layout (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;

  logical_monitor =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);
  return meta_logical_monitor_get_layout (logical_monitor);
}

static void
toplevel_bounds_struts (void)
{
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle logical_monitor_layout;
  MtkRectangle work_area;

  /*
   * This test case makes sure that setting and changing struts result in the
   * right bounds are sent.
   */

  logical_monitor_layout = get_primary_logical_monitor_layout ();
  set_struts ((MtkRectangle) {
                .x = 0,
                .y = 0,
                .width = logical_monitor_layout.width,
                .height = 10,
              },
              META_SIDE_TOP);

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-toplevel-bounds");

  wait_for_sync_point (1);
  wait_until_after_paint ();

  window = find_client_window ("toplevel-bounds-test");

  g_assert_nonnull (window->monitor);
  meta_window_get_work_area_current_monitor (window, &work_area);
  g_assert_cmpint (work_area.width, ==, logical_monitor_layout.width);
  g_assert_cmpint (work_area.height, ==, logical_monitor_layout.height - 10);

  g_assert_cmpint (window->rect.width, ==, work_area.width - 10);
  g_assert_cmpint (window->rect.height, ==, work_area.height - 10);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  clear_struts ();

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-toplevel-bounds");

  wait_for_sync_point (1);
  wait_until_after_paint ();

  window = find_client_window ("toplevel-bounds-test");
  g_assert_nonnull (window->monitor);
  meta_window_get_work_area_current_monitor (window, &work_area);
  g_assert_cmpint (work_area.width, ==, logical_monitor_layout.width);
  g_assert_cmpint (work_area.height, ==, logical_monitor_layout.height);

  g_assert_cmpint (window->rect.width, ==, work_area.width - 10);
  g_assert_cmpint (window->rect.height, ==, work_area.height - 10);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
wait_for_cursor_position (float x,
                          float y)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  graphene_point_t point;

  while (TRUE)
    {
      meta_cursor_tracker_get_pointer (cursor_tracker, &point, NULL);
      if (G_APPROX_VALUE (x, point.x, FLT_EPSILON) &&
          G_APPROX_VALUE (y, point.y, FLT_EPSILON))
        break;

      g_main_context_iteration (NULL, TRUE);
    }
}

static void
toplevel_bounds_monitors (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat;
  g_autoptr (MetaVirtualMonitor) second_virtual_monitor = NULL;
  MetaWaylandTestClient *wayland_test_client;
  MtkRectangle logical_monitor_layout;
  MtkRectangle work_area;
  MetaWindow *window;

  /*
   * This test case creates two monitors, with different sizes, with a fake
   * panel on top of the primary monitor. It then makes sure launching on both
   * monitors results in the correct bounds.
   */

  seat = meta_backend_get_default_seat (backend);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  second_virtual_monitor = meta_create_test_monitor (test_context,
                                                     300, 200, 60.0);

  logical_monitor_layout = get_primary_logical_monitor_layout ();
  set_struts ((MtkRectangle) {
                .x = 0,
                .y = 0,
                .width = logical_monitor_layout.width,
                .height = 10,
              },
              META_SIDE_TOP);

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-toplevel-bounds");

  wait_for_sync_point (1);
  wait_until_after_paint ();

  window = find_client_window ("toplevel-bounds-test");

  g_assert_nonnull (window->monitor);
  meta_window_get_work_area_current_monitor (window, &work_area);
  g_assert_cmpint (work_area.width, ==, logical_monitor_layout.width);
  g_assert_cmpint (work_area.height, ==, logical_monitor_layout.height - 10);

  g_assert_cmpint (window->rect.width, ==, work_area.width - 10);
  g_assert_cmpint (window->rect.height, ==, work_area.height - 10);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       CLUTTER_CURRENT_TIME,
                                                       700.0, 100.0);
  wait_for_cursor_position (700.0, 100.0);

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-toplevel-bounds");

  wait_for_sync_point (1);
  wait_until_after_paint ();

  window = find_client_window ("toplevel-bounds-test");

  g_assert_nonnull (window->monitor);
  meta_window_get_work_area_current_monitor (window, &work_area);
  g_assert_cmpint (work_area.width, ==, 300);
  g_assert_cmpint (work_area.height, ==, 200);

  g_assert_cmpint (window->rect.width, ==, 300 - 10);
  g_assert_cmpint (window->rect.height, ==, 200 - 10);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
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

static void
toplevel_show_states (void)
{
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "toplevel-show-states");

  wait_for_sync_point (0);
  window = find_client_window ("showing-states");

  g_assert_true (meta_window_should_show (window));
  g_assert_false (meta_window_should_be_showing (window));

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  wait_for_sync_point (1);

  g_assert_true (meta_window_should_show (window));
  g_assert_true (meta_window_should_be_showing (window));

  meta_wayland_test_client_finish (wayland_test_client);
}

enum
{
  XDG_TOPLEVEL_SUSPENDED_COMMAND_NEXT_WORKSPACE = 0,
  XDG_TOPLEVEL_SUSPENDED_COMMAND_PREV_WORKSPACE = 1,
  XDG_TOPLEVEL_SUSPENDED_COMMAND_ACTIVATE_WINDOW = 2,
};

static void
on_toplevel_suspended_sync_point (MetaWaylandTestDriver *test_driver,
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
    }
}

static void
toplevel_suspended (void)
{
  MetaWaylandTestClient *wayland_test_client;
  gulong sync_point_id;

  sync_point_id =
    g_signal_connect (test_driver, "sync-point",
                      G_CALLBACK (on_toplevel_suspended_sync_point),
                      NULL);

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-toplevel-suspended");
  meta_wayland_test_client_finish (wayland_test_client);

  g_signal_handler_disconnect (test_driver, sync_point_id);
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
#ifdef MUTTER_PRIVILEGED_TEST
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  MetaKmsDevice *kms_device = meta_kms_get_devices (kms)->data;
#endif

  test_driver = meta_wayland_test_driver_new (compositor);

#ifdef MUTTER_PRIVILEGED_TEST
  meta_wayland_test_driver_set_property (test_driver,
                                         "gpu-path",
                                         meta_kms_device_get_path (kms_device));

  meta_set_custom_monitor_config_full (backend,
                                       "vkms-640x480.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);
#else
  virtual_monitor = meta_create_test_monitor (test_context,
                                              640, 480, 60.0);
#endif
  meta_monitor_manager_reload (monitor_manager);
}

static void
on_after_tests (void)
{
  g_clear_object (&test_driver);
  g_clear_object (&virtual_monitor);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/buffer/transform",
                   buffer_transform);
  g_test_add_func ("/wayland/buffer/single-pixel-buffer",
                   buffer_single_pixel_buffer);
  g_test_add_func ("/wayland/buffer/ycbcr-basic",
                   buffer_ycbcr_basic);
  g_test_add_func ("/wayland/idle-inhibit/instant-destroy",
                   idle_inhibit_instant_destroy);
  g_test_add_func ("/wayland/registry/filter",
                   registry_filter);
  g_test_add_func ("/wayland/subsurface/remap-toplevel",
                   subsurface_remap_toplevel);
  g_test_add_func ("/wayland/subsurface/reparent",
                   subsurface_reparenting);
  g_test_add_func ("/wayland/subsurface/invalid-subsurfaces",
                   subsurface_invalid_subsurfaces);
  g_test_add_func ("/wayland/subsurface/invalid-xdg-shell-actions",
                   subsurface_invalid_xdg_shell_actions);
  g_test_add_func ("/wayland/subsurface/corner-cases",
                   subsurface_corner_cases);
  g_test_add_func ("/wayland/subsurface/parent-unmapped",
                   subsurface_parent_unmapped);
  g_test_add_func ("/wayland/toplevel/apply-limits",
                   toplevel_apply_limits);
  g_test_add_func ("/wayland/toplevel/activation",
                   toplevel_activation);
#ifdef MUTTER_PRIVILEGED_TEST
  (void)(toplevel_bounds_struts);
  (void)(toplevel_bounds_monitors);
#else
  g_test_add_func ("/wayland/toplevel/bounds/struts",
                   toplevel_bounds_struts);
  g_test_add_func ("/wayland/toplevel/bounds/monitors",
                   toplevel_bounds_monitors);
#endif
  g_test_add_func ("/wayland/xdg-foreign/set-parent-of",
                   xdg_foreign_set_parent_of);
  g_test_add_func ("/wayland/toplevel/show-states",
                   toplevel_show_states);
  g_test_add_func ("/wayland/toplevel/suspended",
                   toplevel_suspended);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

#ifdef MUTTER_PRIVILEGED_TEST
  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
#else
  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
#endif
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
