/*
 * Copyright (C) 2021 Christian Rauch
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

#include "wayland-test-client-utils.h"

typedef enum _State
{
  STATE_INIT = 0,
  STATE_WAIT_FOR_CONFIGURE_1,
  STATE_WAIT_FOR_FRAME_1,
  STATE_WAIT_FOR_ACTOR_DESTROYED,
  STATE_WAIT_FOR_CONFIGURE_2,
  STATE_WAIT_FOR_FRAME_2
} State;

static struct wl_surface *surface;
static struct xdg_toplevel *xdg_toplevel;

static struct wl_callback *frame_callback;

static gboolean running;

static State state;

static void
init_surface (void)
{
  xdg_toplevel_set_title (xdg_toplevel, "toplevel-limits-test");
  wl_surface_commit (surface);
}

static void
actor_destroyed (void               *data,
                 struct wl_callback *callback,
                 uint32_t            serial)
{
  g_assert_cmpint (state, ==, STATE_WAIT_FOR_ACTOR_DESTROYED);

  init_surface ();
  state = STATE_WAIT_FOR_CONFIGURE_2;

  wl_callback_destroy (callback);
}

static const struct wl_callback_listener actor_destroy_listener = {
  actor_destroyed,
};

static void
reset_surface (WaylandDisplay *display)
{
  struct wl_callback *callback;

  if (display->test_driver)
    {
      callback = test_driver_sync_actor_destroyed (display->test_driver,
                                                   surface);
      wl_callback_add_listener (callback, &actor_destroy_listener, NULL);
    }

  wl_surface_attach (surface, NULL, 0, 0);
  wl_surface_commit (surface);

  state = STATE_WAIT_FOR_ACTOR_DESTROYED;
}

static void
draw_main (WaylandDisplay *display)
{
  draw_surface (display, surface, 700, 500, 0xff00ff00);
}

static void
draw_subsurface (WaylandDisplay    *display,
                 struct wl_surface *subsurface_surface)
{
  draw_surface (display, subsurface_surface, 500, 300, 0xff007f00);
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
handle_xdg_toplevel_close (void                *data,
                           struct xdg_toplevel *xdg_toplevel)
{
  g_assert_not_reached ();
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
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
      reset_surface (display);
      test_driver_sync_point (display->test_driver, 1, NULL);
      break;
    case STATE_WAIT_FOR_FRAME_2:
      exit (EXIT_SUCCESS);
    case STATE_INIT:
      g_assert_not_reached ();
    case STATE_WAIT_FOR_CONFIGURE_1:
      g_assert_not_reached ();
    case STATE_WAIT_FOR_ACTOR_DESTROYED:
      g_assert_not_reached ();
    case STATE_WAIT_FOR_CONFIGURE_2:
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
      draw_main (display);
      state = STATE_WAIT_FOR_FRAME_1;
      break;
    case STATE_WAIT_FOR_CONFIGURE_2:
      draw_main (display);
      state = STATE_WAIT_FOR_FRAME_2;
      break;
    case STATE_WAIT_FOR_ACTOR_DESTROYED:
      g_assert_not_reached ();
    case STATE_WAIT_FOR_FRAME_1:
    case STATE_WAIT_FOR_FRAME_2:
      /* ignore */
      return;
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

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct xdg_surface *xdg_surface;
  struct wl_surface *subsurface_surface;
  struct wl_subsurface *subsurface;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, display);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);

  subsurface_surface = wl_compositor_create_surface (display->compositor);
  subsurface = wl_subcompositor_get_subsurface (display->subcompositor,
                                                subsurface_surface,
                                                surface);
  wl_subsurface_set_position (subsurface, 100, 100);
  draw_subsurface (display, subsurface_surface);
  wl_surface_commit (subsurface_surface);

  init_surface ();
  state = STATE_WAIT_FOR_CONFIGURE_1;

  /* set minimum and maximum size and commit */
  xdg_toplevel_set_min_size (xdg_toplevel, 700, 500);
  xdg_toplevel_set_max_size (xdg_toplevel, 700, 500);
  wl_surface_commit (surface);

  test_driver_sync_point (display->test_driver, 0, NULL);

  running = TRUE;
  while (running)
    wayland_display_dispatch (display);

  return EXIT_SUCCESS;
}
