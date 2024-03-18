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

#include <glib.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

typedef enum _State
{
  STATE_INIT = 0,
  STATE_WAIT_FOR_CONFIGURE_1,
  STATE_WAIT_FOR_CONFIGURE_2,
  STATE_WAIT_FOR_EFFECTS,
  STATE_DONE,
} State;

typedef struct _TestWindow
{
  WaylandDisplay *display;
  struct wl_surface *wl_surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  State state;
} TestWindow;

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial);

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  TestWindow *window = data;
  WaylandDisplay *display = window->display;

  switch (window->state)
    {
    case STATE_WAIT_FOR_CONFIGURE_1:
      draw_surface (display, window->wl_surface, 700, 500, 0xff00ff00);
      xdg_surface_ack_configure (window->xdg_surface, serial);
      wl_surface_commit (window->wl_surface);

      window->state = STATE_WAIT_FOR_EFFECTS;
      wait_for_effects_completed (display, window->wl_surface);

      xdg_toplevel_destroy (window->xdg_toplevel);
      xdg_surface_destroy (window->xdg_surface);
      wl_surface_attach (window->wl_surface, NULL, 0, 0);
      wl_surface_commit (window->wl_surface);
      window->xdg_surface =
        xdg_wm_base_get_xdg_surface (display->xdg_wm_base, window->wl_surface);
      xdg_surface_add_listener (window->xdg_surface,
                                &xdg_surface_listener, window);
      window->xdg_toplevel = xdg_surface_get_toplevel (window->xdg_surface);
      xdg_toplevel_set_title (window->xdg_toplevel,
                              "toplevel-reuse-surface-test");
      wl_surface_commit (window->wl_surface);
      window->state = STATE_WAIT_FOR_CONFIGURE_2;
      break;
    case STATE_WAIT_FOR_CONFIGURE_2:
      draw_surface (display, window->wl_surface, 700, 500, 0xff00ff00);
      xdg_surface_ack_configure (window->xdg_surface, serial);
      wl_surface_commit (window->wl_surface);

      window->state = STATE_WAIT_FOR_EFFECTS;
      wait_for_effects_completed (display, window->wl_surface);

      window->state = STATE_DONE;
      break;
    default:
      break;
    }
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  TestWindow window;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  window.display = display;

  window.wl_surface = wl_compositor_create_surface (display->compositor);
  window.xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base,
                                                    window.wl_surface);
  xdg_surface_add_listener (window.xdg_surface, &xdg_surface_listener, &window);
  window.xdg_toplevel = xdg_surface_get_toplevel (window.xdg_surface);

  xdg_toplevel_set_title (window.xdg_toplevel,
                          "toplevel-reuse-surface-test");
  wl_surface_commit (window.wl_surface);

  window.state = STATE_WAIT_FOR_CONFIGURE_1;

  while (window.state != STATE_DONE)
    {
      if (wl_display_dispatch (display->display) == -1)
        return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
