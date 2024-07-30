/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "wayland/meta-wayland-single-pixel-buffer.h"

#include "backends/meta-backend-private.h"
#include "cogl/cogl-half-float.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-private.h"

#include "single-pixel-buffer-v1-server-protocol.h"

struct _MetaWaylandSinglePixelBuffer
{
  uint32_t r;
  uint32_t g;
  uint32_t b;
  uint32_t a;
};

static void
buffer_destroy (struct wl_client   *client,
                struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_buffer_interface single_pixel_buffer_implementation =
{
  buffer_destroy,
};

static void
single_pixel_buffer_manager_destroy (struct wl_client *client,
                                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
single_pixel_buffer_manager_create_1px_rgba32_buffer (struct wl_client   *client,
                                                      struct wl_resource *resource,
                                                      uint32_t            buffer_id,
                                                      uint32_t            r,
                                                      uint32_t            g,
                                                      uint32_t            b,
                                                      uint32_t            a)
{
  MetaWaylandCompositor *compositor = wl_resource_get_user_data (resource);
  MetaWaylandSinglePixelBuffer *single_pixel_buffer;
  struct wl_resource *buffer_resource;

  single_pixel_buffer = g_new0 (MetaWaylandSinglePixelBuffer, 1);
  single_pixel_buffer->r = r;
  single_pixel_buffer->g = g;
  single_pixel_buffer->b = b;
  single_pixel_buffer->a = a;

  buffer_resource =
    wl_resource_create (client, &wl_buffer_interface, 1, buffer_id);
  wl_resource_set_implementation (buffer_resource,
                                  &single_pixel_buffer_implementation,
                                  single_pixel_buffer, NULL);
  meta_wayland_buffer_from_resource (compositor, buffer_resource);
}

static const struct wp_single_pixel_buffer_manager_v1_interface
  single_pixel_buffer_manager_implementation =
{
  single_pixel_buffer_manager_destroy,
  single_pixel_buffer_manager_create_1px_rgba32_buffer,
};

static void
single_pixel_buffer_manager_bind (struct wl_client *client,
                                  void             *user_data,
                                  uint32_t          version,
                                  uint32_t          id)
{
  MetaWaylandCompositor *compositor = user_data;
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_single_pixel_buffer_manager_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource,
                                  &single_pixel_buffer_manager_implementation,
                                  compositor, NULL);
}

static void
get_data_in_half_float_format (MetaWaylandSinglePixelBuffer  *single_pixel_buffer,
                               CoglPixelFormat               *pixel_format,
                               int                           *rowstride,
                               uint8_t                      **data)
{
  uint16_t *d;

  if (single_pixel_buffer->a == UINT32_MAX)
    *pixel_format = COGL_PIXEL_FORMAT_BGRX_FP_16161616;
  else
    *pixel_format = COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE;

  *rowstride = 4 * sizeof (uint16_t);

  d = g_malloc0 (*rowstride);
  d[0] = cogl_float_to_half ((float) single_pixel_buffer->b / (float) UINT32_MAX);
  d[1] = cogl_float_to_half ((float) single_pixel_buffer->g / (float) UINT32_MAX);
  d[2] = cogl_float_to_half ((float) single_pixel_buffer->r / (float) UINT32_MAX);
  d[3] = cogl_float_to_half ((float) single_pixel_buffer->a / (float) UINT32_MAX);

  *data = (uint8_t *) d;
}

static void
get_data_in_ABGR_2101010_format (MetaWaylandSinglePixelBuffer  *single_pixel_buffer,
                                 CoglPixelFormat               *pixel_format,
                                 int                           *rowstride,
                                 uint8_t                      **data)
{
  uint32_t a, b, g, r;
  uint32_t *d;

  if (single_pixel_buffer->a == UINT32_MAX)
    *pixel_format = COGL_PIXEL_FORMAT_XBGR_2101010;
  else
    *pixel_format = COGL_PIXEL_FORMAT_ABGR_2101010_PRE;

  *rowstride = sizeof (uint32_t);

  a = 3;
  b = single_pixel_buffer->b / (UINT32_MAX / 0x3ff);
  g = single_pixel_buffer->g / (UINT32_MAX / 0x3ff);
  r = single_pixel_buffer->r / (UINT32_MAX / 0x3ff);

  d = g_malloc0 (*rowstride);
  *d = (a << 30) | (b << 20) | (g << 10) | r;

  *data = (uint8_t *) d;
}

static void
get_data_in_BGRA_8888_format (MetaWaylandSinglePixelBuffer  *single_pixel_buffer,
                              CoglPixelFormat               *pixel_format,
                              int                           *rowstride,
                              uint8_t                      **data)
{
  if (single_pixel_buffer->a == UINT32_MAX)
    *pixel_format = COGL_PIXEL_FORMAT_BGR_888;
  else
    *pixel_format = COGL_PIXEL_FORMAT_BGRA_8888_PRE;

  *rowstride = 4 * sizeof (uint8_t);

  *data = g_malloc0 (*rowstride);
  (*data)[0] = single_pixel_buffer->b / (UINT32_MAX / 0xff);
  (*data)[1] = single_pixel_buffer->g / (UINT32_MAX / 0xff);
  (*data)[2] = single_pixel_buffer->r / (UINT32_MAX / 0xff);
  (*data)[3] = single_pixel_buffer->a / (UINT32_MAX / 0xff);
}

gboolean
meta_wayland_single_pixel_buffer_attach (MetaWaylandBuffer  *buffer,
                                         MetaMultiTexture  **texture,
                                         GError            **error)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (buffer->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  MetaWaylandSinglePixelBuffer *single_pixel_buffer =
    wl_resource_get_user_data (buffer->resource);
  g_autofree uint8_t *data = NULL;
  CoglPixelFormat pixel_format;
  CoglTexture *tex_2d;
  int rowstride;

  if (buffer->single_pixel.texture)
    {
      *texture = g_object_ref (buffer->single_pixel.texture);
      return TRUE;
    }

  if (cogl_context_has_feature (cogl_context, COGL_FEATURE_ID_TEXTURE_HALF_FLOAT))
    {
      get_data_in_half_float_format (single_pixel_buffer,
                                     &pixel_format,
                                     &rowstride,
                                     &data);
    }
  else if (cogl_context_has_feature (cogl_context, COGL_FEATURE_ID_TEXTURE_RGBA1010102) &&
           single_pixel_buffer->a == UINT32_MAX)
    {
      get_data_in_ABGR_2101010_format (single_pixel_buffer,
                                       &pixel_format,
                                       &rowstride,
                                       &data);
    }
  else
    {
      get_data_in_BGRA_8888_format (single_pixel_buffer,
                                    &pixel_format,
                                    &rowstride,
                                    &data);
    }

  tex_2d = cogl_texture_2d_new_from_data (cogl_context,
                                          1, 1,
                                          pixel_format,
                                          rowstride, data,
                                          error);
  if (!tex_2d)
    return FALSE;

  buffer->single_pixel.texture =
    meta_multi_texture_new_simple (tex_2d);

  g_clear_object (texture);
  *texture = g_object_ref (buffer->single_pixel.texture);
  return TRUE;
}

MetaWaylandSinglePixelBuffer *
meta_wayland_single_pixel_buffer_from_buffer (MetaWaylandBuffer *buffer)
{
  if (!buffer->resource)
    return NULL;

  if (wl_resource_instance_of (buffer->resource, &wl_buffer_interface,
                               &single_pixel_buffer_implementation))
    return wl_resource_get_user_data (buffer->resource);

  return NULL;
}

void
meta_wayland_single_pixel_buffer_free (MetaWaylandSinglePixelBuffer *single_pixel_buffer)
{
  g_free (single_pixel_buffer);
}

gboolean
meta_wayland_single_pixel_buffer_is_opaque_black (MetaWaylandSinglePixelBuffer *single_pixel_buffer)
{
  return (single_pixel_buffer->a == UINT32_MAX &&
          single_pixel_buffer->r == 0x0 &&
          single_pixel_buffer->g == 0x0 &&
          single_pixel_buffer->b == 0x0);
}

void
meta_wayland_init_single_pixel_buffer_manager (MetaWaylandCompositor *compositor)
{
  if (!wl_global_create (compositor->wayland_display,
                         &wp_single_pixel_buffer_manager_v1_interface,
                         META_WP_SINGLE_PIXEL_BUFFER_V1_VERSION,
                         compositor,
                         single_pixel_buffer_manager_bind))
    g_warning ("Failed to create wp_single_pixel_buffer_manager_v1 global");
}
