/*
 * Copyright (C) 2022 Collabora, Ltd.
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

static WaylandDisplay *display;
static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *xdg_toplevel;

static gboolean waiting_for_configure = FALSE;
static gboolean fullscreen = 0;
static uint32_t window_width = 0;
static uint32_t window_height = 0;

static void
handle_xdg_toplevel_configure (void                *data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *states)
{
  uint32_t *p;

  fullscreen = 0;
  wl_array_for_each(p, states)
    {
      uint32_t state = *p;

      switch (state)
        {
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
          fullscreen = 1;
          break;
        }
    }

  if (width > 0 && height > 0)
    {
      window_width = width;
      window_height = height;
    }
}

static void
handle_xdg_toplevel_close(void                *data,
                          struct xdg_toplevel *xdg_toplevel)
{
  g_assert_not_reached ();
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
};

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  xdg_surface_ack_configure (xdg_surface, serial);

  waiting_for_configure = FALSE;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
draw_main (gboolean rotated)
{
  static uint32_t color0 = 0xffffffff;
  static uint32_t color1 = 0xff00ffff;
  static uint32_t color2 = 0xffff00ff;
  static uint32_t color3 = 0xffffff00;
  WaylandBuffer *buffer;
  int x, y;
  uint32_t width;
  uint32_t height;

  width = rotated ? window_height : window_width,
  height = rotated ? window_width : window_height,

  buffer = wayland_buffer_create (display, NULL,
                                  width, height,
                                  DRM_FORMAT_XRGB8888,
                                  NULL, 0,
                                  GBM_BO_USE_LINEAR);
  if (!buffer)
    g_error ("Failed to create buffer");

  for (y = 0; y < height; y++)
    {
      for (x = 0; x < width; x++)
        {
          uint32_t current_color;

          if (y < height / 2)
            {
              if (x < width / 2)
                current_color = color0;
              else
                current_color = color1;
            }
          else
            {
              if (x < width / 2)
                current_color = color2;
              else
                current_color = color3;
            }

          wayland_buffer_draw_pixel (buffer, x, y, current_color);
        }
    }

  wl_surface_attach (surface, wayland_buffer_get_wl_buffer (buffer), 0, 0);
}

static void
wait_for_configure (void)
{
  waiting_for_configure = TRUE;
  while (waiting_for_configure || window_width == 0)
    {
      if (wl_display_dispatch (display->display) == -1)
        exit (EXIT_FAILURE);
    }
}

int
main (int    argc,
      char **argv)
{
  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, NULL);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);

  xdg_toplevel_set_fullscreen(xdg_toplevel, NULL);
  wl_surface_commit (surface);
  wait_for_configure ();

  draw_main (FALSE);
  wl_surface_commit (surface);
  wait_for_effects_completed (display, surface);

  wl_surface_set_buffer_transform (surface, WL_OUTPUT_TRANSFORM_NORMAL);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 0);

  wl_surface_set_buffer_transform (surface, WL_OUTPUT_TRANSFORM_180);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 1);

  wl_surface_set_buffer_transform (surface, WL_OUTPUT_TRANSFORM_FLIPPED);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 2);

  wl_surface_set_buffer_transform (surface, WL_OUTPUT_TRANSFORM_FLIPPED_180);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 3);

  draw_main (TRUE);
  wl_surface_set_buffer_transform (surface, WL_OUTPUT_TRANSFORM_90);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 4);

  wl_surface_set_buffer_transform (surface, WL_OUTPUT_TRANSFORM_270);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 5);

  wl_surface_set_buffer_transform (surface, WL_OUTPUT_TRANSFORM_FLIPPED_90);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 6);

  wl_surface_set_buffer_transform (surface, WL_OUTPUT_TRANSFORM_FLIPPED_270);
  wl_surface_commit (surface);
  wait_for_view_verified (display, 7);

  g_clear_object (&display);

  return EXIT_SUCCESS;
}
