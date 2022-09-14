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

#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <glib.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

#include "linux-dmabuf-unstable-v1-client-protocol.h"

typedef enum
{
  WINDOW_STATE_NONE,
  WINDOW_STATE_FULLSCREEN,
} WindowState;

typedef struct _Buffer
{
  struct wl_buffer *buffer;
  gboolean busy;
  gboolean recreate;
  int dmabuf_fds[4];
  struct gbm_bo *bo;
  int n_planes;
  uint32_t width, height, strides[4], offsets[4];
  uint32_t format;
  uint64_t modifier;
} Buffer;

static WaylandDisplay *display;
static struct wl_registry *registry;
static struct zwp_linux_dmabuf_v1 *dmabuf;

static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *xdg_toplevel;

struct gbm_device *gbm_device;

static GList *active_buffers;

static int prev_width;
static int prev_height;
static WindowState window_state;

static struct
{
  uint32_t format;
  uint64_t *modifiers;
  int n_modifiers;
} format_state = {
  .format = DRM_FORMAT_XRGB8888,
};

static gboolean running;

static void
buffer_free (Buffer *buffer)
{
  int i;

  g_clear_pointer (&buffer->buffer, wl_buffer_destroy);
  g_clear_pointer (&buffer->bo, gbm_bo_destroy);

  for (i = 0; i < buffer->n_planes; i++)
    close (buffer->dmabuf_fds[i]);

  g_free (buffer);
}

static void
handle_buffer_release (void             *user_data,
                       struct wl_buffer *buffer_resource)
{
  Buffer *buffer = user_data;

  active_buffers = g_list_remove (active_buffers, buffer);
  buffer_free (buffer);
}

static const struct wl_buffer_listener buffer_listener = {
  handle_buffer_release
};

static Buffer *
create_dma_buf_buffer (uint32_t      width,
                       uint32_t      height,
                       uint32_t      format,
                       unsigned int  n_modifiers,
                       uint64_t     *modifiers)
{
  Buffer *buffer;
  static uint32_t flags = 0;
  struct zwp_linux_buffer_params_v1 *params;
  int i;

  buffer = g_new0 (Buffer, 1);

  buffer->width = width;
  buffer->height = height;
  buffer->format = format;

  if (n_modifiers > 0)
    {
      buffer->bo = gbm_bo_create_with_modifiers2 (gbm_device,
                                                  buffer->width, buffer->height,
                                                  format, modifiers,
                                                  n_modifiers,
                                                  GBM_BO_USE_RENDERING |
                                                  GBM_BO_USE_SCANOUT);
      if (buffer->bo)
        buffer->modifier = gbm_bo_get_modifier (buffer->bo);
  }

  if (!buffer->bo)
    {
      buffer->bo = gbm_bo_create (gbm_device, buffer->width,
                                  buffer->height, buffer->format,
                                  GBM_BO_USE_RENDERING |
                                  GBM_BO_USE_SCANOUT);
      buffer->modifier = DRM_FORMAT_MOD_INVALID;
    }

  g_assert_nonnull (buffer->bo);

  buffer->n_planes = gbm_bo_get_plane_count (buffer->bo);

  params = zwp_linux_dmabuf_v1_create_params (dmabuf);

  for (i = 0; i < buffer->n_planes; i++)
    {
      buffer->dmabuf_fds[i] = gbm_bo_get_fd_for_plane (buffer->bo, i);
      buffer->strides[i] = gbm_bo_get_stride_for_plane (buffer->bo, i);
      buffer->offsets[i] = gbm_bo_get_offset (buffer->bo, i);
      g_assert_cmpint (buffer->dmabuf_fds[i], >=, 0);
      g_assert_cmpint (buffer->strides[i], >, 0);

      zwp_linux_buffer_params_v1_add (params, buffer->dmabuf_fds[i], i,
                                      buffer->offsets[i], buffer->strides[i],
                                      buffer->modifier >> 32,
                                      buffer->modifier & 0xffffffff);
    }

  buffer->buffer =
    zwp_linux_buffer_params_v1_create_immed(params,
                                            buffer->width,
                                            buffer->height,
                                            buffer->format,
                                            flags);

  g_assert_nonnull (buffer->buffer);

  wl_buffer_add_listener (buffer->buffer, &buffer_listener, buffer);

  return buffer;
}

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
  Buffer *buffer;

  buffer = create_dma_buf_buffer (width, height,
                                  format_state.format,
                                  format_state.n_modifiers,
                                  format_state.modifiers);

  active_buffers = g_list_prepend (active_buffers, buffer);

  wl_surface_attach (surface, buffer->buffer, 0, 0);
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
dmabuf_modifiers (void                       *user_data,
                  struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
                  uint32_t                    format,
                  uint32_t                    modifier_hi,
                  uint32_t                    modifier_lo)
{
  uint64_t modifier;

  modifier = ((uint64_t)modifier_hi << 32) | modifier_lo;
  if (format != format_state.format)
    return;

  if (modifier != DRM_FORMAT_MOD_INVALID)
    {
      format_state.n_modifiers++;
      format_state.modifiers = g_realloc_n (format_state.modifiers,
                                            format_state.n_modifiers,
                                            sizeof (uint64_t));
      format_state.modifiers[format_state.n_modifiers - 1] = modifier;
    }
}

static void
dmabuf_format (void                       *user_data,
               struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
               uint32_t                    format)
{
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
  dmabuf_format,
  dmabuf_modifiers
};

static void
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  if (strcmp (interface, "zwp_linux_dmabuf_v1") == 0)
    {
      g_assert_cmpuint (version, >=, 3);
      dmabuf = wl_registry_bind (registry, id,
                                 &zwp_linux_dmabuf_v1_interface, 3);
      zwp_linux_dmabuf_v1_add_listener (dmabuf, &dmabuf_listener, NULL);
    }
}

static void
handle_registry_global_remove (void               *user_data,
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
  g_assert (serial == 0);

  running = FALSE;
}

int
main (int    argc,
      char **argv)
{
  const char *gpu_path;
  int fd;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  g_signal_connect (display, "sync-event", G_CALLBACK (on_sync_event), NULL);
  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, NULL);
  wl_display_roundtrip (display->display);
  wl_display_roundtrip (display->display);

  g_assert_nonnull (dmabuf);

  gpu_path = lookup_property_value (display, "gpu-path");
  g_assert_nonnull (gpu_path);

  fd = open (gpu_path, O_RDWR);
  if (fd < 0)
    {
      g_error ("Failed to open drm render node %s: %s",
               gpu_path, g_strerror (errno));
    }

  gbm_device = gbm_create_device (fd);

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

  g_list_free_full (active_buffers, (GDestroyNotify) buffer_free);
  g_object_unref (display);

  return EXIT_SUCCESS;
}
