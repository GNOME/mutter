/*
 * Copyright (C) 2026 Canonical Ltd.
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
 * Author: Alessandro Astone <alessandro.astone@canonical.com>
 */

#include "config.h"

#include "wayland-test-client-utils.h"

typedef enum
{
  TEST_STATE_WAIT_ENTER,
  TEST_STATE_WAIT_FINALIZE,
  TEST_STATE_FINALIZING,
  TEST_STATE_DONE,
} TestState;

static GMainLoop *loop;
static gboolean saw_dnd_enter;
static WaylandSurface *drag_surface;
static struct wl_data_source *drag_source;
static TestState test_state;

static void
drag_data_device_enter (WaylandSurface        *surface,
                        struct wl_data_device *data_device,
                        uint32_t               serial,
                        WaylandDisplay        *display)
{
  g_assert_cmpint (test_state, ==, TEST_STATE_WAIT_ENTER);
  g_assert_true (surface == drag_surface);

  saw_dnd_enter = TRUE;
  test_state = TEST_STATE_WAIT_FINALIZE;
  test_driver_sync_point (display->test_driver, 1, NULL);
}

static void
drag_data_device_leave (WaylandSurface        *surface,
                        struct wl_data_device *data_device,
                        WaylandDisplay        *display)
{
  g_assert_cmpint (test_state, >=, TEST_STATE_FINALIZING);
}

static void
start_drag (WaylandDisplay *display,
            uint32_t        serial)
{
  g_assert_nonnull (display->data_device);
  g_assert_null (drag_source);

  g_assert_cmpuint (serial, >, 0);

  drag_source =
    wl_data_device_manager_create_data_source (display->data_device_manager);
  wl_data_source_offer (drag_source, "text/plain;charset=utf-8");
  wl_data_device_start_drag (display->data_device,
                             drag_source,
                             drag_surface->wl_surface,
                             NULL,
                             serial);
}

static void
touch_handle_down (void               *user_data,
                   struct wl_touch    *wl_touch,
                   uint32_t            serial,
                   uint32_t            time,
                   struct wl_surface  *surface,
                   int32_t             id,
                   wl_fixed_t          x,
                   wl_fixed_t          y)
{
  WaylandDisplay *display = user_data;

  g_assert_true (surface == drag_surface->wl_surface);
  start_drag (display, serial);
}

static void
touch_handle_up (void            *user_data,
                 struct wl_touch *wl_touch,
                 uint32_t         serial,
                 uint32_t         time,
                 int32_t          id)
{
}

static void
touch_handle_motion (void            *user_data,
                     struct wl_touch *wl_touch,
                     uint32_t         time,
                     int32_t          id,
                     wl_fixed_t       x,
                     wl_fixed_t       y)
{
}

static void
touch_handle_frame (void            *user_data,
                    struct wl_touch *wl_touch)
{
}

static void
touch_handle_cancel (void            *user_data,
                     struct wl_touch *wl_touch)
{
}

static void
touch_handle_shape (void            *user_data,
                    struct wl_touch *wl_touch,
                    int32_t          id,
                    wl_fixed_t       major,
                    wl_fixed_t       minor)
{
}

static void
touch_handle_orientation (void            *user_data,
                          struct wl_touch *wl_touch,
                          int32_t          id,
                          wl_fixed_t       orientation)
{
}

static const struct wl_touch_listener touch_listener = {
  touch_handle_down,
  touch_handle_up,
  touch_handle_motion,
  touch_handle_frame,
  touch_handle_cancel,
  touch_handle_shape,
  touch_handle_orientation,
};

static void
on_sync_event (WaylandDisplay *display,
               uint32_t        serial)
{
  switch (serial)
    {
    case 0:
      g_assert_true (saw_dnd_enter);
      g_assert_cmpint (test_state, ==, TEST_STATE_WAIT_FINALIZE);
      test_state = TEST_STATE_FINALIZING;
      break;
    case 1:
      g_assert_true (saw_dnd_enter);
      test_state = TEST_STATE_DONE;
      g_main_loop_quit (loop);
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  g_signal_connect (display, "sync-event", G_CALLBACK (on_sync_event), NULL);

  test_state = TEST_STATE_WAIT_ENTER;

  surface = wayland_surface_new (display, "touch-dnd",
                                 100, 100, 0xff00ffff);
  drag_surface = surface;
  g_signal_connect (drag_surface, "data-enter", G_CALLBACK (drag_data_device_enter), display);
  g_signal_connect (drag_surface, "data-leave", G_CALLBACK (drag_data_device_leave), display);

  wl_surface_commit (surface->wl_surface);

  while (!display->wl_touch)
    wl_display_roundtrip (display->display);
  wl_touch_add_listener (display->wl_touch, &touch_listener, display);

  test_driver_sync_point (display->test_driver, 0, NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);
  loop = NULL;

  if (drag_source)
    wl_data_source_destroy (drag_source);

  drag_source = NULL;
  drag_surface = NULL;

  wl_display_roundtrip (display->display);

  return EXIT_SUCCESS;
}
