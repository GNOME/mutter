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
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
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
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland-client-private.h"
#include "wayland/meta-wayland-filter-manager.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-window-configuration.h"
#include "wayland/meta-window-wayland.h"

#include "dummy-client-protocol.h"
#include "dummy-server-protocol.h"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;
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

static void
buffer_shm_destroy_before_release (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "shm-destroy-before-release");

  wait_for_sync_point (0);
  meta_wayland_test_driver_emit_sync_event (test_driver, 0);

  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
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

static const MetaWaylandSurface *
get_surface_from_window (const char *title)
{
  MetaWindow *window;
  MetaWaylandSurface *surface;

  window = find_client_window ("color-representation");
  g_assert_nonnull (window);
  surface = meta_window_get_wayland_surface (window);
  g_assert_nonnull (surface);
  return surface;
}

static void
color_representation_state (void)
{
  MetaWaylandTestClient *wayland_test_client;
  const MetaWaylandSurface *surface;

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "color-representation",
                                            "state",
                                            NULL);

  wait_for_sync_point (0);
  surface = get_surface_from_window ("color-representation");
  g_assert_cmpint (surface->committed_state.premult, ==,
                   META_MULTI_TEXTURE_ALPHA_MODE_NONE);
  g_assert_cmpint (surface->committed_state.coeffs, ==,
                   META_MULTI_TEXTURE_COEFFICIENTS_NONE);
  emit_sync_event (0);

  wait_for_sync_point (1);
  g_assert_cmpint (surface->committed_state.premult, ==,
                   META_MULTI_TEXTURE_ALPHA_MODE_STRAIGHT);
  g_assert_cmpint (surface->committed_state.coeffs, ==,
                   META_MULTI_TEXTURE_COEFFICIENTS_BT709_LIMITED);
  emit_sync_event (1);

  wait_for_sync_point (2);
  g_assert_cmpint (surface->committed_state.premult, ==,
                   META_MULTI_TEXTURE_ALPHA_MODE_STRAIGHT);
  g_assert_cmpint (surface->committed_state.coeffs, ==,
                   META_MULTI_TEXTURE_COEFFICIENTS_BT709_LIMITED);
  emit_sync_event (2);

  wait_for_sync_point (3);
  g_assert_cmpint (surface->committed_state.premult, ==,
                   META_MULTI_TEXTURE_ALPHA_MODE_NONE);
  g_assert_cmpint (surface->committed_state.coeffs, ==,
                   META_MULTI_TEXTURE_COEFFICIENTS_NONE);
  emit_sync_event (3);

  meta_wayland_test_client_finish (wayland_test_client);
}

static void
color_representation_bad_state (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "color-representation",
                                            "bad-state",
                                            NULL);
  /* we wait for the window to flush out all the messages */
  meta_wait_for_client_window (test_context, "color-representation");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
color_representation_bad_state2 (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "color-representation",
                                            "bad-state-2",
                                            NULL);
  /* we wait for the window to flush out all the messages */
  meta_wait_for_client_window (test_context, "color-representation");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
color_representation_premult_reftest (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "color-representation",
                                            "premult-reftest",
                                            NULL);
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
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
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Buggy client * committed initial non-empty content*");
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
                           MetaWaylandTestDriver *driver)
{
  meta_wayland_test_driver_emit_sync_event (driver, 0);
}

static void
on_unmap_sync_point (MetaWaylandTestDriver *driver,
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
                        driver);
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
toplevel_activation (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-activation");
  meta_wayland_test_client_finish (wayland_test_client);
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
  wait_for_paint (stage);

  window = find_client_window ("toplevel1");
  g_assert_nonnull (window);

  g_assert_nonnull (window->monitor);
  frame_rect = meta_window_config_get_rect (window->config);
  g_assert_cmpint (frame_rect.width, ==, 100);
  g_assert_cmpint (frame_rect.height, ==, 100);

  meta_window_move_resize_frame (window, FALSE, 123, 234, 200, 200);
  wait_for_paint (stage);

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
  wait_for_paint (stage);

  window = find_client_window ("toplevel1");
  g_assert_nonnull (window);

  /* Move to second monitor */
  meta_window_move_resize_frame (window, FALSE,
                                 monitor_layout.width + 123, 123, 100, 100);
  wait_for_paint (stage);

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
  wait_for_paint (stage);

  window = find_client_window ("toplevel1");
  g_assert_nonnull (window);

  /* Move to second monitor */
  meta_window_move_resize_frame (window, FALSE,
                                 monitor_layout.width + 123, 123, 100, 100);
  wait_for_paint (stage);

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
  wait_for_paint (stage);

  window = find_client_window ("toplevel1");
  g_assert_nonnull (window);

  /* Move to second monitor */
  meta_window_move_resize_frame (window, FALSE,
                                 monitor_layout.width + 123, 123, 100, 100);
  wait_for_paint (stage);

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
  wait_for_paint (stage);

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
  wait_for_paint (stage);

  /* Launch client again, check window moves to first monitor */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "xdg-session-management-restore",
                                            session_id,
                                            NULL);

  wait_for_sync_point (0);
  wait_for_paint (stage);

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
  wait_until_after_paint ();

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
  wait_until_after_paint ();

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
  wait_until_after_paint ();

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
  wait_until_after_paint ();

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
  ClutterInputDevice *pointer;
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
  pointer = clutter_seat_get_pointer (seat);
  g_assert_nonnull (pointer);
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
  g_test_add_func ("/wayland/color-representation/state",
                   color_representation_state);
  g_test_add_func ("/wayland/color-representation/bad-state",
                   color_representation_bad_state);
  g_test_add_func ("/wayland/color-representation/bad-state2",
                   color_representation_bad_state2);
  g_test_add_func ("/wayland/color-representation/premult-reftest",
                   color_representation_premult_reftest);
  g_test_add_func ("/wayland/buffer/transform",
                   buffer_transform);
  g_test_add_func ("/wayland/buffer/single-pixel-buffer",
                   buffer_single_pixel_buffer);
  g_test_add_func ("/wayland/buffer/ycbcr-basic",
                   buffer_ycbcr_basic);
  g_test_add_func ("/wayland/buffer/shm-destroy-before-release",
                   buffer_shm_destroy_before_release);
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
  g_test_add_func ("/wayland/toplevel/invalid-limits",
                   toplevel_invalid_limits);
  g_test_add_func ("/wayland/toplevel/invalid-geometry/basic",
                   toplevel_invalid_geometry_basic);
  g_test_add_func ("/wayland/toplevel/invalid-geometry/subsurface",
                   toplevel_invalid_geometry_subsurface);
  g_test_add_func ("/wayland/toplevel/activation",
                   toplevel_activation);
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
  (void)(toplevel_bounds_struts);
  (void)(toplevel_bounds_monitors);
#else
  g_test_add_func ("/wayland/toplevel/sessions/restore-maximized",
                   toplevel_sessions_restore_maximized);
  g_test_add_func ("/wayland/toplevel/sessions/restore-tiled",
                   toplevel_sessions_restore_tiled);
  g_test_add_func ("/wayland/toplevel/sessions/restore-fullscreen",
                   toplevel_sessions_restore_fullscreen);
  g_test_add_func ("/wayland/toplevel/sessions/restore-fullscreen-monitor-removed",
                   toplevel_sessions_restore_fullscreen_monitor_removed);
  g_test_add_func ("/wayland/toplevel/bounds/struts",
                   toplevel_bounds_struts);
  g_test_add_func ("/wayland/toplevel/bounds/monitors",
                   toplevel_bounds_monitors);
#endif
  g_test_add_func ("/wayland/toplevel/reuse-surface",
                   toplevel_reuse_surface);
  g_test_add_func ("/wayland/xdg-foreign/set-parent-of",
                   xdg_foreign_set_parent_of);
  g_test_add_func ("/wayland/toplevel/show-states",
                   toplevel_show_states);
  g_test_add_func ("/wayland/toplevel/suspended",
                   toplevel_suspended);
  g_test_add_func ("/wayland/cursor/shape",
                   cursor_shape);
  g_test_add_func ("/wayland/toplevel/tag",
                   toplevel_tag);
  g_test_add_func ("/wayland/toplevel/activation-before-mapped",
                   toplevel_activation_before_mapped);
  g_test_add_func ("/wayland/toplevel/fixed-size-fullscreen",
                   toplevel_fixed_size_fullscreen);
  g_test_add_func ("/wayland/toplevel/fixed-size-fullscreen-exceeds",
                   toplevel_fixed_size_fullscreen_exceeds);
  g_test_add_func ("/wayland/toplevel/focus-changes-remembers-size",
                   toplevel_focus_changes_remembers_size);
  g_test_add_func ("/wayland/toplevel/begin-interactive-resize",
                   toplevel_begin_interactive_resize);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;
  MetaTestRunFlags test_run_flags;

  g_setenv ("MUTTER_DEBUG_SESSION_MANAGEMENT_PROTOCOL", "1", TRUE);

#ifdef MUTTER_PRIVILEGED_TEST
  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
#else
  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
#endif
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));
  meta_context_test_set_background_color (META_CONTEXT_TEST (context),
                                          COGL_COLOR_INIT (255, 255, 255, 255));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

#ifdef MUTTER_PRIVILEGED_TEST
  test_run_flags = META_TEST_RUN_FLAG_CAN_SKIP;
#else
  test_run_flags = META_TEST_RUN_FLAG_NONE;
#endif
  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      test_run_flags);
}
