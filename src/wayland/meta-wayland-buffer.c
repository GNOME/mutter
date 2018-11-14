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

#ifdef HAVE_WAYLAND_EGLSTREAM
  stream = meta_wayland_egl_stream_new (buffer, NULL);
  if (stream)
    {
      CoglMultiPlaneTexture *texture;

      texture = meta_wayland_egl_stream_create_texture (stream, NULL);
      if (!texture)
        return FALSE;

      buffer->egl_stream.stream = stream;
      buffer->type = META_WAYLAND_BUFFER_TYPE_EGL_STREAM;
      buffer->egl_stream.texture = texture;
      buffer->is_y_inverted = meta_wayland_egl_stream_is_y_inverted (stream);

      return TRUE;
    }
#endif /* HAVE_WAYLAND_EGLSTREAM */

  if (meta_egl_query_wayland_buffer (egl, egl_display, buffer->resource,
                                     EGL_TEXTURE_FORMAT, &format,
                                     NULL))
    {
      buffer->type = META_WAYLAND_BUFFER_TYPE_EGL_IMAGE;
      return TRUE;
    }

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
                                  CoglTextureComponents *components_out)
{
  CoglPixelFormat format;

  switch (wl_shm_buffer_get_format (shm_buffer))
    {
#if G_BYTE_ORDER == G_BIG_ENDIAN
    case WL_SHM_FORMAT_ARGB8888:
      format = COGL_PIXEL_FORMAT_ARGB_8888_PRE;
      components_out[0] = COGL_TEXTURE_COMPONENTS_RGBA;
      break;
    case WL_SHM_FORMAT_XRGB8888:
      format = COGL_PIXEL_FORMAT_ARGB_8888;
      components_out[0] = COGL_TEXTURE_COMPONENTS_RGB;
      break;
#elif G_BYTE_ORDER == G_LITTLE_ENDIAN
    case WL_SHM_FORMAT_ARGB8888:
      format = COGL_PIXEL_FORMAT_BGRA_8888_PRE;
      components_out[0] = COGL_TEXTURE_COMPONENTS_RGBA;
      break;
    case WL_SHM_FORMAT_XRGB8888:
      format = COGL_PIXEL_FORMAT_BGRA_8888;
      components_out[0] = COGL_TEXTURE_COMPONENTS_RGB;
      break;
#endif
    case WL_SHM_FORMAT_NV12:
      format = COGL_PIXEL_FORMAT_NV12;
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_RG;
      break;
    case WL_SHM_FORMAT_NV21:
      format = COGL_PIXEL_FORMAT_NV21;
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_RG;
      break;
    case WL_SHM_FORMAT_YUV422:
      format = COGL_PIXEL_FORMAT_YUV422;
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_R;
      components_out[2] = COGL_TEXTURE_COMPONENTS_R;
      break;
    case WL_SHM_FORMAT_YVU422:
      format = COGL_PIXEL_FORMAT_YVU422;
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_R;
      components_out[2] = COGL_TEXTURE_COMPONENTS_R;
      break;
    case WL_SHM_FORMAT_YUV444:
      format = COGL_PIXEL_FORMAT_YUV444;
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_R;
      components_out[2] = COGL_TEXTURE_COMPONENTS_R;
      break;
    case WL_SHM_FORMAT_YVU444:
      format = COGL_PIXEL_FORMAT_YVU444;
      components_out[0] = COGL_TEXTURE_COMPONENTS_R;
      components_out[1] = COGL_TEXTURE_COMPONENTS_R;
      components_out[2] = COGL_TEXTURE_COMPONENTS_R;
      break;

    default:
      g_warn_if_reached ();
      format = COGL_PIXEL_FORMAT_ARGB_8888;
      components_out[0] = COGL_TEXTURE_COMPONENTS_RGBA;
    }

  if (format_out)
    *format_out = format;
}

static gboolean
shm_buffer_attach (MetaWaylandBuffer      *buffer,
                   CoglMultiPlaneTexture **texture,
                   gboolean               *changed_texture,
                   GError                **error)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  struct wl_shm_buffer *shm_buffer;
  int stride, width, height;
  CoglPixelFormat format;
  CoglTextureComponents components[3];
  guint i, n_planes;
  uint8_t h_factors[3], v_factors[3], bpp[3];
  CoglPixelFormat subformats[3];
  gsize plane_offset = 0;
  guint8 *data;
  GPtrArray *planes;

  /* Query the necessary parameters */
  shm_buffer = wl_shm_buffer_get (buffer->resource);
  stride = wl_shm_buffer_get_stride (shm_buffer);
  width = wl_shm_buffer_get_width (shm_buffer);
  height = wl_shm_buffer_get_height (shm_buffer);
  shm_buffer_get_cogl_pixel_format (shm_buffer, &format, components);
  n_planes = cogl_pixel_format_get_n_planes (format);
  cogl_pixel_format_get_subsampling_factors (format, h_factors, v_factors);
  cogl_pixel_format_get_subformats (format, subformats);
  cogl_pixel_format_get_bytes_per_pixel_ (format, bpp);

  if (*texture &&
      cogl_multi_plane_texture_get_width (*texture) == width &&
      cogl_multi_plane_texture_get_height (*texture) == height &&
      /*XXX cogl_texture_get_components (*texture) == components && */
      cogl_multi_plane_texture_get_format (*texture) == format)
    {
      buffer->is_y_inverted = TRUE;
      *changed_texture = FALSE;
      return TRUE;
    }

  cogl_clear_object (texture);

  /* Safely access the data inside the buffer */
  wl_shm_buffer_begin_access (shm_buffer);
  data = wl_shm_buffer_get_data (shm_buffer);

  planes = g_ptr_array_new_full (n_planes, cogl_object_unref);
  for (i = 0; i < n_planes; i++)
    {
      int plane_stride;
      CoglBitmap *plane_bitmap;
      CoglTexture *plane_texture;

      /* Adjust the stride: map to the amount of pixels and calculate how many
       * bytes that takes in the current plane */
      plane_stride = (stride / bpp[0]) * bpp[i] / h_factors[i];

      /* Define the bitmap that of this plane */
      plane_bitmap = cogl_bitmap_new_for_data (cogl_context,
                                               width / h_factors[i],
                                               height / v_factors[i],
                                               subformats[i],
                                               plane_stride,
                                               data + plane_offset);
      g_assert (plane_bitmap);

      /* Create a texture out of it so we can upload it to the GPU */
      plane_texture = COGL_TEXTURE (cogl_texture_2d_new_from_bitmap (plane_bitmap));

      /* Separately set the components (necessary for e.g. XRGB, NV12) */
      cogl_texture_set_components (plane_texture, components[i]);

      if (!cogl_texture_allocate (plane_texture, error))
        {
          CoglTexture2DSliced *texture_sliced;

          cogl_clear_object (&plane_texture);

          /* If it didn't work due to an NPOT size, try again with an atlas texture */
          if (!g_error_matches (*error, COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_SIZE))
            {
              cogl_object_unref (plane_bitmap);
              goto failure;
            }

          g_clear_error (error);

          texture_sliced =
            cogl_texture_2d_sliced_new_from_bitmap (plane_bitmap,
                                                    COGL_TEXTURE_MAX_WASTE);
          plane_texture = COGL_TEXTURE (texture_sliced);

          cogl_texture_set_components (plane_texture, components[i]);

          if (!cogl_texture_allocate (plane_texture, error))
            {
              cogl_clear_object (&plane_texture);
              goto failure;
            }
        }

      g_ptr_array_add (planes, plane_texture);

      /* Calculate the next plane start in the buffer (consider subsampling) */
      plane_offset += plane_stride * (height / v_factors[i]);
    }

  *texture = cogl_multi_plane_texture_new (format,
                                          (CoglTexture **) g_ptr_array_free (planes, FALSE),
                                          n_planes);

  wl_shm_buffer_end_access (shm_buffer);

  *changed_texture = TRUE;
  buffer->is_y_inverted = TRUE;

  return TRUE;

failure:
  *texture = NULL;
  return FALSE;
}

static gboolean
egl_image_buffer_attach (MetaWaylandBuffer      *buffer,
                         CoglMultiPlaneTexture **texture,
                         gboolean               *changed_texture,
                         GError                **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  int format, width, height, y_inverted;
  CoglPixelFormat cogl_format;
  CoglTextureComponents components[3];
  guint i, n_planes;
  GPtrArray *planes;

  if (buffer->egl_image.texture)
    {
      *changed_texture = *texture != buffer->egl_image.texture;
      cogl_clear_object (texture);
      *texture = cogl_object_ref (buffer->egl_image.texture);
      return TRUE;
    }

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
      cogl_format = COGL_PIXEL_FORMAT_NV12;
      break;
    case EGL_TEXTURE_Y_U_V_WL:
      cogl_format = COGL_PIXEL_FORMAT_YUV444;
      break;
    default:
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unsupported buffer format %d", format);
      return FALSE;
    }

  n_planes = cogl_pixel_format_get_n_planes (cogl_format);
  cogl_pixel_format_get_cogl_components (cogl_format, components);
  planes = g_ptr_array_new_full (n_planes, cogl_object_unref);

  /* Each EGLImage is a plane in the final texture */
  for (i = 0; i < n_planes; i++)
    {
      EGLint egl_attribs[3];
      EGLImageKHR egl_img;
      CoglTexture2D *texture_2d;

      /* Specify that we want the i'th plane */
      egl_attribs[0] = EGL_WAYLAND_PLANE_WL;
      egl_attribs[1] = i;
      egl_attribs[2] = EGL_NONE;

      /* The WL_bind_wayland_display spec states that EGL_NO_CONTEXT is to be
       * used in conjunction with the EGL_WAYLAND_BUFFER_WL target. */
      egl_img = meta_egl_create_image (egl, egl_display, EGL_NO_CONTEXT,
                                       EGL_WAYLAND_BUFFER_WL, buffer->resource,
                                       egl_attribs,
                                       error);

      if (G_UNLIKELY (egl_img == EGL_NO_IMAGE_KHR))
        goto on_error;

      texture_2d = cogl_egl_texture_2d_new_from_image (cogl_context,
                                                    width, height,
                                                    cogl_format,
                                                    components[i],
                                                    egl_img,
                                                    error);

      meta_egl_destroy_image (egl, egl_display, egl_img, NULL);

      if (G_UNLIKELY (!texture_2d))
        goto on_error;

      g_ptr_array_add (planes, texture_2d);
    }


  buffer->egl_image.texture = cogl_multi_plane_texture_new (cogl_format,
                                             (CoglTexture **) g_ptr_array_free (planes, FALSE),
                                             n_planes);
  buffer->is_y_inverted = !!y_inverted;

  cogl_clear_object (texture);
  *texture = cogl_object_ref (buffer->egl_image.texture);
  *changed_texture = TRUE;

  return TRUE;

on_error:
  g_ptr_array_free (planes, TRUE);

  return FALSE;
}

#ifdef HAVE_WAYLAND_EGLSTREAM
static gboolean
egl_stream_buffer_attach (MetaWaylandBuffer      *buffer,
                          CoglMultiPlaneTexture **texture,
                          gboolean               *changed_texture,
                          GError                **error)
{
  MetaWaylandEglStream *stream = buffer->egl_stream.stream;

  g_assert (stream);

  if (!meta_wayland_egl_stream_attach (stream, error))
    return FALSE;

  *changed_texture = *texture != buffer->egl_stream.texture;
  cogl_clear_object (texture);
  *texture = cogl_object_ref (buffer->egl_stream.texture);

  return TRUE;
}
#endif /* HAVE_WAYLAND_EGLSTREAM */

/**
 * meta_wayland_buffer_attach:
 * @buffer: a pointer to a #MetaWaylandBuffer
 * @texture: (inout) (transfer full): a #CoglTexture representing the surface
 *                                    content
 * @error: return location for error or %NULL
 *
 * This function should be passed a pointer to the texture used to draw the
 * surface content. The texture will either be replaced by a new texture, or
 * stay the same, in which case, it may later be updated with new content when
 * processing damage. The new texture might be newly created, or it may be a
 * reference to an already existing one.
 *
 * If replaced, the old texture will have its reference count decreased by one,
 * potentially freeing it. When a new texture is set, the caller (i.e. the
 * surface) will be the owner of one reference count. It must free it, either
 * using g_object_unref() or have it updated again using
 * meta_wayland_buffer_attach(), which also might free it, as described above.
 */
gboolean
meta_wayland_buffer_attach (MetaWaylandBuffer      *buffer,
                            CoglMultiPlaneTexture **texture,
                            gboolean               *changed_texture,
                            GError                **error)
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
      return shm_buffer_attach (buffer, texture, changed_texture, error);
    case META_WAYLAND_BUFFER_TYPE_EGL_IMAGE:
      return egl_image_buffer_attach (buffer, texture, changed_texture, error);
#ifdef HAVE_WAYLAND_EGLSTREAM
    case META_WAYLAND_BUFFER_TYPE_EGL_STREAM:
      return egl_stream_buffer_attach (buffer, texture, changed_texture, error);
#endif
    case META_WAYLAND_BUFFER_TYPE_DMA_BUF:
      return meta_wayland_dma_buf_buffer_attach (buffer,
                                                 texture,
                                                 changed_texture,
                                                 error);
    case META_WAYLAND_BUFFER_TYPE_UNKNOWN:
      g_assert_not_reached ();
      return FALSE;
    }

  g_assert_not_reached ();
  return FALSE;
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
process_shm_buffer_damage (MetaWaylandBuffer      *buffer,
                           CoglMultiPlaneTexture  *texture,
                           cairo_region_t         *region,
                           GError                **error)
{
  struct wl_shm_buffer *shm_buffer;
  gint j, n_rectangles;
  gboolean set_texture_failed = FALSE;
  CoglPixelFormat format;
  CoglTextureComponents components[3];
  uint8_t h_factors[3], v_factors[3], bpp[3];
  CoglPixelFormat subformats[3];
  gsize plane_offset = 0;
  const uint8_t *data;
  int32_t stride, height;
  guint i, n_planes = cogl_multi_plane_texture_get_n_planes (texture);

  n_rectangles = cairo_region_num_rectangles (region);

  shm_buffer = wl_shm_buffer_get (buffer->resource);

  /* Get the data */
  wl_shm_buffer_begin_access (shm_buffer);
  data = wl_shm_buffer_get_data (shm_buffer);

  /* Query the necessary properties */
  stride = wl_shm_buffer_get_stride (shm_buffer);
  height = wl_shm_buffer_get_height (shm_buffer);
  shm_buffer_get_cogl_pixel_format (shm_buffer, &format, components);

  /* Fetch some properties from the pixel format */
  cogl_pixel_format_get_subformats (format, subformats);
  cogl_pixel_format_get_subsampling_factors (format, h_factors, v_factors);
  cogl_pixel_format_get_bytes_per_pixel_ (format, bpp);

  for (i = 0; i < n_planes; i++)
    {
      CoglTexture *plane;
      int plane_stride;

      plane = cogl_multi_plane_texture_get_plane (texture, i);
      plane_stride = (stride / bpp[0]) * bpp[i] / h_factors[i];

      for (j = 0; j < n_rectangles; j++)
        {
          cairo_rectangle_int_t rect;
          gsize rect_offset;

          cairo_region_get_rectangle (region, j, &rect);

          /* It's possible we get a faulty rectangle of size zero: ignore */
          if (rect.height == 0 || rect.width == 0)
            continue;

          rect_offset = plane_offset
              + rect.y * plane_stride / v_factors[i] /* Find the right row */
              + rect.x * bpp[i] / h_factors[i];      /* and the right column */

          if (!_cogl_texture_set_region (plane,
                                         rect.width / h_factors[i],
                                         rect.height / v_factors[i],
                                         subformats[i],
                                         plane_stride,
                                         data + rect_offset,
                                         rect.x, rect.y,
                                         0,
                                         error))
            {
              set_texture_failed = TRUE;
              goto out;
            }
        }

      /* Calculate the next plane start in the buffer (consider subsampling) */
      plane_offset += plane_stride * (height / v_factors[i]);
    }

out:
  wl_shm_buffer_end_access (shm_buffer);

  return !set_texture_failed;
}

void
meta_wayland_buffer_process_damage (MetaWaylandBuffer     *buffer,
                                    CoglMultiPlaneTexture *texture,
                                    cairo_region_t        *region)
{
  gboolean res = FALSE;
  GError *error = NULL;

  g_return_if_fail (buffer->resource);

  switch (buffer->type)
    {
    case META_WAYLAND_BUFFER_TYPE_SHM:
      res = process_shm_buffer_damage (buffer, texture, region, &error);
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

  g_clear_pointer (&buffer->egl_image.texture, cogl_object_unref);
#ifdef HAVE_WAYLAND_EGLSTREAM
  g_clear_pointer (&buffer->egl_stream.texture, cogl_object_unref);
  g_clear_object (&buffer->egl_stream.stream);
#endif
  g_clear_pointer (&buffer->dma_buf.texture, cogl_object_unref);
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
