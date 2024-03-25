/*
 * Copyright (C) 2023 Red Hat Inc.
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
#include <wayland-cursor.h>

#include "wayland-test-client-utils.h"

static struct wl_seat *wl_seat;
static struct wl_pointer *wl_pointer;
static uint32_t enter_serial;

static struct wl_surface *surface;
struct wl_surface *cursor_surface;
struct wl_cursor *cursor;
struct wl_cursor *cursor2;

static gboolean running;

static void
init_surface (struct xdg_toplevel *xdg_toplevel)
{
  xdg_toplevel_set_title (xdg_toplevel, "kms-cursor-hotplug-helper");
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
handle_xdg_toplevel_close (void                *data,
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
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
  handle_xdg_toplevel_configure_bounds,
};

static void
handle_xdg_surface_configure (void               *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  WaylandDisplay *display = data;

  draw_main (display, 100, 100);
  xdg_surface_ack_configure (xdg_surface, serial);
  wl_surface_commit (surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};


static void
pointer_handle_enter (void              *data,
                      struct wl_pointer *pointer,
                      uint32_t           serial,
                      struct wl_surface *surface,
                      wl_fixed_t         sx,
                      wl_fixed_t         sy)
{
  struct wl_buffer *buffer;
  struct wl_cursor_image *image;

  image = cursor->images[0];
  buffer = wl_cursor_image_get_buffer (image);

  enter_serial = serial;
  wl_pointer_set_cursor (pointer, serial,
                         cursor_surface,
                         image->hotspot_x,
                         image->hotspot_y);
  wl_surface_attach (cursor_surface, buffer, 0, 0);
  wl_surface_damage (cursor_surface, 0, 0,
                     image->width, image->height);
  wl_surface_commit (cursor_surface);
}

static void
pointer_handle_leave (void              *data,
                      struct wl_pointer *pointer,
                      uint32_t           serial,
                      struct wl_surface *surface)
{
}

static void
pointer_handle_motion (void              *data,
                       struct wl_pointer *pointer,
                       uint32_t           time,
                       wl_fixed_t         sx,
                       wl_fixed_t         sy)
{
}

static void
pointer_handle_button (void              *data,
                       struct wl_pointer *pointer,
                       uint32_t           serial,
                       uint32_t           time,
                       uint32_t           button,
                       uint32_t           state)
{
}

static void
pointer_handle_axis (void              *data,
                     struct wl_pointer *pointer,
                     uint32_t           time,
                     uint32_t           axis,
                     wl_fixed_t         value)
{
}

static const struct wl_pointer_listener pointer_listener = {
  pointer_handle_enter,
  pointer_handle_leave,
  pointer_handle_motion,
  pointer_handle_button,
  pointer_handle_axis,
};

static void
seat_handle_capabilities (void                    *data,
                          struct wl_seat          *wl_seat,
                          enum wl_seat_capability  caps)
{
  if (caps & WL_SEAT_CAPABILITY_POINTER)
    {
      wl_pointer = wl_seat_get_pointer (wl_seat);
      wl_pointer_add_listener (wl_pointer, &pointer_listener, NULL);
    }
}

static void
seat_handle_name (void           *data,
                  struct wl_seat *seat,
                  const char     *name)
{
}

static const struct wl_seat_listener seat_listener = {
  seat_handle_capabilities,
  seat_handle_name,
};

static void
handle_registry_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  if (strcmp (interface, "wl_seat") == 0)
    {
      wl_seat = wl_registry_bind (registry, id,
                                  &wl_seat_interface,
                                  1);
      wl_seat_add_listener (wl_seat, &seat_listener, NULL);
    }
}

static void
handle_registry_global_remove (void               *data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
  handle_registry_global,
  handle_registry_global_remove
};

static void
on_sync_event (WaylandDisplay *display,
               uint32_t        serial)
{
  if (serial == 1)
    {
      running = FALSE;
    }
  else if (serial == 0)
    {
      struct wl_buffer *buffer;
      struct wl_cursor_image *image;

      image = cursor2->images[0];
      buffer = wl_cursor_image_get_buffer (image);

      wl_surface_attach (cursor_surface, buffer, 0, 0);
      wl_surface_damage (cursor_surface, 0, 0,
                         image->width, image->height);
      wl_surface_commit (cursor_surface);

      wl_surface_destroy (cursor_surface);

      test_driver_sync_point (display->test_driver, 0, NULL);
    }
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  struct wl_registry *wl_registry;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_surface *xdg_surface;
  struct wl_cursor_theme *cursor_theme;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  wl_registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (wl_registry, &registry_listener, display);
  wl_display_roundtrip (display->display);

  g_signal_connect (display, "sync-event", G_CALLBACK (on_sync_event), NULL);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, display);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);

  cursor_surface = wl_compositor_create_surface (display->compositor);
  cursor_theme = wl_cursor_theme_load (NULL, 24, display->shm);
  cursor = wl_cursor_theme_get_cursor (cursor_theme, "default");
  cursor2 = wl_cursor_theme_get_cursor (cursor_theme, "text");
  g_assert_nonnull (cursor);
  g_assert_nonnull (cursor2);

  init_surface (xdg_toplevel);
  wl_surface_commit (surface);

  running = TRUE;
  while (running)
    wayland_display_dispatch (display);

  return EXIT_SUCCESS;
}

