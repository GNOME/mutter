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
#include "compositor/meta-window-actor-private.h"
#include "core/display-private.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-wayland-test-driver.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-surface.h"

typedef struct _WaylandTestClient
{
  GSubprocess *subprocess;
  char *path;
  GMainLoop *main_loop;
} WaylandTestClient;

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;
static ClutterVirtualInputDevice *virtual_pointer;

static char *
get_test_client_path (const char *test_client_name)
{
  return g_test_build_filename (G_TEST_BUILT,
                                "src",
                                "tests",
                                "wayland-test-clients",
                                test_client_name,
                                NULL);
}

static WaylandTestClient *
wayland_test_client_new (const char *test_client_name)
{
  MetaWaylandCompositor *compositor;
  const char *wayland_display_name;
  g_autofree char *test_client_path = NULL;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  GSubprocess *subprocess;
  GError *error = NULL;
  WaylandTestClient *wayland_test_client;

  compositor = meta_wayland_compositor_get_default ();
  wayland_display_name = meta_wayland_get_wayland_display_name (compositor);
  test_client_path = get_test_client_path (test_client_name);

  launcher =  g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv (launcher,
                                "WAYLAND_DISPLAY", wayland_display_name,
                                TRUE);

  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            test_client_path,
                                            NULL);
  if (!subprocess)
    {
      g_error ("Failed to launch Wayland test client '%s': %s",
               test_client_path, error->message);
    }

  wayland_test_client = g_new0 (WaylandTestClient, 1);
  wayland_test_client->subprocess = subprocess;
  wayland_test_client->path = g_strdup (test_client_name);
  wayland_test_client->main_loop = g_main_loop_new (NULL, FALSE);

  return wayland_test_client;
}

static void
wayland_test_client_finished (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  WaylandTestClient *wayland_test_client = user_data;
  GError *error = NULL;

  if (!g_subprocess_wait_finish (wayland_test_client->subprocess,
                                 res,
                                 &error))
    {
      g_error ("Failed to wait for Wayland test client '%s': %s",
               wayland_test_client->path, error->message);
    }

  g_main_loop_quit (wayland_test_client->main_loop);
}

static void
wayland_test_client_finish (WaylandTestClient *wayland_test_client)
{
  g_subprocess_wait_async (wayland_test_client->subprocess, NULL,
                           wayland_test_client_finished, wayland_test_client);

  g_main_loop_run (wayland_test_client->main_loop);

  g_assert_true (g_subprocess_get_successful (wayland_test_client->subprocess));

  g_main_loop_unref (wayland_test_client->main_loop);
  g_free (wayland_test_client->path);
  g_object_unref (wayland_test_client->subprocess);
  g_free (wayland_test_client);
}

static MetaWindow *
find_client_window (const char *title)
{
  MetaDisplay *display = meta_get_display ();
  g_autoptr (GSList) windows = NULL;
  GSList *l;

  windows = meta_display_list_windows (display, META_LIST_DEFAULT);
  for (l = windows; l; l = l->next)
    {
      MetaWindow *window = l->data;

      if (g_strcmp0 (meta_window_get_title (window), title) == 0)
        return window;
    }

  return NULL;
}

static void
subsurface_remap_toplevel (void)
{
  WaylandTestClient *wayland_test_client;

  wayland_test_client = wayland_test_client_new ("subsurface-remap-toplevel");
  wayland_test_client_finish (wayland_test_client);
}

static void
subsurface_reparenting (void)
{
  WaylandTestClient *wayland_test_client;

  wayland_test_client = wayland_test_client_new ("subsurface-reparenting");
  wayland_test_client_finish (wayland_test_client);
}

static void
subsurface_invalid_subsurfaces (void)
{
  WaylandTestClient *wayland_test_client;

  wayland_test_client = wayland_test_client_new ("invalid-subsurfaces");
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
subsurface_invalid_xdg_shell_actions (void)
{
  WaylandTestClient *wayland_test_client;

  wayland_test_client = wayland_test_client_new ("invalid-xdg-shell-actions");
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "Invalid geometry * set on xdg_surface*");
  wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
on_after_paint (ClutterStage     *stage,
                ClutterStageView *view,
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
  MetaRectangle buffer_rect;

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
      MetaRectangle buffer_rect;

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
  WaylandTestClient *wayland_test_client;
  ClutterSeat *seat;
  gulong window_added_id;
  gulong sync_point_id;

  seat = meta_backend_get_default_seat (backend);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  wayland_test_client = wayland_test_client_new ("subsurface-parent-unmapped");

  window_added_id =
    g_signal_connect (display->stack, "window-added",
                      G_CALLBACK (on_window_added),
                      virtual_pointer);
  sync_point_id =
    g_signal_connect (test_driver, "sync-point",
                      G_CALLBACK (on_unmap_sync_point),
                      NULL);

  wayland_test_client_finish (wayland_test_client);

  g_clear_object (&virtual_pointer);
  g_signal_handler_disconnect (test_driver, sync_point_id);
  g_signal_handler_disconnect (display->stack, window_added_id);
}

typedef enum _ApplyLimitState
{
  APPLY_LIMIT_STATE_INIT,
  APPLY_LIMIT_STATE_RESET,
  APPLY_LIMIT_STATE_FINISH,
} ApplyLimitState;

typedef struct _ApplyLimitData
{
  GMainLoop *loop;
  WaylandTestClient *wayland_test_client;
  ApplyLimitState state;
} ApplyLimitData;

static void
on_sync_point (MetaWaylandTestDriver *test_driver,
               unsigned int           sequence,
               struct wl_resource    *surface_resource,
               struct wl_client      *wl_client,
               ApplyLimitData        *data)
{
  MetaWindow *window;

  if (sequence == 0)
    g_assert (data->state == APPLY_LIMIT_STATE_INIT);
  else if (sequence == 0)
    g_assert (data->state == APPLY_LIMIT_STATE_RESET);

  window = find_client_window ("toplevel-limits-test");

  if (sequence == 0)
    {
      g_assert_nonnull (window);
      g_assert_cmpint (window->size_hints.max_width, ==, 700);
      g_assert_cmpint (window->size_hints.max_height, ==, 500);
      g_assert_cmpint (window->size_hints.min_width, ==, 700);
      g_assert_cmpint (window->size_hints.min_height, ==, 500);

      data->state = APPLY_LIMIT_STATE_RESET;
    }
  else if (sequence == 1)
    {
      g_assert_null (window);
      data->state = APPLY_LIMIT_STATE_FINISH;
      g_main_loop_quit (data->loop);
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
toplevel_apply_limits (void)
{
  ApplyLimitData data = {};
  gulong handler_id;

  data.loop = g_main_loop_new (NULL, FALSE);
  data.wayland_test_client = wayland_test_client_new ("xdg-apply-limits");
  handler_id = g_signal_connect (test_driver, "sync-point",
                                 G_CALLBACK (on_sync_point), &data);
  g_main_loop_run (data.loop);
  g_assert_cmpint (data.state, ==, APPLY_LIMIT_STATE_FINISH);
  wayland_test_client_finish (data.wayland_test_client);
  g_test_assert_expected_messages ();
  g_signal_handler_disconnect (test_driver, handler_id);
}

static void
toplevel_activation (void)
{
  ApplyLimitData data = {};

  data.loop = g_main_loop_new (NULL, FALSE);
  data.wayland_test_client = wayland_test_client_new ("xdg-activation");
  wayland_test_client_finish (data.wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
on_before_tests (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);
  g_autoptr (MetaVirtualMonitorInfo) monitor_info = NULL;
  g_autoptr (GError) error = NULL;

  test_driver = meta_wayland_test_driver_new (compositor);

  monitor_info = meta_virtual_monitor_info_new (400, 400, 60.0,
                                                "MetaTestVendor",
                                                "MetaVirtualMonitor",
                                                "0x1234");
  virtual_monitor = meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                                 monitor_info,
                                                                 &error);
  if (!virtual_monitor)
    g_error ("Failed to create virtual monitor: %s", error->message);
  meta_monitor_manager_reload (monitor_manager);
}

static void
on_after_tests (void)
{
  g_clear_object (&virtual_monitor);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/subsurface/remap-toplevel",
                   subsurface_remap_toplevel);
  g_test_add_func ("/wayland/subsurface/reparent",
                   subsurface_reparenting);
  g_test_add_func ("/wayland/subsurface/invalid-subsurfaces",
                   subsurface_invalid_subsurfaces);
  g_test_add_func ("/wayland/subsurface/invalid-xdg-shell-actions",
                   subsurface_invalid_xdg_shell_actions);
  g_test_add_func ("/wayland/subsurface/parent-unmapped",
                   subsurface_parent_unmapped);
  g_test_add_func ("/wayland/toplevel/apply-limits",
                   toplevel_apply_limits);
  g_test_add_func ("/wayland/toplevel/activation",
                   toplevel_activation);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context));
}
