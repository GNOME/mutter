/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include <glib.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

typedef enum _State
{
  STATE_INIT = 0,
  STATE_WAIT_FOR_CONFIGURE_1,
  STATE_WAIT_FOR_FRAME_1,
} State;

static struct wl_surface *surface;

static struct wl_callback *frame_callback;

static gboolean running;

static State state;
static int32_t pending_bounds_width;
static int32_t pending_bounds_height;

static void
init_surface (struct xdg_toplevel *xdg_toplevel)
{
  xdg_toplevel_set_title (xdg_toplevel, "toplevel-bounds-test");
  wl_surface_commit (surface);
}

static void
draw_main (WaylandDisplay *display,
           int             width,
           int             height)
{
  draw_surface (display, surface, width, height, 0xff00ff00);
}

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *state)
{
}

static void
handle_xdg_toplevel_close(void                *data,
                          struct xdg_toplevel *xdg_toplevel)
{
  g_assert_not_reached ();
}

static void
handle_xdg_toplevel_configure_bounds (void                *data,
                                      struct xdg_toplevel *xdg_toplevel,
                                      int32_t              bounds_width,
                                      int32_t              bounds_height)
{
  pending_bounds_width = bounds_width;
  pending_bounds_height = bounds_height;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
  handle_xdg_toplevel_configure_bounds,
};

static void
handle_frame_callback (void               *data,
                       struct wl_callback *callback,
                       uint32_t            time)
{
  WaylandDisplay *display = data;

  switch (state)
    {
    case STATE_WAIT_FOR_FRAME_1:
      test_driver_sync_point (display->test_driver, 1, NULL);
      break;
    case STATE_INIT:
    case STATE_WAIT_FOR_CONFIGURE_1:
      g_assert_not_reached ();
    }
}

static const struct wl_callback_listener frame_listener = {
  handle_frame_callback,
};

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  WaylandDisplay *display = data;

  switch (state)
    {
    case STATE_INIT:
      g_assert_not_reached ();
    case STATE_WAIT_FOR_CONFIGURE_1:
      g_assert (pending_bounds_width > 0);
      g_assert (pending_bounds_height > 0);

      draw_main (display,
                 pending_bounds_width - 10,
                 pending_bounds_height - 10);
      state = STATE_WAIT_FOR_FRAME_1;
      break;
    case STATE_WAIT_FOR_FRAME_1:
      break;
    }

  xdg_surface_ack_configure (xdg_surface, serial);
  frame_callback = wl_surface_frame (surface);
  wl_callback_add_listener (frame_callback, &frame_listener, display);
  wl_surface_commit (surface);
  wl_display_flush (display->display);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
on_sync_event (WaylandDisplay *display,
               uint32_t        serial)
{
  g_assert (serial == 0);

  exit (EXIT_SUCCESS);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_surface *xdg_surface;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER |
                                 WAYLAND_DISPLAY_CAPABILITY_XDG_SHELL_V4);
  g_signal_connect (display, "sync-event", G_CALLBACK (on_sync_event), NULL);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, display);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);

  init_surface (xdg_toplevel);
  state = STATE_WAIT_FOR_CONFIGURE_1;

  wl_surface_commit (surface);

  running = TRUE;
  while (running)
    wayland_display_dispatch (display);

  return EXIT_SUCCESS;
}
