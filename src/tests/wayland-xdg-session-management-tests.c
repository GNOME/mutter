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

#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "core/window-private.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-runner.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-window-wayland.h"

static void
wait_for_sync_point (unsigned int sync_point)
{
  meta_wayland_test_driver_wait_for_sync_point (test_driver, sync_point);
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
toplevel_sessions_basic (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-session-management");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
toplevel_sessions_replace (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-session-management-replace");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
on_session_instantiated (MetaSessionManager *session_manager,
                         const char         *name,
                         MetaSessionState   *state,
                         gpointer            user_data)
{
  char **session_id = user_data;

  *session_id = g_strdup (name);
}

static void
toplevel_sessions_restore (void)
{
  MetaSessionManager *session_manager;
  MetaWaylandTestClient *wayland_test_client;
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaWindow *window;
  MtkRectangle frame_rect;
  g_autofree char *session_id = NULL;

  session_manager = meta_context_get_session_manager (test_context);
  g_signal_connect (session_manager, "session-instantiated",
                    G_CALLBACK (on_session_instantiated), &session_id);

  /* Launch client once, resize window */
  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-session-management-restore");

  wait_for_sync_point (0);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);

  g_assert_nonnull (window->monitor);
  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.width, ==, 100);
  g_assert_cmpint (frame_rect.height, ==, 100);

  meta_window_move_resize_frame (window, FALSE, 123, 234, 200, 200);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  g_assert_nonnull (session_id);

  /* Launch client again, check window size persists */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "xdg-session-management-restore",
                                            session_id,
                                            NULL);

  wait_for_sync_point (0);

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);

  g_assert_nonnull (window->monitor);
  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, 123);
  g_assert_cmpint (frame_rect.y, ==, 234);
  g_assert_cmpint (frame_rect.width, ==, 200);
  g_assert_cmpint (frame_rect.height, ==, 200);

  g_signal_handlers_disconnect_by_func (session_manager,
                                        on_session_instantiated,
                                        &session_id);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
toplevel_sessions_restore_fullscreen (void)
{
  MetaSessionManager *session_manager;
  g_autoptr (MetaVirtualMonitor) second_virtual_monitor = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle frame_rect, monitor_layout;
  g_autofree char *session_id = NULL;

  monitor_layout = get_primary_logical_monitor_layout ();

  second_virtual_monitor = meta_create_test_monitor (test_context,
                                                     800, 600, 60.0);

  session_manager = meta_context_get_session_manager (test_context);
  g_signal_connect (session_manager, "session-instantiated",
                    G_CALLBACK (on_session_instantiated), &session_id);

  /* Launch client once, resize window */
  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-session-management-restore");

  wait_for_sync_point (0);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);

  /* Move to second monitor */
  meta_window_move_resize_frame (window, FALSE,
                                 monitor_layout.width + 123, 123, 100, 100);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, monitor_layout.width + 123);
  g_assert_cmpint (frame_rect.y, ==, 123);
  g_assert_cmpint (frame_rect.width, ==, 100);
  g_assert_cmpint (frame_rect.height, ==, 100);

  meta_window_make_fullscreen (window);

  while (!meta_window_wayland_is_acked_fullscreen (META_WINDOW_WAYLAND (window)))
    g_main_context_iteration (NULL, TRUE);

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, 640);
  g_assert_cmpint (frame_rect.y, ==, 0);
  g_assert_cmpint (frame_rect.width, ==, 800);
  g_assert_cmpint (frame_rect.height, ==, 600);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  g_assert_nonnull (session_id);

  /* Launch client again, check window persists fullscreen on second monitor */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "xdg-session-management-restore",
                                            session_id,
                                            NULL);

  wait_for_sync_point (0);

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);

  g_assert_nonnull (window->monitor);
  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, monitor_layout.width);
  g_assert_cmpint (frame_rect.y, ==, 0);
  g_assert_cmpint (frame_rect.width, ==, 800);
  g_assert_cmpint (frame_rect.height, ==, 600);

  g_signal_handlers_disconnect_by_func (session_manager,
                                        on_session_instantiated,
                                        &session_id);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
toplevel_sessions_restore_maximized (void)
{
  MetaSessionManager *session_manager;
  g_autoptr (MetaVirtualMonitor) second_virtual_monitor = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle frame_rect, monitor_layout;
  g_autofree char *session_id = NULL;
  MetaWindowWayland *wl_window;
  uint32_t state_change_serial = 0U;

  monitor_layout = get_primary_logical_monitor_layout ();

  second_virtual_monitor = meta_create_test_monitor (test_context,
                                                     800, 600, 60.0);

  session_manager = meta_context_get_session_manager (test_context);
  g_signal_connect (session_manager, "session-instantiated",
                    G_CALLBACK (on_session_instantiated), &session_id);

  /* Launch client once, resize window */
  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-session-management-restore");

  wait_for_sync_point (0);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);

  /* Move to second monitor */
  meta_window_move_resize_frame (window, FALSE,
                                 monitor_layout.width + 123, 123, 100, 100);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, monitor_layout.width + 123);
  g_assert_cmpint (frame_rect.y, ==, 123);
  g_assert_cmpint (frame_rect.width, ==, 100);
  g_assert_cmpint (frame_rect.height, ==, 100);

  meta_window_maximize (window);

  wl_window = META_WINDOW_WAYLAND (window);
  meta_window_wayland_get_pending_serial (wl_window, &state_change_serial);
  g_assert_cmpint (state_change_serial, !=, 0U);
  while (meta_window_wayland_peek_configuration (wl_window, state_change_serial))
    g_main_context_iteration (NULL, TRUE);

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, 640);
  g_assert_cmpint (frame_rect.y, ==, 0);
  g_assert_cmpint (frame_rect.width, ==, 800);
  g_assert_cmpint (frame_rect.height, ==, 600);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  g_assert_nonnull (session_id);

  /* Launch client again, check window persists maximized on second monitor */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "xdg-session-management-restore",
                                            session_id,
                                            NULL);

  wait_for_sync_point (0);

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);

  g_assert_nonnull (window->monitor);
  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, monitor_layout.width);
  g_assert_cmpint (frame_rect.y, ==, 0);
  g_assert_cmpint (frame_rect.width, ==, 800);
  g_assert_cmpint (frame_rect.height, ==, 600);

  g_signal_handlers_disconnect_by_func (session_manager, on_session_instantiated,
                                        &session_id);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
toplevel_sessions_restore_tiled (void)
{
  MetaSessionManager *session_manager;
  g_autoptr (MetaVirtualMonitor) second_virtual_monitor = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle frame_rect, monitor_layout;
  g_autofree char *session_id = NULL;
  MetaWindowWayland *wl_window;
  uint32_t state_change_serial = 0U;

  monitor_layout = get_primary_logical_monitor_layout ();

  second_virtual_monitor = meta_create_test_monitor (test_context,
                                                     800, 600, 60.0);

  session_manager = meta_context_get_session_manager (test_context);
  g_signal_connect (session_manager, "session-instantiated",
                    G_CALLBACK (on_session_instantiated), &session_id);

  /* Launch client once, resize window */
  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-session-management-restore");

  wait_for_sync_point (0);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);

  /* Move to second monitor */
  meta_window_move_resize_frame (window, FALSE,
                                 monitor_layout.width + 123, 123, 100, 100);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, monitor_layout.width + 123);
  g_assert_cmpint (frame_rect.y, ==, 123);
  g_assert_cmpint (frame_rect.width, ==, 100);
  g_assert_cmpint (frame_rect.height, ==, 100);

  meta_window_tile (window, META_TILE_LEFT);

  wl_window = META_WINDOW_WAYLAND (window);
  meta_window_wayland_get_pending_serial (wl_window, &state_change_serial);
  g_assert_cmpint (state_change_serial, !=, 0U);
  while (meta_window_wayland_peek_configuration (wl_window, state_change_serial))
    g_main_context_iteration (NULL, TRUE);

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, 640);
  g_assert_cmpint (frame_rect.y, ==, 0);
  g_assert_cmpint (frame_rect.width, ==, 400);
  g_assert_cmpint (frame_rect.height, ==, 600);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  g_assert_nonnull (session_id);

  /* Launch client again, check window persists left-tiled on then
   * second monitor */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "xdg-session-management-restore",
                                            session_id,
                                            NULL);
  wait_for_sync_point (0);

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);
  g_assert_nonnull (window->monitor);
  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, 640);
  g_assert_cmpint (frame_rect.y, ==, 0);
  g_assert_cmpint (frame_rect.width, ==, 400);
  g_assert_cmpint (frame_rect.height, ==, 600);

  g_signal_handlers_disconnect_by_func (session_manager, on_session_instantiated,
                                        &session_id);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
toplevel_sessions_restore_fullscreen_monitor_removed (void)
{
  MetaSessionManager *session_manager;
  g_autoptr (MetaVirtualMonitor) second_virtual_monitor = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle frame_rect, monitor_layout;
  g_autofree char *session_id = NULL;

  monitor_layout = get_primary_logical_monitor_layout ();

  second_virtual_monitor = meta_create_test_monitor (test_context,
                                                     640, 480, 60.0);

  session_manager = meta_context_get_session_manager (test_context);
  g_signal_connect (session_manager, "session-instantiated",
                    G_CALLBACK (on_session_instantiated), &session_id);

  /* Launch client once, resize window */
  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-session-management-restore");

  wait_for_sync_point (0);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);

  /* Move to second monitor */
  meta_window_move_resize_frame (window, FALSE,
                                 monitor_layout.width, 123, 100, 100);
  meta_window_make_fullscreen (window);

  while (!meta_window_wayland_is_acked_fullscreen (META_WINDOW_WAYLAND (window)))
    g_main_context_iteration (NULL, TRUE);

  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, monitor_layout.width);
  g_assert_cmpint (frame_rect.y, ==, 0);
  g_assert_cmpint (frame_rect.width, ==, 640);
  g_assert_cmpint (frame_rect.height, ==, 480);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);

  g_assert_nonnull (session_id);

  /* Destroy second monitor */
  g_clear_object (&second_virtual_monitor);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  /* Launch client again, check window moves to first monitor */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "xdg-session-management-restore",
                                            session_id,
                                            NULL);

  wait_for_sync_point (0);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  window = meta_find_client_window (test_context, "toplevel1");
  g_assert_nonnull (window);

  g_assert_nonnull (window->monitor);
  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.x, ==, 0);
  g_assert_cmpint (frame_rect.y, ==, 0);
  g_assert_cmpint (frame_rect.width, ==, 640);
  g_assert_cmpint (frame_rect.height, ==, 480);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/toplevel/sessions/basic",
                   toplevel_sessions_basic);
  g_test_add_func ("/wayland/toplevel/sessions/replace",
                   toplevel_sessions_replace);
  g_test_add_func ("/wayland/toplevel/sessions/restore",
                   toplevel_sessions_restore);
#ifdef MUTTER_PRIVILEGED_TEST
  (void)(toplevel_sessions_restore_maximized);
  (void)(toplevel_sessions_restore_tiled);
  (void)(toplevel_sessions_restore_fullscreen);
  (void)(toplevel_sessions_restore_fullscreen_monitor_removed);
#else
  g_test_add_func ("/wayland/toplevel/sessions/restore-maximized",
                   toplevel_sessions_restore_maximized);
  g_test_add_func ("/wayland/toplevel/sessions/restore-tiled",
                   toplevel_sessions_restore_tiled);
  g_test_add_func ("/wayland/toplevel/sessions/restore-fullscreen",
                   toplevel_sessions_restore_fullscreen);
  g_test_add_func ("/wayland/toplevel/sessions/restore-fullscreen-monitor-removed",
                   toplevel_sessions_restore_fullscreen_monitor_removed);
#endif
}

int
main (int   argc,
      char *argv[])
{
  g_setenv ("MUTTER_DEBUG_SESSION_MANAGEMENT_PROTOCOL", "1", TRUE);

  meta_run_wayland_tests (argc, argv, init_tests);
}
