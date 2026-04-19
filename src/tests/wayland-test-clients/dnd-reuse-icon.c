/*
 * Copyright (C) 2026 Red Hat Inc.
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

#include "wayland-test-client-utils.h"

static GMainLoop *loop;

static void
handle_frame_callback (void               *user_data,
                       struct wl_callback *callback,
                       uint32_t            time)
{
  gboolean *done = user_data;

  wl_callback_destroy (callback);
  *done = TRUE;
}

static const struct wl_callback_listener frame_listener = {
  handle_frame_callback,
};

static void
on_button_event (WaylandSurface    *surface,
                 struct wl_pointer *wl_pointer,
                 uint32_t           serial,
                 uint32_t           button,
                 gboolean           state)
{
  WaylandDisplay *display = surface->display;
  struct wl_data_source *source;
  WaylandSurface *dnd_surface;
  struct wl_callback *callback;
  gboolean done;

  /* Start a drag with a new icon surface. */

  dnd_surface = wayland_surface_new_unassigned (display);

  source =
    wl_data_device_manager_create_data_source (display->data_device_manager);
  wl_data_device_start_drag (display->data_device, source, surface->wl_surface,
                             dnd_surface->wl_surface, serial);
  draw_surface (display, dnd_surface->wl_surface, 20, 20, 0xff0000ff);

  done = FALSE;
  callback = wl_surface_frame (dnd_surface->wl_surface);
  wl_callback_add_listener (callback, &frame_listener, &done);

  wl_surface_commit (dnd_surface->wl_surface);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  wait_for_view_verified (display, 0);

  done = FALSE;
  callback = test_driver_sync_actor_destroyed (display->test_driver,
                                               dnd_surface->wl_surface);
  wl_callback_add_listener (callback, &frame_listener, &done);

  wl_data_source_destroy (source);

  /* Start another drag with the same icon surface, but with a different
   * buffer. */

  source =
    wl_data_device_manager_create_data_source (display->data_device_manager);

  draw_surface (display, dnd_surface->wl_surface, 20, 20, 0xffff00ff);
  wl_surface_commit (dnd_surface->wl_surface);

  wl_data_device_start_drag (display->data_device, source, surface->wl_surface,
                             dnd_surface->wl_surface, serial);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  done = FALSE;
  callback = wl_surface_frame (dnd_surface->wl_surface);
  wl_callback_add_listener (callback, &frame_listener, &done);
  wl_surface_commit (dnd_surface->wl_surface);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  wait_for_view_verified (display, 1);

  wl_data_source_destroy (source);
  g_object_unref (dnd_surface);

  g_main_loop_quit (loop);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  surface = wayland_surface_new (display, "dnd",
                                 100, 100, 0xff00ffff);
  wayland_surface_set_input_region (surface);
  g_signal_connect (surface, "button-event",
                    G_CALLBACK (on_button_event),
                    NULL);
  wl_surface_commit (surface->wl_surface);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  return EXIT_SUCCESS;
}
