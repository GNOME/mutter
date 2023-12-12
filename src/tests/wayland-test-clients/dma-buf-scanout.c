/*
 * Copyright © 2022 Red Hat Inc.
 * Copyright © 2020 Collabora, Ltd.
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
 *
 * Original license of parts of the DMA buffer parts:
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <fcntl.h>
#include <glib.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

typedef enum
{
  WINDOW_STATE_NONE,
  WINDOW_STATE_FULLSCREEN,
} WindowState;

static WaylandDisplay *display;

static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *xdg_toplevel;

static GList *active_buffers;

static int prev_width;
static int prev_height;
static WindowState window_state;

static gboolean running;

static void
handle_buffer_release (void             *user_data,
                       struct wl_buffer *buffer_resource)
{
  WaylandBuffer *buffer = user_data;

  active_buffers = g_list_remove (active_buffers, buffer);
  g_object_unref (buffer);
}

static const struct wl_buffer_listener buffer_listener = {
  handle_buffer_release
};

static void
init_surface (void)
{
  xdg_toplevel_set_title (xdg_toplevel, "dma-buf-scanout-test");
  xdg_toplevel_set_fullscreen (xdg_toplevel, NULL);
  wl_surface_commit (surface);
}

static void
draw_main (int width,
           int height)
{
  WaylandBuffer *buffer;
  DmaBufFormat *format;

  format = g_hash_table_lookup (display->formats,
                                GUINT_TO_POINTER (DRM_FORMAT_XRGB8888));
  g_assert_nonnull (format);

  buffer = wayland_buffer_create (display,
                                  &buffer_listener,
                                  width, height,
                                  format->format,
                                  format->modifiers,
                                  format->n_modifiers,
                                  GBM_BO_USE_RENDERING |
                                  GBM_BO_USE_SCANOUT);
  g_assert_nonnull (buffer);

  active_buffers = g_list_prepend (active_buffers, buffer);

  wl_surface_attach (surface, wayland_buffer_get_wl_buffer (buffer), 0, 0);
}

static WindowState
parse_xdg_toplevel_state (struct wl_array *states)
{
  uint32_t *state_ptr;

  wl_array_for_each (state_ptr, states)
    {
      uint32_t state = *state_ptr;

      if (state == XDG_TOPLEVEL_STATE_FULLSCREEN)
        return WINDOW_STATE_FULLSCREEN;
    }

  return WINDOW_STATE_NONE;
}

static void
handle_xdg_toplevel_configure (void                *user_data,
                               struct xdg_toplevel *xdg_toplevel,
                               int32_t              width,
                               int32_t              height,
                               struct wl_array     *states)
{
  g_assert (width > 0 || prev_width > 0);
  g_assert (height > 0 || prev_width > 0);

  if (width > 0 && height > 0)
    {
      prev_width = width;
      prev_height = height;
    }
  else
    {
      width = prev_width;
      height = prev_height;
    }

  window_state = parse_xdg_toplevel_state (states);

  draw_main (width, height);
}

static void
handle_xdg_toplevel_close (void                *user_data,
                           struct xdg_toplevel *xdg_toplevel)
{
  g_assert_not_reached ();
}

static void
handle_xdg_toplevel_configure_bounds (void                *user_data,
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
handle_frame_callback (void               *user_data,
                       struct wl_callback *callback,
                       uint32_t            time)
{
}

static const struct wl_callback_listener frame_listener = {
  handle_frame_callback,
};

static void
handle_xdg_surface_configure (void               *user_data,
                              struct xdg_surface *xdg_surface,
                              uint32_t            serial)
{
  struct wl_callback *frame_callback;

  xdg_surface_ack_configure (xdg_surface, serial);
  frame_callback = wl_surface_frame (surface);
  wl_callback_add_listener (frame_callback, &frame_listener, NULL);
  wl_surface_commit (surface);
  test_driver_sync_point (display->test_driver, window_state, NULL);
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

  running = FALSE;
}

int
main (int    argc,
      char **argv)
{
  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  g_signal_connect (display, "sync-event", G_CALLBACK (on_sync_event), NULL);
  wl_display_roundtrip (display->display);
  g_assert_nonnull (display->gbm_device);

  surface = wl_compositor_create_surface (display->compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface (display->xdg_wm_base, surface);
  xdg_surface_add_listener (xdg_surface, &xdg_surface_listener, NULL);
  xdg_toplevel = xdg_surface_get_toplevel (xdg_surface);
  xdg_toplevel_add_listener (xdg_toplevel, &xdg_toplevel_listener, NULL);

  init_surface ();

  running = TRUE;
  while (running)
    {
      if (wl_display_dispatch (display->display) == -1)
        return EXIT_FAILURE;
    }

  g_list_free_full (active_buffers, (GDestroyNotify) g_object_unref);
  g_object_unref (display);

  return EXIT_SUCCESS;
}
