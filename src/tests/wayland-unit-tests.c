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
#include <libevdev/libevdev.h>
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
#include "wayland/meta-window-wayland.h"

#include "dummy-client-protocol.h"
#include "dummy-server-protocol.h"

static ClutterVirtualInputDevice *virtual_pointer;

static void
wait_for_sync_point (unsigned int sync_point)
{
  meta_wayland_test_driver_wait_for_sync_point (test_driver, sync_point);
}

static void
emit_sync_event (unsigned int sync_point)
{
  meta_wayland_test_driver_emit_sync_event (test_driver, sync_point);
}

static MetaWindow *
find_client_window (const char *title)
{
  return meta_find_client_window (test_context, title);
}

static void
cursor_shape (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  MetaWaylandTestClient *wayland_test_client;

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       320.0f,
                                                       240.0f);
  meta_flush_input (test_context);

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "cursor-shape",
                                            "v2-shape-on-v1",
                                            NULL);
  /* we wait for the window to flush out all the messages */
  meta_wait_for_client_window (test_context, "cursor-shape");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "cursor-shape",
                                            "bad-shape",
                                            NULL);
  /* we wait for the window to flush out all the messages */
  meta_wait_for_client_window (test_context, "cursor-shape");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();

  /* FIXME workaround for a bug in native cursor renderer where just trying to
   * get the cursor on a plane results in no software cursor being rendered */
  meta_backend_inhibit_hw_cursor (backend);
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "cursor-shape",
                                            "ref-test",
                                            NULL);
  meta_wayland_test_client_finish (wayland_test_client);
  meta_backend_uninhibit_hw_cursor (backend);
  g_clear_object (&virtual_pointer);
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

static void
toplevel_activation_no_serial (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-activation-no-serial");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
toplevel_activation_before_mapped (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  g_autoptr (GSettings) wm_prefs = NULL;
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;

  wm_prefs = g_settings_new ("org.gnome.desktop.wm.preferences");
  virtual_keyboard =
    clutter_seat_create_virtual_device (seat, CLUTTER_KEYBOARD_DEVICE);
  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-activation-before-mapped");

  wait_for_sync_point (0);
  g_settings_set_enum (wm_prefs, "focus-new-windows",
                       G_DESKTOP_FOCUS_NEW_WINDOWS_STRICT);
  emit_sync_event (0);

  wait_for_sync_point (1);
  window = find_client_window ("activated-window");
  g_assert_true (meta_window_has_focus (window));
  g_assert_true (window == meta_stack_get_top (window->display->stack));
  g_assert_true (window->stack_position == 1);

  meta_wayland_test_client_finish (wayland_test_client);
  g_settings_reset (wm_prefs, "focus-new-windows");
}

static void
toplevel_activation_serial (const char             *client_arg,
                            ClutterInputDeviceType  device_type)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaDisplay *display = meta_context_get_display (test_context);
  g_autoptr (ClutterVirtualInputDevice) device = NULL;
  g_autoptr (GSettings) wm_prefs = NULL;
  MetaWaylandTestClient *wayland_test_client;
  ClutterSeat *seat;
  MetaWindow *parent_window;
  MtkRectangle parent_rect;
  MetaWindow *child_window;

  wm_prefs = g_settings_new ("org.gnome.desktop.wm.preferences");

  seat = meta_backend_get_default_seat (backend);
  device = clutter_seat_create_virtual_device (seat, device_type);

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "xdg-activation-serial",
                                            client_arg,
                                            NULL);

  while (!(parent_window = find_client_window ("xdg-activation-parent")))
    g_main_context_iteration (NULL, TRUE);
  g_object_add_weak_pointer (G_OBJECT (parent_window),
                             (gpointer *) &parent_window);

  while (meta_window_is_hidden (parent_window))
    g_main_context_iteration (NULL, TRUE);
  meta_wait_for_effects (parent_window);

  g_settings_set_enum (wm_prefs, "focus-new-windows",
                       G_DESKTOP_FOCUS_NEW_WINDOWS_STRICT);

  g_assert_true (meta_display_get_focus_window (display) == parent_window);

  meta_window_get_frame_rect (parent_window, &parent_rect);

  if (device_type == CLUTTER_POINTER_DEVICE)
    {
      clutter_virtual_input_device_notify_absolute_motion (device,
                                                           CLUTTER_CURRENT_TIME,
                                                           parent_rect.x + 10,
                                                           parent_rect.y + 10);
      clutter_virtual_input_device_notify_button (device,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_PRESSED);
      clutter_virtual_input_device_notify_button (device,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_RELEASED);
    }
  else if (device_type == CLUTTER_KEYBOARD_DEVICE)
    {
      meta_window_activate (parent_window, META_CURRENT_TIME);
      clutter_virtual_input_device_notify_key (device,
                                               CLUTTER_CURRENT_TIME,
                                               KEY_A,
                                               CLUTTER_KEY_STATE_PRESSED);
      clutter_virtual_input_device_notify_key (device,
                                               CLUTTER_CURRENT_TIME,
                                               KEY_A,
                                               CLUTTER_KEY_STATE_RELEASED);
    }

  while (!(child_window = find_client_window ("xdg-activation-child")))
    g_main_context_iteration (NULL, TRUE);
  g_object_add_weak_pointer (G_OBJECT (child_window),
                             (gpointer *) &child_window);

  while (meta_window_is_hidden (child_window))
    g_main_context_iteration (NULL, TRUE);

  g_assert_true (meta_display_get_focus_window (display) == child_window);

  meta_wayland_test_driver_terminate (test_driver);
  meta_wayland_test_client_finish (wayland_test_client);

  g_settings_reset (wm_prefs, "focus-new-windows");

  while (child_window || parent_window)
    g_main_context_iteration (NULL, TRUE);
}

static void
toplevel_activation_button_press (void)
{
  toplevel_activation_serial ("button-press", CLUTTER_POINTER_DEVICE);
}

static void
toplevel_activation_button_release (void)
{
  toplevel_activation_serial ("button-release", CLUTTER_POINTER_DEVICE);
}

static void
toplevel_activation_key_press (void)
{
  toplevel_activation_serial ("key-press", CLUTTER_KEYBOARD_DEVICE);
}

static void
toplevel_activation_key_release (void)
{
  toplevel_activation_serial ("key-release", CLUTTER_KEYBOARD_DEVICE);
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

static gboolean
mark_later_as_done (gpointer user_data)
{
  gboolean *done = user_data;

  *done = TRUE;

  return G_SOURCE_REMOVE;
}

static void
delayed_cursor (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  MetaWaylandTestClient *test_client1, *test_client2;
  MetaWindow *window;

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       600.0f,
                                                       0.0f);
  meta_flush_input (test_context);

  /* FIXME workaround for a bug in native cursor renderer where just trying to
   * get the cursor on a plane results in no software cursor being rendered */
  meta_backend_inhibit_hw_cursor (backend);

  test_client1 =
    meta_wayland_test_client_new_with_args (test_context,
                                            "delayed-cursor",
                                            "src",
                                            NULL);
  meta_wait_for_client_window (test_context, "src");
  window = find_client_window ("src");
  g_assert_nonnull (window);
  meta_wait_for_effects (window);
  wait_for_sync_point (0);
  meta_window_move_frame (window, FALSE, 100, 100);

  test_client2 =
    meta_wayland_test_client_new_with_args (test_context,
                                            "delayed-cursor",
                                            "dst",
                                            NULL);
  meta_wait_for_client_window (test_context, "dst");
  window = find_client_window ("dst");
  g_assert_nonnull (window);
  meta_wait_for_effects (window);
  wait_for_sync_point (1);
  meta_window_move_frame (window, FALSE, 200, 200);

  /* Move into src */
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       150.0f,
                                                       150.0f);
  meta_flush_input (test_context);

  wait_for_sync_point (2);

  /* Move into compositor chrome */
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       250.0f,
                                                       150.0f);
  meta_flush_input (test_context);

  /* Move into dst */
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       250.0f,
                                                       250.0f);
  meta_flush_input (test_context);

  wait_for_sync_point (3);

  /* Move into compositor chrome again */
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       150.0f,
                                                       250.0f);
  meta_flush_input (test_context);

  emit_sync_event (0);

  meta_wayland_test_client_finish (test_client1);
  meta_wayland_test_client_finish (test_client2);
  meta_backend_uninhibit_hw_cursor (backend);
  g_clear_object (&virtual_pointer);
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
on_session_instantiated (MetaSessionManager *session_manager,
                         const char         *name,
                         MetaSessionState   *state,
                         gpointer            user_data)
{
  char **session_id = user_data;

  *session_id = g_strdup (name);
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

  window = find_client_window ("toplevel1");
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

  window = find_client_window ("toplevel1");
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

  window = find_client_window ("toplevel1");
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

  window = find_client_window ("toplevel1");
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

  window = find_client_window ("toplevel1");
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

  window = find_client_window ("toplevel1");
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

  window = find_client_window ("toplevel1");
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

  window = find_client_window ("toplevel1");
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

  window = find_client_window ("toplevel1");
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

  window = find_client_window ("toplevel1");
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
  g_test_add_func ("/wayland/idle-inhibit/instant-destroy",
                   idle_inhibit_instant_destroy);
  g_test_add_func ("/wayland/registry/filter",
                   registry_filter);
  g_test_add_func ("/wayland/toplevel/activation/no-serial",
                   toplevel_activation_no_serial);
  g_test_add_func ("/wayland/toplevel/activation/before-mapped",
                   toplevel_activation_before_mapped);
  g_test_add_func ("/wayland/toplevel/activation/button-press",
                   toplevel_activation_button_press);
  g_test_add_func ("/wayland/toplevel/activation/button-release",
                   toplevel_activation_button_release);
  g_test_add_func ("/wayland/toplevel/activation/key-press",
                   toplevel_activation_key_press);
  g_test_add_func ("/wayland/toplevel/activation/key-release",
                   toplevel_activation_key_release);
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
  g_test_add_func ("/wayland/xdg-foreign/set-parent-of",
                   xdg_foreign_set_parent_of);
  g_test_add_func ("/wayland/toplevel/suspended",
                   toplevel_suspended);
  g_test_add_func ("/wayland/cursor/shape",
                   cursor_shape);
  g_test_add_func ("/wayland/cursor/delayed",
                   delayed_cursor);
  g_test_add_func ("/wayland/toplevel/tag",
                   toplevel_tag);
}

int
main (int   argc,
      char *argv[])
{
  g_setenv ("MUTTER_DEBUG_SESSION_MANAGEMENT_PROTOCOL", "1", TRUE);

  meta_run_wayland_tests (argc, argv, init_tests);
}
