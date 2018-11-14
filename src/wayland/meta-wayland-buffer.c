/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Endless Mobile
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "wayland/meta-wayland-buffer.h"

#include <drm_fourcc.h>

#include "backends/meta-backend-private.h"
#include "clutter/clutter.h"
#include "cogl/cogl-egl.h"
#include "meta/util.h"
#include "wayland/meta-wayland-dma-buf.h"

#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)
#endif

enum
{
  RESOURCE_DESTROYED,

  LAST_SIGNAL
};

guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (MetaWaylandBuffer, meta_wayland_buffer, G_TYPE_OBJECT);

static void
meta_wayland_buffer_destroy_handler (struct wl_listener *listener,
                                     void *data)
{
  MetaWaylandBuffer *buffer =
    wl_container_of (listener, buffer, destroy_listener);

  buffer->resource = NULL;
  g_signal_emit (buffer, signals[RESOURCE_DESTROYED], 0);
  g_object_unref (buffer);
}

MetaWaylandBuffer *
meta_wayland_buffer_from_resource (struct wl_resource *resource)
{
  MetaWaylandBuffer *buffer;
  struct wl_listener *listener;

  listener =
    wl_resource_get_destroy_listener (resource,
                                      meta_wayland_buffer_destroy_handler);

  if (listener)
    {
      buffer = wl_container_of (listener, buffer, destroy_listener);
    }
  else
    {
      buffer = g_object_new (META_TYPE_WAYLAND_BUFFER, NULL);

      buffer->resource = resource;
      buffer->destroy_listener.notify = meta_wayland_buffer_destroy_handler;
      wl_resource_add_destroy_listener (resource, &buffer->destroy_listener);
    }

  return buffer;
}

struct wl_resource *
meta_wayland_buffer_get_resource (MetaWaylandBuffer *buffer)
{
  return buffer->resource;
}

gboolean
meta_wayland_buffer_is_realized (MetaWaylandBuffer *buffer)
{
  return buffer->type != META_WAYLAND_BUFFER_TYPE_UNKNOWN;
}

gboolean
meta_wayland_buffer_realize (MetaWaylandBuffer *buffer)
{
  EGLint format;
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
#ifdef HAVE_WAYLAND_EGLSTREAM
  MetaWaylandEglStream *stream;
#endif
  MetaWaylandDmaBufBuffer *dma_buf;

  if (wl_shm_buffer_get (buffer->resource) != NULL)
    {
      buffer->type = META_WAYLAND_BUFFER_TYPE_SHM;
      return TRUE;
    }

  if (meta_egl_query_wayland_buffer (egl, egl_display, buffer->resource,
                                     EGL_TEXTURE_FORMAT, &format,
                                     NULL))
    {
      buffer->type = META_WAYLAND_BUFFER_TYPE_EGL_IMAGE;
      return TRUE;
    }

#ifdef HAVE_WAYLAND_EGLSTREAM
  stream = meta_wayland_egl_stream_new (buffer, NULL);
  if (stream)
    {
      CoglTexture2D *texture;

      texture = meta_wayland_egl_stream_create_texture (stream, NULL);
      if (!texture)
        return FALSE;

      buffer->egl_stream.stream = stream;
      buffer->type = META_WAYLAND_BUFFER_TYPE_EGL_STREAM;
      buffer->texture = COGL_TEXTURE (texture);
      buffer->is_y_inverted = meta_wayland_egl_stream_is_y_inverted (stream);

      return TRUE;
    }
#endif /* HAVE_WAYLAND_EGLSTREAM */

  dma_buf = meta_wayland_dma_buf_from_buffer (buffer);
  if (dma_buf)
    {
      buffer->dma_buf.dma_buf = dma_buf;
      buffer->type = META_WAYLAND_BUFFER_TYPE_DMA_BUF;
      return TRUE;
    }

  return FALSE;
}

static void
shm_buffer_get_cogl_pixel_format (struct wl_shm_buffer  *shm_buffer,
                                  CoglPixelFormat       *format_out,
                                  CoglTextureComponents *components_out,
                                  guint                 *n_planes_out)
{
  CoglPixelFormat format;
  CoglTextureComponents components = COGL_TEXTURE_COMPONENTS_RGBA;
  guint n_planes = 1;

  g_warning ("SHM BUFFER_FORMAT: %d", wl_shm_buffer_get_format (shm_buffer));
  switch (wl_shm_buffer_get_format (shm_buffer))
    {
#if G_BYTE_ORDER == G_BIG_ENDIAN
    case WL_SHM_FORMAT_ARGB8888:
      format = COGL_PIXEL_FORMAT_ARGB_8888_PRE;
      break;
    case WL_SHM_FORMAT_XRGB8888:
      format = COGL_PIXEL_FORMAT_ARGB_8888;
      components = COGL_TEXTURE_COMPONENTS_RGB;
      break;
#elif G_BYTE_ORDER == G_LITTLE_ENDIAN
    case WL_SHM_FORMAT_ARGB8888:
      format = COGL_PIXEL_FORMAT_BGRA_8888_PRE;
      break;
    case WL_SHM_FORMAT_XRGB8888:
      format = COGL_PIXEL_FORMAT_BGRA_8888;
      components = COGL_TEXTURE_COMPONENTS_RGB;
      break;
#endif
    case WL_SHM_FORMAT_NV12:
      format = COGL_PIXEL_FORMAT_NV12;
      n_planes = 2;
      g_warning ("FORMAT IS NV12");
      break;
    case WL_SHM_FORMAT_NV21:
      g_warning ("FORMAT IS NV21");
      break;
    case WL_SHM_FORMAT_YUV422:
      g_warning ("FORMAT IS YUV422");
      break;
    case WL_SHM_FORMAT_YVU422:
      g_warning ("FORMAT IS YVU422");
      break;
    case WL_SHM_FORMAT_YUV444:
      g_warning ("FORMAT IS YUV444");
      break;
    case WL_SHM_FORMAT_YVU444:
      g_warning ("FORMAT IS YVU444");
      break;

    default:
      g_warn_if_reached ();
      format = COGL_PIXEL_FORMAT_ARGB_8888;
    }

  if (format_out)
    *format_out = format;
  if (components_out)
    *components_out = components;
  if (n_planes_out)
    *n_planes_out = n_planes;
}

static gboolean
shm_buffer_attach (MetaWaylandBuffer *buffer,
                   GError           **error)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  struct wl_shm_buffer *shm_buffer;
  int stride, width, height;
  CoglPixelFormat format;
  CoglTextureComponents components;
  guint i, n_planes;
  guint h_subsampling[3], v_subsampling[3];
  gsize offset = 0;
  const guint8 *data;
  GPtrArray *planes;
  gboolean ret;

  if (buffer->texture)
    return TRUE;

  /* Query the necessary parameters */
  shm_buffer = wl_shm_buffer_get (buffer->resource);
  stride = wl_shm_buffer_get_stride (shm_buffer);
  width = wl_shm_buffer_get_width (shm_buffer);
  height = wl_shm_buffer_get_height (shm_buffer);

  shm_buffer_get_cogl_pixel_format (shm_buffer, &format, &components, &n_planes);
  cogl_pixel_format_get_subsampling_parameters (format,
                                                h_subsampling,
                                                v_subsampling);

  /* Safely access the data inside the buffer */
  wl_shm_buffer_begin_access (shm_buffer);
  data = wl_shm_buffer_get_data (shm_buffer);

  planes = g_ptr_array_new_full (n_planes, cogl_object_unref);
  for (i = 0; i < n_planes; i++)
    {
      CoglBitmap *bitmap;
      CoglTexture *plane;

      /* Internally, the texture's planes are laid out in memory as one
       * contiguous block, so we have to consider any subsampling (based on the
       * pixel format). */
      if (i == 0)
        offset = 0;
      else
        offset += (stride / h_subsampling[i-1]) * (height / v_subsampling[i-1]);

      bitmap = cogl_bitmap_new_for_data (cogl_context,
                                         width / h_subsampling[i],
                                         height / v_subsampling[i],
                                         format,
                                         stride, /* XXX Do we need to change this too?*/
                                         data + offset);
      g_assert (bitmap);

      plane = COGL_TEXTURE (cogl_texture_2d_new_from_bitmap (bitmap));
      cogl_texture_set_components (COGL_TEXTURE (plane), components);

      cogl_object_unref (bitmap);

      if (G_UNLIKELY (!cogl_texture_allocate (COGL_TEXTURE (plane), error)))
        {
          g_clear_pointer (&plane, cogl_object_unref);
          ret = FALSE;
          goto out;
        }

      g_ptr_array_add (planes, plane);
    }

  buffer->texture = cogl_multi_plane_texture_new (format,
                                             g_ptr_array_free (planes, FALSE),
                                             n_planes);
  buffer->is_y_inverted = TRUE;
  ret = TRUE;

out:
  wl_shm_buffer_end_access (shm_buffer);

  if (!ret)
    g_ptr_array_free (planes, TRUE);

  return ret;
}

static gboolean
egl_image_buffer_attach (MetaWaylandBuffer *buffer,
                         GError           **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  int format, width, height, y_inverted;
  CoglPixelFormat cogl_format;
  guint i, n_planes;
  GPtrArray *planes;
  gboolean ret = FALSE;
  EGLint attrib_list[3] = { EGL_NONE, EGL_NONE, EGL_NONE };

  if (buffer->texture)
    return TRUE;

  /* Query the necessary properties */
  if (!meta_egl_query_wayland_buffer (egl, egl_display, buffer->resource,
                                      EGL_TEXTURE_FORMAT, &format,
                                      error))
    return FALSE;

  if (!meta_egl_query_wayland_buffer (egl, egl_display, buffer->resource,
                                      EGL_WIDTH, &width,
                                      error))
    return FALSE;

  if (!meta_egl_query_wayland_buffer (egl, egl_display, buffer->resource,
                                      EGL_HEIGHT, &height,
                                      error))
    return FALSE;

  if (!meta_egl_query_wayland_buffer (egl, egl_display, buffer->resource,
                                      EGL_WAYLAND_Y_INVERTED_WL, &y_inverted,
                                      NULL))
    y_inverted = EGL_TRUE;

  /* Map the EGL texture format to CoglPixelFormat, if possible */
  switch (format)
    {
    case EGL_TEXTURE_RGB:
      cogl_format = COGL_PIXEL_FORMAT_RGB_888;
      break;
    case EGL_TEXTURE_RGBA:
      cogl_format = COGL_PIXEL_FORMAT_RGBA_8888_PRE;
      break;
    case EGL_TEXTURE_Y_UV_WL:
      g_warning ("Got a NV12 color format texture!!");
      cogl_format = COGL_PIXEL_FORMAT_NV12;
      break;
    case EGL_TEXTURE_Y_U_V_WL:
      g_warning ("Got a YUV 4:4:4 color format texture!!");
      cogl_format = COGL_PIXEL_FORMAT_YUV444;
      break;
    default:
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unsupported buffer format %d", format);
      return FALSE;
    }

  n_planes = cogl_pixel_format_get_n_planes (cogl_format);
  planes = g_ptr_array_new_full (n_planes, cogl_object_unref);

  /* Each EGLImage is a plane in the final texture */
  for (i = 0; i < n_planes; i++)
    {
      EGLImageKHR egl_img;
      CoglTexture2D *texture;

      /* Specify that we want the i'th plane */
      attrib_list[0] = EGL_WAYLAND_PLANE_WL;
      attrib_list[1] = i;

      /* The WL_bind_wayland_display spec states that EGL_NO_CONTEXT is to be
       * used in conjunction with the EGL_WAYLAND_BUFFER_WL target. */
      egl_img = meta_egl_create_image (egl, egl_display, EGL_NO_CONTEXT,
                                       EGL_WAYLAND_BUFFER_WL, buffer->resource,
                                       attrib_list,
                                       error);

      if (G_UNLIKELY (egl_img == EGL_NO_IMAGE_KHR))
        goto out;

      texture = cogl_egl_texture_2d_new_from_image (cogl_context,
                                                    width, height,
                                                    cogl_format,
                                                    egl_img,
                                                    error);

      meta_egl_destroy_image (egl, egl_display, egl_img, NULL);

      if (G_UNLIKELY (!texture))
        goto out;

      g_ptr_array_add (planes, texture);
    }


  buffer->texture = cogl_multi_plane_texture_new (cogl_format,
                                             g_ptr_array_free (planes, FALSE),
                                             n_planes);
  buffer->is_y_inverted = !!y_inverted;

  ret = TRUE;

out:
  if (!ret)
    g_ptr_array_free (planes, TRUE);

  return ret;
}

#ifdef HAVE_WAYLAND_EGLSTREAM
static gboolean
egl_stream_buffer_attach (MetaWaylandBuffer  *buffer,
                          GError            **error)
{
  MetaWaylandEglStream *stream = buffer->egl_stream.stream;

  g_assert (stream);

  if (!meta_wayland_egl_stream_attach (stream, error))
    return FALSE;

  return TRUE;
}
#endif /* HAVE_WAYLAND_EGLSTREAM */

gboolean
meta_wayland_buffer_attach (MetaWaylandBuffer *buffer,
                            GError           **error)
{
  g_return_val_if_fail (buffer->resource, FALSE);

  if (!meta_wayland_buffer_is_realized (buffer))
    {
      /* The buffer should have been realized at surface commit time */
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unknown buffer type");
      return FALSE;
    }

  switch (buffer->type)
    {
    case META_WAYLAND_BUFFER_TYPE_SHM:
      g_warning ("GOT SHM BUFFER");
      return shm_buffer_attach (buffer, error);
    case META_WAYLAND_BUFFER_TYPE_EGL_IMAGE:
      g_warning ("GOT EGL IMAGE BUFFER");
      return egl_image_buffer_attach (buffer, error);
#ifdef HAVE_WAYLAND_EGLSTREAM
    case META_WAYLAND_BUFFER_TYPE_EGL_STREAM:
      g_warning ("GOT EGL STREAM BUFFER");
      return egl_stream_buffer_attach (buffer, error);
#endif
    case META_WAYLAND_BUFFER_TYPE_DMA_BUF:
      g_warning ("GOT DMA BUF BUFFER");
      return meta_wayland_dma_buf_buffer_attach (buffer, error);
    case META_WAYLAND_BUFFER_TYPE_UNKNOWN:
      g_assert_not_reached ();
      return FALSE;
    }

  g_assert_not_reached ();
}

CoglMultiPlaneTexture *
meta_wayland_buffer_get_texture (MetaWaylandBuffer *buffer)
{
  return buffer->texture;
}

CoglSnippet *
meta_wayland_buffer_create_snippet (MetaWaylandBuffer *buffer)
{
#ifdef HAVE_WAYLAND_EGLSTREAM
  if (!buffer->egl_stream.stream)
    return NULL;

  return meta_wayland_egl_stream_create_snippet ();
#else
  return NULL;
#endif /* HAVE_WAYLAND_EGLSTREAM */
}

gboolean
meta_wayland_buffer_is_y_inverted (MetaWaylandBuffer *buffer)
{
  return buffer->is_y_inverted;
}

static gboolean
process_shm_buffer_damage (MetaWaylandBuffer *buffer,
                           cairo_region_t    *region,
                           GError           **error)
{
  struct wl_shm_buffer *shm_buffer;
  int i, n_rectangles;
  gboolean set_texture_failed = FALSE;

  n_rectangles = cairo_region_num_rectangles (region);

  shm_buffer = wl_shm_buffer_get (buffer->resource);
  wl_shm_buffer_begin_access (shm_buffer);

  for (i = 0; i < n_rectangles; i++)
    {
      const uint8_t *data = wl_shm_buffer_get_data (shm_buffer);
      int32_t stride = wl_shm_buffer_get_stride (shm_buffer);
      CoglPixelFormat format;
      guint n_planes;
      int bpp;
      cairo_rectangle_int_t rect;

      shm_buffer_get_cogl_pixel_format (shm_buffer, &format, NULL, &n_planes);
      bpp = _cogl_pixel_format_get_bytes_per_pixel (format);
      cairo_region_get_rectangle (region, i, &rect);

      for (i = 0; i < n_planes; i++)
        {
          CoglTexture *plane = cogl_multi_plane_texture_get_plane (buffer->texture, i);

          if (!_cogl_texture_set_region (plane,
                                         rect.width, rect.height,
                                         format,
                                         stride,
                                         data + rect.x * bpp + rect.y * stride,
                                         rect.x, rect.y,
                                         0,
                                         error))
            {
              set_texture_failed = TRUE;
              goto out;
            }
        }
    }

out:
  wl_shm_buffer_end_access (shm_buffer);

  return !set_texture_failed;
}

void
meta_wayland_buffer_process_damage (MetaWaylandBuffer *buffer,
                                    cairo_region_t    *region)
{
  gboolean res = FALSE;
  GError *error = NULL;

  g_return_if_fail (buffer->resource);

  switch (buffer->type)
    {
    case META_WAYLAND_BUFFER_TYPE_SHM:
      res = process_shm_buffer_damage (buffer, region, &error);
      break;
    case META_WAYLAND_BUFFER_TYPE_EGL_IMAGE:
#ifdef HAVE_WAYLAND_EGLSTREAM
    case META_WAYLAND_BUFFER_TYPE_EGL_STREAM:
#endif
    case META_WAYLAND_BUFFER_TYPE_DMA_BUF:
      res = TRUE;
      break;
    case META_WAYLAND_BUFFER_TYPE_UNKNOWN:
      g_set_error (&error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unknown buffer type");
      res = FALSE;
      break;
    }

  if (!res)
    {
      g_warning ("Failed to process Wayland buffer damage: %s", error->message);
      g_error_free (error);
    }
}

static void
meta_wayland_buffer_finalize (GObject *object)
{
  MetaWaylandBuffer *buffer = META_WAYLAND_BUFFER (object);

  cogl_clear_object (&buffer->texture);
#ifdef HAVE_WAYLAND_EGLSTREAM
  g_clear_object (&buffer->egl_stream.stream);
#endif
  g_clear_object (&buffer->dma_buf.dma_buf);

  G_OBJECT_CLASS (meta_wayland_buffer_parent_class)->finalize (object);
}

static void
meta_wayland_buffer_init (MetaWaylandBuffer *buffer)
{
}

static void
meta_wayland_buffer_class_init (MetaWaylandBufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_buffer_finalize;

  signals[RESOURCE_DESTROYED] = g_signal_new ("resource-destroyed",
                                              G_TYPE_FROM_CLASS (object_class),
                                              G_SIGNAL_RUN_LAST,
                                              0,
                                              NULL, NULL, NULL,
                                              G_TYPE_NONE, 0);
}
