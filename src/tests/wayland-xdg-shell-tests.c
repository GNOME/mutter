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

#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-logical-monitor-private.h"
#include "core/meta-workspace-manager-private.h"
#include "core/window-private.h"
#include "tests/meta-ref-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-runner.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland-window-configuration.h"
#include "wayland/meta-window-wayland.h"

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
wait_for_paint (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

  meta_wait_for_paint (stage);
}

static void
invalid_xdg_shell_actions (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "invalid-xdg-shell-actions");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Invalid geometry * set on xdg_surface*");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Buggy client * committed initial non-empty content*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
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
toplevel_invalid_limits (void)
{
  GSettings *settings = g_settings_new ("org.gnome.mutter");
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle rect;

  g_assert_true (g_settings_set_boolean (settings, "center-new-windows", TRUE));

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "invalid-size-limits-on-map-client");

  while (!(window = find_client_window ("invalid-size-limits-client")))
    g_main_context_iteration (NULL, TRUE);
  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);

  rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (window->size_hints.max_width, ==, 200);
  g_assert_cmpint (window->size_hints.max_height, ==, 200);
  g_assert_cmpint (window->size_hints.max_width, ==, 200);
  g_assert_cmpint (window->size_hints.max_height, ==, 200);
  g_assert_cmpint (rect.width, ==, 250);
  g_assert_cmpint (rect.height, ==, 250);
  g_assert_cmpint (rect.x, ==, 195);
  g_assert_cmpint (rect.y, ==, 115);

  meta_wayland_test_driver_terminate (test_driver);
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
toplevel_invalid_geometry_basic (void)
{
  GSettings *settings = g_settings_new ("org.gnome.mutter");
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle rect;

  g_assert_true (g_settings_set_boolean (settings, "center-new-windows", TRUE));

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "invalid-geometry");

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Client provided invalid window geometry for "
                         "xdg_surface*");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Client provided invalid window geometry for "
                         "xdg_surface*");

  while (!(window = find_client_window ("invalid-geometry")))
    g_main_context_iteration (NULL, TRUE);
  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);

  rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (rect.width, ==, 200);
  g_assert_cmpint (rect.height, ==, 200);
  g_assert_cmpint (rect.x, ==, 220);
  g_assert_cmpint (rect.y, ==, 140);

  meta_wayland_test_driver_terminate (test_driver);
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static MetaWindow *
map_test_window (MetaTestClient *test_client,
                 const char     *script)
{
  MetaWindow *window;
  GError *error = NULL;

  meta_test_client_run (test_client, script);

  while (!(window = meta_test_client_find_window (test_client, "1", &error)))
    {
      g_assert_no_error (error);
      g_main_context_iteration (NULL, TRUE);
    }
  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);
  meta_wait_for_effects (window);

  return window;
}

static void
toplevel_invalid_geometry_subsurface (void)
{
  GSettings *settings = g_settings_new ("org.gnome.mutter");
  GError *error = NULL;
  MetaTestClient *test_client;
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle rect;

  g_assert_true (g_settings_set_boolean (settings, "center-new-windows", TRUE));

  test_client = meta_test_client_new (test_context,
                                      "1",
                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  g_assert_no_error (error);
  map_test_window (test_client,
                   "create 1 csd\n"
                   "resize 1 400 400\n"
                   "show 1\n");

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "invalid-geometry",
                                            "with-subsurface",
                                            NULL);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Client provided invalid window geometry for "
                         "xdg_surface*");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Client provided invalid window geometry for "
                         "xdg_surface*");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Client provided invalid window geometry for "
                         "xdg_surface*");

  while (!(window = find_client_window ("invalid-geometry")))
    g_main_context_iteration (NULL, TRUE);
  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);

  rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (rect.width, ==, 200);
  g_assert_cmpint (rect.height, ==, 200);
  g_assert_cmpint (rect.x, ==, 220);
  g_assert_cmpint (rect.y, ==, 140);

  meta_wayland_test_driver_terminate (test_driver);
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();

  meta_test_client_destroy (test_client);
}

static void
set_struts (MtkRectangle rect,
            MetaSide     side)
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

static void
toplevel_bounds_struts (void)
{
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle logical_monitor_layout;
  MtkRectangle work_area;
  MtkRectangle frame_rect;

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
  wait_for_paint ();

  window = find_client_window ("toplevel-bounds-test");

  g_assert_nonnull (window->monitor);
  meta_window_get_work_area_current_monitor (window, &work_area);
  g_assert_cmpint (work_area.width, ==, logical_monitor_layout.width);
  g_assert_cmpint (work_area.height, ==, logical_monitor_layout.height - 10);

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.width, ==, work_area.width - 10);
  g_assert_cmpint (frame_rect.height, ==, work_area.height - 10);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  clear_struts ();

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-toplevel-bounds");

  wait_for_sync_point (1);
  wait_for_paint ();

  window = find_client_window ("toplevel-bounds-test");
  g_assert_nonnull (window->monitor);
  meta_window_get_work_area_current_monitor (window, &work_area);
  g_assert_cmpint (work_area.width, ==, logical_monitor_layout.width);
  g_assert_cmpint (work_area.height, ==, logical_monitor_layout.height);

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.width, ==, work_area.width - 10);
  g_assert_cmpint (frame_rect.height, ==, work_area.height - 10);

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
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  MetaWaylandTestClient *wayland_test_client;
  MtkRectangle logical_monitor_layout;
  MtkRectangle work_area;
  MtkRectangle frame_rect;
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
  wait_for_paint ();

  window = find_client_window ("toplevel-bounds-test");

  g_assert_nonnull (window->monitor);
  meta_window_get_work_area_current_monitor (window, &work_area);
  g_assert_cmpint (work_area.width, ==, logical_monitor_layout.width);
  g_assert_cmpint (work_area.height, ==, logical_monitor_layout.height - 10);

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.width, ==, work_area.width - 10);
  g_assert_cmpint (frame_rect.height, ==, work_area.height - 10);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       CLUTTER_CURRENT_TIME,
                                                       700.0, 100.0);
  wait_for_cursor_position (700.0, 100.0);

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-toplevel-bounds");

  wait_for_sync_point (1);
  wait_for_paint ();

  window = find_client_window ("toplevel-bounds-test");

  g_assert_nonnull (window->monitor);
  meta_window_get_work_area_current_monitor (window, &work_area);
  g_assert_cmpint (work_area.width, ==, 300);
  g_assert_cmpint (work_area.height, ==, 200);

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.width, ==, 300 - 10);
  g_assert_cmpint (frame_rect.height, ==, 200 - 10);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
  g_clear_object (&virtual_pointer);
}

static void
toplevel_reuse_surface (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "toplevel-reuse-surface");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
toplevel_fixed_size_fullscreen (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaWaylandTestClient *wayland_test_client;
  GSettings *settings;
  GList *views;
  ClutterStageView *view;
  MetaWindow *window;

  meta_cursor_tracker_inhibit_cursor_visibility (cursor_tracker);

  settings = g_settings_new ("org.gnome.mutter");
  g_assert_true (g_settings_set_boolean (settings, "center-new-windows", FALSE));

  views = meta_renderer_get_views (renderer);
  g_assert_cmpint (g_list_length (views), ==, 1);
  view = CLUTTER_STAGE_VIEW (views->data);

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "fixed-size-client",
                                            "100", "100",
                                            NULL);

  while (!(window = find_client_window ("fixed-size-client")))
    g_main_context_iteration (NULL, TRUE);
  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *) &window);

  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);
  meta_wait_for_effects (window);

  meta_ref_test_verify_view (view,
                             g_test_get_path (), 0,
                             meta_ref_test_determine_ref_test_flag ());

  meta_window_make_fullscreen (window);
  meta_wait_wayland_window_reconfigure (window);
  meta_wait_for_effects (window);

  meta_ref_test_verify_view (view,
                             g_test_get_path (), 1,
                             meta_ref_test_determine_ref_test_flag ());

  meta_wayland_test_driver_terminate (test_driver);
  meta_wayland_test_client_finish (wayland_test_client);

  meta_cursor_tracker_uninhibit_cursor_visibility (cursor_tracker);

  while (window)
    g_main_context_iteration (NULL, TRUE);
}

static void
toplevel_fixed_size_fullscreen_exceeds (void)
{
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "fixed-size-client",
                                            "1000", "1000",
                                            NULL);

  while (!(window = find_client_window ("fixed-size-client")))
    g_main_context_iteration (NULL, TRUE);
  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *) &window);

  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);
  meta_wait_for_effects (window);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Window * (fixed-size-client) (wl_surface#*) "
                         "size 1000x1000 exceeds allowed maximum size 640x480");

  meta_window_make_fullscreen (window);
  meta_wait_wayland_window_reconfigure (window);
  meta_wait_for_effects (window);

  meta_wayland_test_driver_terminate (test_driver);
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();

  while (window)
    g_main_context_iteration (NULL, TRUE);
}

static void
toplevel_focus_changes_remembers_size (void)
{
  GSettings *settings;
  MetaWindow *window;
  MetaWindowWayland *wl_window;
  MtkRectangle rect;
  MetaTestClient *test_client;
  GError *error = NULL;
  uint32_t serial;
  MetaWaylandWindowConfiguration *pending_configuration;

  settings = g_settings_new ("org.gnome.mutter");
  g_assert_true (g_settings_set_boolean (settings, "center-new-windows", TRUE));

  test_client = meta_test_client_new (test_context,
                                      "1",
                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  g_assert_no_error (error);
  meta_test_client_run (test_client,
                        "create 1 csd\n"
                        "resize 1 200 200\n"
                        "maximize 1\n"
                        "show 1\n");

  while (!(window = meta_test_client_find_window (test_client, "1", &error)))
    {
      g_assert_no_error (error);
      g_main_context_iteration (NULL, TRUE);
    }
  wl_window = META_WINDOW_WAYLAND (window);
  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);
  meta_wait_for_effects (window);

  rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (rect.x, ==, 0);
  g_assert_cmpint (rect.y, ==, 0);
  g_assert_cmpint (rect.width, ==, 640);
  g_assert_cmpint (rect.height, ==, 480);

  meta_window_unmaximize (window);
  meta_wait_wayland_window_reconfigure (window);
  meta_wait_for_effects (window);
  rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (rect.x, ==, 220);
  g_assert_cmpint (rect.y, ==, 140);
  g_assert_cmpint (rect.width, ==, 200);
  g_assert_cmpint (rect.height, ==, 200);

  g_assert_true (meta_window_appears_focused (window));

  /* Make the window unfocused by opening another window. */

  g_assert_false (meta_window_wayland_get_pending_serial (wl_window, &serial));

  meta_test_client_run (test_client,
                        "create 2 csd\n"
                        "show 2\n");

  while (meta_window_appears_focused (window))
    g_main_context_iteration (NULL, TRUE);
  g_assert_true (meta_window_wayland_get_pending_serial (wl_window, &serial));
  pending_configuration =
    meta_window_wayland_peek_configuration (wl_window, serial);
  g_assert_nonnull (pending_configuration);
  g_assert_true (pending_configuration->has_size);
  g_assert_cmpint (pending_configuration->width, ==, 200);
  g_assert_cmpint (pending_configuration->height, ==, 200);
  meta_wait_wayland_window_reconfigure (window);
  g_assert_cmpint (rect.x, ==, 220);
  g_assert_cmpint (rect.y, ==, 140);
  g_assert_cmpint (rect.width, ==, 200);
  g_assert_cmpint (rect.height, ==, 200);
  meta_test_client_destroy (test_client);
}

static void
toplevel_begin_interactive_resize (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterStage *stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  ClutterSprite *pointer_sprite;
  GSettings *settings;
  MetaWindow *window;
  MetaWindowWayland *wl_window;
  MtkRectangle rect;
  MetaTestClient *test_client;
  GError *error = NULL;
  gboolean ret;
  uint32_t serial;
  MetaWaylandWindowConfiguration *pending_configuration;

  settings = g_settings_new ("org.gnome.mutter");
  g_assert_true (g_settings_set_boolean (settings, "center-new-windows", TRUE));

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       0.0f, 0.0f);
  meta_flush_input (test_context);

  test_client = meta_test_client_new (test_context,
                                      __func__,
                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  g_assert_no_error (error);
  meta_test_client_run (test_client,
                        "create 1 csd\n"
                        "resize 1 200 200\n"
                        "show 1\n");

  while (!(window = meta_test_client_find_window (test_client, "1", &error)))
    {
      g_assert_no_error (error);
      g_main_context_iteration (NULL, TRUE);
    }
  wl_window = META_WINDOW_WAYLAND (window);
  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);
  meta_wait_for_effects (window);

  rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (rect.x, ==, 220);
  g_assert_cmpint (rect.y, ==, 140);
  g_assert_cmpint (rect.width, ==, 200);
  g_assert_cmpint (rect.height, ==, 200);

  pointer_sprite = clutter_backend_get_pointer_sprite (clutter_backend, stage);
  ret = meta_window_begin_grab_op (window,
                                   META_GRAB_OP_RESIZING_E,
                                   pointer_sprite,
                                   meta_display_get_current_time_roundtrip (window->display),
                                   NULL);
  g_assert_true (ret);

  g_assert_true (meta_window_wayland_get_pending_serial (wl_window, &serial));
  pending_configuration =
    meta_window_wayland_peek_configuration (wl_window, serial);
  g_assert_nonnull (pending_configuration);
  g_assert_true (pending_configuration->has_size);
  g_assert_cmpint (pending_configuration->width, ==, 200);
  g_assert_cmpint (pending_configuration->height, ==, 200);
  meta_wait_wayland_window_reconfigure (window);
  rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (rect.x, ==, 220);
  g_assert_cmpint (rect.y, ==, 140);
  g_assert_cmpint (rect.width, ==, 200);
  g_assert_cmpint (rect.height, ==, 200);

  clutter_virtual_input_device_notify_relative_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       10.0f, 0.0f);
  meta_flush_input (test_context);
  meta_wait_for_update (test_context);
  g_assert_true (meta_window_wayland_get_pending_serial (wl_window, &serial));
  pending_configuration =
    meta_window_wayland_peek_configuration (wl_window, serial);
  g_assert_nonnull (pending_configuration);
  g_assert_true (pending_configuration->has_size);
  g_assert_cmpint (pending_configuration->width, ==, 210);
  g_assert_cmpint (pending_configuration->height, ==, 200);
  meta_wait_wayland_window_reconfigure (window);
  rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (rect.x, ==, 220);
  g_assert_cmpint (rect.y, ==, 140);
  g_assert_cmpint (rect.width, ==, 210);
  g_assert_cmpint (rect.height, ==, 200);


  meta_test_client_destroy (test_client);
  g_clear_object (&virtual_pointer);
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

static void
init_tests (void)
{
  g_test_add_func ("/wayland/invalid-xdg-shell-actions",
                   invalid_xdg_shell_actions);
  g_test_add_func ("/wayland/toplevel/apply-limits",
                   toplevel_apply_limits);
  g_test_add_func ("/wayland/toplevel/invalid-limits",
                   toplevel_invalid_limits);
  g_test_add_func ("/wayland/toplevel/invalid-geometry/basic",
                   toplevel_invalid_geometry_basic);
  g_test_add_func ("/wayland/toplevel/invalid-geometry/subsurface",
                   toplevel_invalid_geometry_subsurface);
  g_test_add_func ("/wayland/toplevel/reuse-surface",
                   toplevel_reuse_surface);
  g_test_add_func ("/wayland/toplevel/fixed-size-fullscreen",
                   toplevel_fixed_size_fullscreen);
  g_test_add_func ("/wayland/toplevel/fixed-size-fullscreen-exceeds",
                   toplevel_fixed_size_fullscreen_exceeds);
  g_test_add_func ("/wayland/toplevel/focus-changes-remembers-size",
                   toplevel_focus_changes_remembers_size);
  g_test_add_func ("/wayland/toplevel/begin-interactive-resize",
                   toplevel_begin_interactive_resize);
  g_test_add_func ("/wayland/toplevel/show-states",
                   toplevel_show_states);
#ifdef MUTTER_PRIVILEGED_TEST
  (void)(toplevel_bounds_struts);
  (void)(toplevel_bounds_monitors);
#else
  g_test_add_func ("/wayland/toplevel/bounds/struts",
                   toplevel_bounds_struts);
  g_test_add_func ("/wayland/toplevel/bounds/monitors",
                   toplevel_bounds_monitors);
#endif
}

int
main (int   argc,
      char *argv[])
{
  meta_run_wayland_tests (argc, argv, init_tests);
}
