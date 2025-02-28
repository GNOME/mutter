/*
 * Copyright (C) 2025 Red Hat Inc.
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

static float toplevel_scale;
static float cursor_scale;
static float subsurface_scale;

static WaylandSurface *toplevel_surface;
static WaylandSurface *cursor_surface;
static WaylandSurface *subsurface;

static void
check_scales (float scale)
{
  g_assert_cmpfloat_with_epsilon (toplevel_scale, scale, FLT_EPSILON);
  g_assert_cmpint (toplevel_surface->preferred_buffer_scale,
                   ==,
                   (int32_t) ceilf (scale));

  g_assert_cmpfloat_with_epsilon (cursor_scale, scale, FLT_EPSILON);
  g_assert_cmpint (cursor_surface->preferred_buffer_scale,
                   ==,
                   (int32_t) ceilf (scale));

  g_assert_cmpfloat_with_epsilon (subsurface_scale, scale, FLT_EPSILON);
  g_assert_cmpint (subsurface->preferred_buffer_scale,
                   ==,
                   (int32_t) ceilf (scale));
}

static void
handle_preferred_scale (void                          *data,
                        struct wp_fractional_scale_v1 *fractional_scale,
                        uint32_t                       wire_scale)
{
  float *scale_ptr = data;

  *scale_ptr = wire_scale / 120.0f;
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
  .preferred_scale = handle_preferred_scale,
};

static void
watch_preferred_scales (WaylandDisplay    *display,
                        struct wl_surface *wl_surface,
                        float             *scale_ptr)
{
  struct wp_fractional_scale_v1 *fractional_scale;

  fractional_scale =
    wp_fractional_scale_manager_v1_get_fractional_scale (display->fractional_scale_mgr,
                                                         wl_surface);
  wp_fractional_scale_v1_add_listener (fractional_scale,
                                       &fractional_scale_listener,
                                       scale_ptr);
}

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

  if (!cursor_surface)
    {
      cursor_surface = wayland_surface_new_unassigned (display);

      watch_preferred_scales (display,
                              cursor_surface->wl_surface,
                              &cursor_scale);
    }

  theme_size = lookup_property_int (display, "cursor-theme-size");

  cursor_theme = wl_cursor_theme_load (NULL,
                                       theme_size,
                                       display->shm);
  cursor = wl_cursor_theme_get_cursor (cursor_theme, "default");
  image = cursor->images[0];
  buffer = wl_cursor_image_get_buffer (image);
  g_assert_nonnull (buffer);

  wl_pointer_set_cursor (pointer, serial,
                         cursor_surface->wl_surface,
                         image->hotspot_x, image->hotspot_y);
  wl_surface_attach (cursor_surface->wl_surface, buffer, 0, 0);
  wl_surface_damage_buffer (cursor_surface->wl_surface, 0, 0,
                            image->width, image->height);
  wl_surface_commit (cursor_surface->wl_surface);

  wl_cursor_theme_destroy (cursor_theme);
}

static void
on_sync_event (WaylandDisplay *display,
               uint32_t        serial,
               uint32_t       *out_scale)
{
  *out_scale = serial;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  uint32_t new_scale = UINT32_MAX;
  uint32_t prev_scale;
  struct wl_subsurface *wl_subsurface;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  g_signal_connect (display, "sync-event",
                    G_CALLBACK (on_sync_event), &new_scale);
  toplevel_surface = wayland_surface_new (display,
                                          "cursor-tests-surface",
                                          100, 100, 0xffffffff);
  g_signal_connect (toplevel_surface, "pointer-enter",
                    G_CALLBACK (on_pointer_enter), NULL);
  xdg_toplevel_set_fullscreen (toplevel_surface->xdg_toplevel, NULL);
  watch_preferred_scales (display,
                          toplevel_surface->wl_surface,
                          &toplevel_scale);

  subsurface = wayland_surface_new_unassigned (display);
  wl_subsurface =
    wl_subcompositor_get_subsurface (display->subcompositor,
                                     subsurface->wl_surface,
                                     toplevel_surface->wl_surface);
  draw_surface (display, subsurface->wl_surface, 10, 10, 0xff0000ff);
  watch_preferred_scales (display,
                          subsurface->wl_surface,
                          &subsurface_scale);
  wl_surface_commit (subsurface->wl_surface);

  wl_surface_commit (toplevel_surface->wl_surface);

  g_debug ("Waiting for scales to check");
  while (new_scale > 0)
    {
      prev_scale = new_scale;
      wayland_display_dispatch (display);
      wl_display_roundtrip (display->display);
      if (prev_scale != new_scale && new_scale > 0)
        {
          float scale = new_scale / 120.0f;

          g_debug ("Checking scale %f", scale);
          check_scales (scale);
          test_driver_sync_point (display->test_driver, 0, NULL);
        }
    }

  g_clear_pointer (&wl_subsurface, wl_subsurface_destroy);

  g_clear_object (&toplevel_surface);
  g_clear_object (&cursor_surface);
  g_clear_object (&subsurface);

  return EXIT_SUCCESS;
}
