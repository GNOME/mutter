/*
 * Copyright (C) 2024 Red Hat Inc.
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
 */

#include "config.h"

#include <wayland-cursor.h>

#include "wayland-test-client-utils.h"

typedef enum _CursorScaleMethod
{
  CURSOR_SCALE_METHOD_BUFFER_SCALE,
  CURSOR_SCALE_METHOD_VIEWPORT,
} CursorScaleMethod;

static CursorScaleMethod scale_method;
static char *cursor_name;

static struct wl_surface *cursor_surface;
static struct wp_viewport *cursor_viewport;

static void
on_pointer_enter (WaylandSurface    *surface,
                  struct wl_pointer *pointer,
                  uint32_t           serial)
{
  WaylandDisplay *display = surface->display;
  struct wl_cursor_theme *cursor_theme;
  struct wl_cursor *cursor;
  struct wl_cursor_image *image;
  struct wl_buffer *buffer;
  int theme_size;
  int num, denom;
  float scale;
  int ceiled_scale;
  int effective_theme_size = 0;
  float image_scale;
  int hotspot_x = 0;
  int hotspot_y = 0;

  if (!cursor_surface)
    cursor_surface = wl_compositor_create_surface (display->compositor);

  switch (scale_method)
    {
    case CURSOR_SCALE_METHOD_BUFFER_SCALE:
      g_clear_pointer (&cursor_viewport, wp_viewport_destroy);
      break;
    case CURSOR_SCALE_METHOD_VIEWPORT:
      if (!cursor_viewport)
        {
          cursor_viewport = wp_viewporter_get_viewport (display->viewporter,
                                                        cursor_surface);
        }
      break;
    }

  theme_size = lookup_property_int (display, "cursor-theme-size");
  num = lookup_property_int (display, "scale-num");
  denom = lookup_property_int (display, "scale-denom");
  scale = (float) num / (float) denom;
  ceiled_scale = (int) ceilf (scale);

  switch (scale_method)
    {
    case CURSOR_SCALE_METHOD_BUFFER_SCALE:
      effective_theme_size = (int) (theme_size * ceilf (scale));
      break;
    case CURSOR_SCALE_METHOD_VIEWPORT:
      effective_theme_size = (int) (theme_size * ceilf (scale));
      break;
    }

  g_debug ("Using effective cursor theme size %d for logical size %d "
           "and actual scale %f",
           effective_theme_size, theme_size, scale);

  cursor_theme = wl_cursor_theme_load (NULL,
                                       effective_theme_size,
                                       display->shm);
  cursor = wl_cursor_theme_get_cursor (cursor_theme, cursor_name);
  image = cursor->images[0];
  buffer = wl_cursor_image_get_buffer (image);
  g_assert_nonnull (buffer);

  image_scale = ((float) image->width / theme_size);

  switch (scale_method)
    {
    case CURSOR_SCALE_METHOD_BUFFER_SCALE:
      hotspot_x = (int) roundf (image->hotspot_x / ceiled_scale);
      hotspot_y = (int) roundf (image->hotspot_y / ceiled_scale);
      break;
    case CURSOR_SCALE_METHOD_VIEWPORT:
      hotspot_x = (int) roundf (image->hotspot_x / image_scale);
      hotspot_y = (int) roundf (image->hotspot_y / image_scale);
      break;
    }

  wl_pointer_set_cursor (pointer, serial,
                         cursor_surface,
                         hotspot_x, hotspot_y);
  wl_surface_attach (cursor_surface, buffer, 0, 0);
  wl_surface_damage_buffer (cursor_surface, 0, 0,
                            image->width, image->height);

  switch (scale_method)
    {
    case CURSOR_SCALE_METHOD_BUFFER_SCALE:
      wl_surface_set_buffer_scale (cursor_surface,
                                   ceiled_scale);
      break;
    case CURSOR_SCALE_METHOD_VIEWPORT:
      wp_viewport_set_destination (cursor_viewport,
                                   (int) roundf (image->width / image_scale),
                                   (int) roundf (image->height / image_scale));
      break;
    }

  wl_surface_commit (cursor_surface);

  wl_cursor_theme_destroy (cursor_theme);

  test_driver_sync_point (display->test_driver, 0, NULL);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  if (g_strcmp0 (argv[1], "buffer-scale") == 0)
    scale_method = CURSOR_SCALE_METHOD_BUFFER_SCALE;
  else if (g_strcmp0 (argv[1], "viewport") == 0)
    scale_method = CURSOR_SCALE_METHOD_VIEWPORT;
  else
    g_error ("Missing scale method");

  cursor_name = argv[2];

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  surface = wayland_surface_new (display,
                                 "cursor-tests-surface",
                                 100, 100, 0xffffffff);
  g_signal_connect (surface, "pointer-enter",
                    G_CALLBACK (on_pointer_enter), NULL);
  xdg_toplevel_set_fullscreen (surface->xdg_toplevel, NULL);
  wl_surface_commit (surface->wl_surface);

  wait_for_sync_event (display, 0);

  return EXIT_SUCCESS;
}
