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

static struct wl_buffer *buffer;

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
wait_for_configure (WaylandDisplay *display)
{
  waiting_for_configure = TRUE;
  while (waiting_for_configure || window_width == 0)
    wayland_display_dispatch (display);
}

static void
handle_buffer_release (void             *data,
                       struct wl_buffer *callback_buffer)
{
  g_assert (callback_buffer == buffer);
  g_clear_pointer (&buffer, wl_buffer_destroy);
}

static const struct wl_buffer_listener buffer_listener = {
  handle_buffer_release
};

static void
wait_for_buffer_released (WaylandDisplay *display)
{
  while (buffer)
    wayland_display_dispatch (display);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wp_viewport *viewport;
  struct wp_viewport *subsurface_viewport;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_surface *xdg_surface;
  struct wl_surface *subsurface_surface;
  struct wl_subsurface *subsurface;
  struct wl_surface *surface;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, NULL);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);

  xdg_toplevel_set_fullscreen (xdg_toplevel, NULL);
  wl_surface_commit (surface);
  wait_for_configure (display);

  viewport = wp_viewporter_get_viewport (display->viewporter, surface);
  wp_viewport_set_destination (viewport, window_width, window_height);

  buffer =
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (display->single_pixel_mgr,
                                                              0xffffffff,
                                                              0xffffffff,
                                                              0xffffffff,
                                                              0xffffffff);
  wl_buffer_add_listener (buffer, &buffer_listener, NULL);
  wl_surface_attach (surface, buffer, 0, 0);
  wl_surface_commit (surface);
  wait_for_effects_completed (display, surface);
  wait_for_view_verified (display, 0);

  wait_for_buffer_released (display);

  subsurface_surface = wl_compositor_create_surface (display->compositor);
  subsurface = wl_subcompositor_get_subsurface (display->subcompositor,
                                                subsurface_surface,
                                                surface);
  wl_subsurface_set_desync (subsurface);
  wl_subsurface_set_position (subsurface, 20, 20);
  wl_surface_commit (surface);

  subsurface_viewport = wp_viewporter_get_viewport (display->viewporter,
                                                    subsurface_surface);
  wp_viewport_set_destination (subsurface_viewport,
                               window_width - 40,
                               window_height - 40);

  buffer =
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (display->single_pixel_mgr,
                                                              0x00000000,
                                                              0x00000000,
                                                              0x00000000,
                                                              0xffffffff);
  wl_buffer_add_listener (buffer, &buffer_listener, NULL);
  wl_surface_attach (subsurface_surface, buffer, 0, 0);
  wl_surface_commit (subsurface_surface);
  wait_for_view_verified (display, 1);

  wait_for_buffer_released (display);

  buffer =
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (display->single_pixel_mgr,
                                                              0x00000000,
                                                              0x00000000,
                                                              0x00000000,
                                                              0x00000000);
  wl_buffer_add_listener (buffer, &buffer_listener, NULL);
  wl_surface_attach (subsurface_surface, buffer, 0, 0);
  wl_surface_commit (subsurface_surface);
  wait_for_view_verified (display, 0);

  wait_for_buffer_released (display);

  buffer =
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (display->single_pixel_mgr,
                                                              0xffffffff,
                                                              0x00000000,
                                                              0x00000000,
                                                              0xffffffff);
  wl_buffer_add_listener (buffer, &buffer_listener, NULL);
  wl_surface_attach (subsurface_surface, buffer, 0, 0);
  wl_surface_commit (subsurface_surface);
  wait_for_view_verified (display, 2);

  wait_for_buffer_released (display);

  buffer =
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (display->single_pixel_mgr,
                                                              0x00000000,
                                                              0xffffffff,
                                                              0x00000000,
                                                              0xffffffff);
  wl_buffer_add_listener (buffer, &buffer_listener, NULL);
  wl_surface_attach (subsurface_surface, buffer, 0, 0);
  wl_surface_commit (subsurface_surface);
  wait_for_view_verified (display, 3);

  wait_for_buffer_released (display);

  buffer =
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (display->single_pixel_mgr,
                                                              0x00000000,
                                                              0x00000000,
                                                              0xffffffff,
                                                              0xffffffff);
  wl_buffer_add_listener (buffer, &buffer_listener, NULL);
  wl_surface_attach (subsurface_surface, buffer, 0, 0);
  wl_surface_commit (subsurface_surface);
  wait_for_view_verified (display, 4);

  wait_for_buffer_released (display);

  buffer =
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (display->single_pixel_mgr,
                                                              0x80808080,
                                                              0x00000000,
                                                              0x80808080,
                                                              0xffffffff);
  wl_buffer_add_listener (buffer, &buffer_listener, NULL);
  wl_surface_attach (subsurface_surface, buffer, 0, 0);
  wl_surface_commit (subsurface_surface);
  wait_for_view_verified (display, 5);

  wait_for_buffer_released (display);

  buffer =
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (display->single_pixel_mgr,
                                                              0x80808080,
                                                              0x00000000,
                                                              0x80808080,
                                                              0x80808080);
  wl_buffer_add_listener (buffer, &buffer_listener, NULL);
  wl_surface_attach (subsurface_surface, buffer, 0, 0);
  wl_surface_commit (subsurface_surface);
  wait_for_view_verified (display, 6);

  wait_for_buffer_released (display);

  /* Test reuse */

  buffer =
    wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (display->single_pixel_mgr,
                                                              0x70707070,
                                                              0x00000000,
                                                              0x70707070,
                                                              0x70707070);
  wl_surface_attach (subsurface_surface, buffer, 0, 0);
  wl_surface_commit (subsurface_surface);
  wait_for_view_verified (display, 7);

  wl_subsurface_destroy (subsurface);
  wl_surface_destroy (subsurface_surface);

  subsurface_surface = wl_compositor_create_surface (display->compositor);
  subsurface = wl_subcompositor_get_subsurface (display->subcompositor,
                                                subsurface_surface,
                                                surface);
  wl_subsurface_set_desync (subsurface);
  wl_subsurface_set_position (subsurface, 30, 30);
  wl_surface_commit (surface);

  subsurface_viewport = wp_viewporter_get_viewport (display->viewporter,
                                                    subsurface_surface);
  wp_viewport_set_destination (subsurface_viewport,
                               window_width - 60,
                               window_height - 60);

  wl_buffer_add_listener (buffer, &buffer_listener, NULL);
  wl_surface_attach (subsurface_surface, buffer, 0, 0);
  wl_surface_commit (subsurface_surface);
  wait_for_view_verified (display, 8);

  wait_for_buffer_released (display);

  return EXIT_SUCCESS;
}
