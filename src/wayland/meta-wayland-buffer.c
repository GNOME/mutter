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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

/**
 * MetaWaylandBuffer
 *
 * A wrapper for wayland buffers
 *
 * #MetaWaylandBuffer is a general wrapper around wl_buffer, the basic way of
 * passing rendered data from Wayland clients to the compositor. Note that a
 * buffer can be backed by several types of memory, as specified by
 * #MetaWaylandBufferType.
 */

/**
 * MetaWaylandBufferType:
 * @META_WAYLAND_BUFFER_TYPE_UNKNOWN: Unknown type.
 * @META_WAYLAND_BUFFER_TYPE_SHM: wl_buffer backed by shared memory
 * @META_WAYLAND_BUFFER_TYPE_EGL_IMAGE: wl_buffer backed by an EGLImage
 * @META_WAYLAND_BUFFER_TYPE_EGL_STREAM: wl_buffer backed by an EGLStream (NVIDIA-specific)
 * @META_WAYLAND_BUFFER_TYPE_DMA_BUF: wl_buffer backed by a Linux DMA-BUF
 *
 * Specifies the backing memory for a #MetaWaylandBuffer. Depending on the type
 * of buffer, this will lead to different handling for the compositor.  For
 * example, a shared-memory buffer will still need to be uploaded to the GPU.
 */

#include "config.h"

#include "wayland/meta-wayland-buffer.h"

#include <drm_fourcc.h>

#include "backends/meta-backend-private.h"
#include "clutter/clutter.h"
#include "cogl/cogl-egl.h"
#include "meta/util.h"
#include "wayland/meta-wayland-dma-buf.h"
#include "wayland/meta-wayland-private.h"
#include "common/meta-cogl-drm-formats.h"
#include "compositor/meta-multi-texture-format-private.h"

#ifdef HAVE_NATIVE_BACKEND
#include "backends/native/meta-drm-buffer-gbm.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-onscreen-native.h"
#include "backends/native/meta-renderer-native.h"
#endif

#define META_WAYLAND_SHM_MAX_PLANES 4

enum
{
  RESOURCE_DESTROYED,

  LAST_SIGNAL
};

guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (MetaWaylandBuffer, meta_wayland_buffer, G_TYPE_OBJECT);

MetaFormatInfo supported_shm_formats[G_N_ELEMENTS (meta_format_info)];
size_t n_supported_shm_formats = 0;

static void
meta_wayland_buffer_destroy_handler (struct wl_listener *listener,
                                     void               *data)
{
  MetaWaylandBuffer *buffer =
    wl_container_of (listener, buffer, destroy_listener);

  buffer->resource = NULL;
  wl_list_remove (&buffer->destroy_listener.link);
  g_signal_emit (buffer, signals[RESOURCE_DESTROYED], 0);
  g_object_unref (buffer);
}

MetaWaylandBuffer *
meta_wayland_buffer_from_resource (MetaWaylandCompositor *compositor,
                                   struct wl_resource    *resource)
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
      buffer->compositor = compositor;
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
#ifdef HAVE_WAYLAND_EGLSTREAM
  MetaWaylandEglStream *stream;
#endif
  MetaWaylandDmaBufBuffer *dma_buf;
  MetaWaylandSinglePixelBuffer *single_pixel_buffer;

  if (wl_shm_buffer_get (buffer->resource) != NULL)
    {
      buffer->type = META_WAYLAND_BUFFER_TYPE_SHM;
      return TRUE;
    }

#ifdef HAVE_WAYLAND_EGLSTREAM
  stream = meta_wayland_egl_stream_new (buffer, NULL);
  if (stream)
    {
      CoglTexture *texture;

      texture = meta_wayland_egl_stream_create_texture (stream, NULL);
      if (!texture)
        return FALSE;

      buffer->egl_stream.stream = stream;
      buffer->type = META_WAYLAND_BUFFER_TYPE_EGL_STREAM;
      buffer->egl_stream.texture = meta_multi_texture_new_simple (texture);
      buffer->is_y_inverted = meta_wayland_egl_stream_is_y_inverted (stream);

      return TRUE;
    }
#endif /* HAVE_WAYLAND_EGLSTREAM */

  if (meta_wayland_compositor_is_egl_display_bound (buffer->compositor))
    {
      MetaContext *context =
        meta_wayland_compositor_get_context (buffer->compositor);
      MetaBackend *backend = meta_context_get_backend (context);
      EGLint format;
      MetaEgl *egl = meta_backend_get_egl (backend);
      ClutterBackend *clutter_backend =
        meta_backend_get_clutter_backend (backend);
      CoglContext *cogl_context =
        clutter_backend_get_cogl_context (clutter_backend);
      EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);

      if (meta_egl_query_wayland_buffer (egl, egl_display, buffer->resource,
                                         EGL_TEXTURE_FORMAT, &format,
                                         NULL))
        {
          buffer->type = META_WAYLAND_BUFFER_TYPE_EGL_IMAGE;
          buffer->dma_buf.dma_buf =
            meta_wayland_dma_buf_fds_for_wayland_buffer (buffer);
          return TRUE;
        }
    }

  dma_buf = meta_wayland_dma_buf_from_buffer (buffer);
  if (dma_buf)
    {
      buffer->dma_buf.dma_buf = dma_buf;
      buffer->type = META_WAYLAND_BUFFER_TYPE_DMA_BUF;
      return TRUE;
    }

  single_pixel_buffer = meta_wayland_single_pixel_buffer_from_buffer (buffer);
  if (single_pixel_buffer)
    {
      buffer->single_pixel.single_pixel_buffer = single_pixel_buffer;
      buffer->type = META_WAYLAND_BUFFER_TYPE_SINGLE_PIXEL;
      return TRUE;
    }

  return FALSE;
}

static uint32_t
shm_to_drm_format (enum wl_shm_format format)
{
  if (format == WL_SHM_FORMAT_ARGB8888)
    return DRM_FORMAT_ARGB8888;
  if (format == WL_SHM_FORMAT_XRGB8888)
    return DRM_FORMAT_XRGB8888;

  /* all other wayland shm formats are the same as the drm format */
  return format;
}

static const char *
shm_format_to_string (MetaDrmFormatBuf   *format_buf,
                      enum wl_shm_format  shm_format)
{
  uint32_t drm_format;

  drm_format = shm_to_drm_format (shm_format);
  return meta_drm_format_to_string (format_buf, drm_format);
}

static const MetaFormatInfo *
get_supported_shm_format_info (uint32_t shm_format)
{
  size_t i;

  for (i = 0; i < n_supported_shm_formats; i++)
    {
      uint32_t drm_format;

      drm_format = shm_to_drm_format (shm_format);
      if (supported_shm_formats[i].drm_format == drm_format)
        return &supported_shm_formats[i];
    }

  return NULL;
}

static CoglTexture *
texture_from_bitmap (CoglBitmap  *bitmap,
                     GError     **error)
{
  g_autoptr (CoglTexture) tex = NULL;
  g_autoptr (CoglTexture) tex_sliced = NULL;

  tex = cogl_texture_2d_new_from_bitmap (bitmap);

  if (cogl_texture_allocate (tex, error))
    return g_steal_pointer (&tex);

  if (!g_error_matches (*error, COGL_TEXTURE_ERROR, COGL_TEXTURE_ERROR_SIZE))
    return NULL;

  g_clear_error (error);
  tex_sliced = cogl_texture_2d_sliced_new_from_bitmap (bitmap,
                                                       COGL_TEXTURE_MAX_WASTE);

  if (cogl_texture_allocate (tex_sliced, error))
    return g_steal_pointer (&tex_sliced);

  return NULL;
}

static size_t
get_logical_elements (const MetaFormatInfo *format_info,
                      size_t                stride)
{
  const MetaMultiTextureFormatInfo *mt_format_info =
    meta_multi_texture_format_get_info (format_info->multi_texture_format);
  CoglPixelFormat subformat = mt_format_info->subformats[0];

  if (subformat == COGL_PIXEL_FORMAT_ANY)
    subformat = format_info->cogl_format;

  return stride / cogl_pixel_format_get_bytes_per_pixel (subformat, 0);
}

static void
get_offset_and_stride (const MetaFormatInfo *format_info,
                       int                   stride,
                       int                   height,
                       int                   shm_offset[3],
                       int                   shm_stride[3])
{
  const MetaMultiTextureFormatInfo *mt_format_info =
    meta_multi_texture_format_get_info (format_info->multi_texture_format);
  int logical_elements;
  size_t n_planes;
  size_t i;

  shm_offset[0] = 0;
  shm_stride[0] = stride;

  logical_elements = get_logical_elements (format_info, stride);
  n_planes = mt_format_info->n_planes;

  for (i = 1; i < n_planes; i++)
    {
      CoglPixelFormat subformat = mt_format_info->subformats[i];
      int horizontal_factor = mt_format_info->hsub[i];
      int bpp;

      if (subformat == COGL_PIXEL_FORMAT_ANY)
        subformat = format_info->cogl_format;

      bpp = cogl_pixel_format_get_bytes_per_pixel (subformat, 0);
      shm_stride[i] = logical_elements / horizontal_factor * bpp;
    }

  for (i = 1; i < n_planes; i++)
    {
      int vertical_factor = mt_format_info->vsub[i - 1];

      shm_offset[i] = shm_offset[i - 1] +
                      (shm_stride[i - 1] * (height / vertical_factor));
    }
}

static MetaMultiTexture *
multi_texture_from_shm (CoglContext           *cogl_context,
                        const MetaFormatInfo  *format_info,
                        int                    width,
                        int                    height,
                        int                    stride,
                        uint8_t               *data,
                        GError               **error)
{
  const MetaMultiTextureFormatInfo *mt_format_info;
  MetaMultiTextureFormat multi_format;
  g_autoptr (GPtrArray) planes = NULL;
  CoglTexture **textures;
  int shm_offset[3] = { 0 };
  int shm_stride[3] = { 0 };
  int n_planes;
  int i;

  multi_format = format_info->multi_texture_format;
  mt_format_info = meta_multi_texture_format_get_info (multi_format);
  n_planes = mt_format_info->n_planes;
  planes = g_ptr_array_new_full (n_planes, g_object_unref);

  get_offset_and_stride (format_info, stride, height, shm_offset, shm_stride);

  for (i = 0; i < n_planes; i++)
    {
      CoglTexture *cogl_texture;
      CoglBitmap *bitmap;
      int plane_index = mt_format_info->plane_indices[i];
      CoglPixelFormat subformat = mt_format_info->subformats[i];
      int horizontal_factor = mt_format_info->hsub[i];
      int vertical_factor = mt_format_info->vsub[i];

      if (subformat == COGL_PIXEL_FORMAT_ANY)
        subformat = format_info->cogl_format;

      bitmap = cogl_bitmap_new_for_data (cogl_context,
                                         width / horizontal_factor,
                                         height / vertical_factor,
                                         subformat,
                                         shm_stride[plane_index],
                                         data + shm_offset[plane_index]);
      cogl_texture = texture_from_bitmap (bitmap, error);
      g_clear_object (&bitmap);

      if (!cogl_texture)
        return NULL;

      g_ptr_array_add (planes, cogl_texture);
    }

  textures = (CoglTexture**) g_ptr_array_free (g_steal_pointer (&planes),
                                               FALSE);

  return meta_multi_texture_new (multi_format,
                                 textures,
                                 n_planes);
}

static gboolean
shm_buffer_attach (MetaWaylandBuffer  *buffer,
                   MetaMultiTexture  **texture,
                   GError            **error)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (buffer->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  struct wl_shm_buffer *shm_buffer;
  int stride, width, height;
  MetaMultiTextureFormat multi_format;
  CoglPixelFormat cogl_format;
  MetaDrmFormatBuf format_buf;
  uint32_t shm_format;
  const MetaFormatInfo *format_info;

  shm_buffer = wl_shm_buffer_get (buffer->resource);
  stride = wl_shm_buffer_get_stride (shm_buffer);
  width = wl_shm_buffer_get_width (shm_buffer);
  height = wl_shm_buffer_get_height (shm_buffer);
  shm_format = wl_shm_buffer_get_format (shm_buffer);

  format_info = get_supported_shm_format_info (shm_format);
  if (!format_info)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid shm pixel format");
      return FALSE;
    }

  cogl_format = format_info->cogl_format;
  multi_format = format_info->multi_texture_format;

  meta_topic (META_DEBUG_WAYLAND,
              "[wl-shm] wl_buffer@%u wl_shm_format %s "
              "-> MetaMultiTextureFormat %s / CoglPixelFormat %s",
              wl_resource_get_id (meta_wayland_buffer_get_resource (buffer)),
              shm_format_to_string (&format_buf, shm_format),
              meta_multi_texture_format_to_string (multi_format),
              cogl_pixel_format_to_string (cogl_format));

  if (*texture &&
      meta_multi_texture_get_width (*texture) == width &&
      meta_multi_texture_get_height (*texture) == height &&
      meta_multi_texture_get_format (*texture) == multi_format)
    {
      CoglTexture *cogl_texture = meta_multi_texture_get_plane (*texture, 0);

      if (!meta_multi_texture_is_simple (*texture) ||
          _cogl_texture_get_format (cogl_texture) == cogl_format)
        {
          buffer->is_y_inverted = TRUE;
          return TRUE;
        }
    }

  g_clear_object (texture);

  wl_shm_buffer_begin_access (shm_buffer);
  *texture = multi_texture_from_shm (cogl_context,
                                     format_info,
                                     width, height, stride,
                                     wl_shm_buffer_get_data (shm_buffer),
                                     error);
  wl_shm_buffer_end_access (shm_buffer);

  if (*texture == NULL)
    return FALSE;

  buffer->is_y_inverted = TRUE;
  return TRUE;
}

static gboolean
egl_image_buffer_attach (MetaWaylandBuffer  *buffer,
                         MetaMultiTexture  **texture,
                         GError            **error)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (buffer->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  EGLDisplay egl_display = cogl_egl_context_get_egl_display (cogl_context);
  int format, width, height, y_inverted;
  CoglPixelFormat cogl_format;
  EGLImageKHR egl_image;
  CoglEglImageFlags flags;
  CoglTexture *texture_2d;

  if (buffer->egl_image.texture)
    {
      g_clear_object (texture);
      *texture = g_object_ref (buffer->egl_image.texture);
      return TRUE;
    }

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

  switch (format)
    {
    case EGL_TEXTURE_RGB:
      cogl_format = COGL_PIXEL_FORMAT_RGB_888;
      break;
    case EGL_TEXTURE_RGBA:
      cogl_format = COGL_PIXEL_FORMAT_RGBA_8888_PRE;
      break;
    default:
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Unsupported buffer format %d", format);
      return FALSE;
    }

  /* The WL_bind_wayland_display spec states that EGL_NO_CONTEXT is to be used
   * in conjunction with the EGL_WAYLAND_BUFFER_WL target. */
  egl_image = meta_egl_create_image (egl, egl_display, EGL_NO_CONTEXT,
                                     EGL_WAYLAND_BUFFER_WL, buffer->resource,
                                     NULL,
                                     error);
  if (egl_image == EGL_NO_IMAGE_KHR)
    return FALSE;

  flags = COGL_EGL_IMAGE_FLAG_NONE;
  texture_2d = cogl_egl_texture_2d_new_from_image (cogl_context,
                                                   width, height,
                                                   cogl_format,
                                                   egl_image,
                                                   flags,
                                                   error);

  meta_egl_destroy_image (egl, egl_display, egl_image, NULL);

  if (!texture_2d)
    return FALSE;

  buffer->egl_image.texture = meta_multi_texture_new_simple (texture_2d);
  buffer->is_y_inverted = !!y_inverted;

  g_clear_object (texture);
  *texture = g_object_ref (buffer->egl_image.texture);

  return TRUE;
}

#ifdef HAVE_WAYLAND_EGLSTREAM
static gboolean
egl_stream_buffer_attach (MetaWaylandBuffer  *buffer,
                          MetaMultiTexture  **texture,
                          GError            **error)
{
  MetaWaylandEglStream *stream = buffer->egl_stream.stream;

  g_assert (stream);

  if (!meta_wayland_egl_stream_attach (stream, error))
    return FALSE;

  g_clear_object (texture);
  *texture = g_object_ref (buffer->egl_stream.texture);

  return TRUE;
}
#endif /* HAVE_WAYLAND_EGLSTREAM */

static void
on_onscreen_destroyed (gpointer  user_data,
                       GObject  *where_the_onscreen_was)
{
  MetaWaylandBuffer *buffer = user_data;

  g_hash_table_remove (buffer->tainted_scanout_onscreens,
                       where_the_onscreen_was);
}

static void
on_scanout_failed (CoglScanout       *scanout,
                   CoglOnscreen      *onscreen,
                   MetaWaylandBuffer *buffer)
{
  if (!buffer->tainted_scanout_onscreens)
    buffer->tainted_scanout_onscreens = g_hash_table_new (NULL, NULL);

  g_hash_table_add (buffer->tainted_scanout_onscreens, onscreen);
  g_object_weak_ref (G_OBJECT (onscreen), on_onscreen_destroyed, buffer);
}

static void
clear_tainted_scanout_onscreens (MetaWaylandBuffer *buffer)
{
  GHashTableIter iter;
  CoglOnscreen *onscreen;

  if (!buffer->tainted_scanout_onscreens)
    return;

  g_hash_table_iter_init (&iter, buffer->tainted_scanout_onscreens);
  while (g_hash_table_iter_next (&iter, (gpointer *) &onscreen, NULL))
    g_object_weak_unref (G_OBJECT (onscreen), on_onscreen_destroyed, buffer);
  g_hash_table_remove_all (buffer->tainted_scanout_onscreens);
}

/**
 * meta_wayland_buffer_attach:
 * @buffer: a pointer to a #MetaWaylandBuffer
 * @texture: (inout) (transfer full): a #MetaMultiTexture representing the
 *                                    surface content
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
meta_wayland_buffer_attach (MetaWaylandBuffer  *buffer,
                            MetaMultiTexture  **texture,
                            GError            **error)
{
  COGL_TRACE_BEGIN_SCOPED (MetaWaylandBufferAttach, "Meta::WaylandBuffer::attach()");

  clear_tainted_scanout_onscreens (buffer);

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
      return shm_buffer_attach (buffer, texture, error);
    case META_WAYLAND_BUFFER_TYPE_EGL_IMAGE:
      return egl_image_buffer_attach (buffer, texture, error);
#ifdef HAVE_WAYLAND_EGLSTREAM
    case META_WAYLAND_BUFFER_TYPE_EGL_STREAM:
      return egl_stream_buffer_attach (buffer, texture, error);
#endif
    case META_WAYLAND_BUFFER_TYPE_DMA_BUF:
      return meta_wayland_dma_buf_buffer_attach (buffer,
                                                 texture,
                                                 error);
    case META_WAYLAND_BUFFER_TYPE_SINGLE_PIXEL:
      return meta_wayland_single_pixel_buffer_attach (buffer,
                                                      texture,
                                                      error);
    case META_WAYLAND_BUFFER_TYPE_UNKNOWN:
      g_assert_not_reached ();
      return FALSE;
    }

  g_assert_not_reached ();
  return FALSE;
}

/**
 * meta_wayland_buffer_create_snippet:
 * @buffer: A #MetaWaylandBuffer object
 *
 * If needed, this method creates a #CoglSnippet to make sure the buffer can be
 * dealt with appropriately in a #CoglPipeline that renders it.
 *
 * Returns: (transfer full) (nullable): A new #CoglSnippet, or %NULL.
 */
CoglSnippet *
meta_wayland_buffer_create_snippet (MetaWaylandBuffer *buffer)
{
#ifdef HAVE_WAYLAND_EGLSTREAM
  if (!buffer->egl_stream.stream)
    return NULL;

  return meta_wayland_egl_stream_create_snippet (buffer->egl_stream.stream);
#else
  return NULL;
#endif /* HAVE_WAYLAND_EGLSTREAM */
}

void
meta_wayland_buffer_inc_use_count (MetaWaylandBuffer *buffer)
{
  g_warn_if_fail (buffer->resource);

  buffer->use_count++;
}

void
meta_wayland_buffer_dec_use_count (MetaWaylandBuffer *buffer)
{
  g_return_if_fail (buffer->use_count > 0);

  buffer->use_count--;

  if (buffer->use_count == 0 && buffer->resource)
    wl_buffer_send_release (buffer->resource);
}

gboolean
meta_wayland_buffer_is_y_inverted (MetaWaylandBuffer *buffer)
{
  return buffer->is_y_inverted;
}

static gboolean
process_shm_buffer_damage (MetaWaylandBuffer *buffer,
                           MetaMultiTexture  *texture,
                           MtkRegion         *region,
                           GError           **error)
{
  const MetaFormatInfo *format_info;
  MetaMultiTextureFormat multi_format;
  const MetaMultiTextureFormatInfo *mt_format_info;
  struct wl_shm_buffer *shm_buffer;
  int shm_offset[3] = { 0 };
  int shm_stride[3] = { 0 };
  const uint8_t *data;
  int stride;
  int height;
  uint32_t shm_format;
  int i, n_rectangles, n_planes;

  n_rectangles = mtk_region_num_rectangles (region);

  shm_buffer = wl_shm_buffer_get (buffer->resource);
  stride = wl_shm_buffer_get_stride (shm_buffer);
  height = wl_shm_buffer_get_height (shm_buffer);
  shm_format = wl_shm_buffer_get_format (shm_buffer);

  format_info = get_supported_shm_format_info (shm_format);
  multi_format = format_info->multi_texture_format;
  mt_format_info = meta_multi_texture_format_get_info (multi_format);
  n_planes = mt_format_info->n_planes;

  get_offset_and_stride (format_info, stride, height, shm_offset, shm_stride);

  wl_shm_buffer_begin_access (shm_buffer);
  data = wl_shm_buffer_get_data (shm_buffer);

  for (i = 0; i < n_planes; i++)
    {
      CoglTexture *cogl_texture;
      int plane_index = mt_format_info->plane_indices[i];
      int horizontal_factor = mt_format_info->hsub[i];
      int vertical_factor = mt_format_info->vsub[i];
      CoglPixelFormat subformat;
      int bpp;
      const uint8_t *plane_data;
      size_t plane_stride;
      int j;

      plane_data = data + shm_offset[plane_index];
      plane_stride = shm_stride[plane_index];

      cogl_texture = meta_multi_texture_get_plane (texture, i);
      subformat = _cogl_texture_get_format (cogl_texture);
      bpp = cogl_pixel_format_get_bytes_per_pixel (subformat, 0);

      for (j = 0; j < n_rectangles; j++)
        {
          MtkRectangle rect;
          const uint8_t *rect_data;

          rect = mtk_region_get_rectangle (region, j);
          rect_data = plane_data + (rect.x * bpp / horizontal_factor) +
                      (rect.y * plane_stride);

          if (!_cogl_texture_set_region (cogl_texture,
                                         rect.width / horizontal_factor,
                                         rect.height / vertical_factor,
                                         subformat,
                                         plane_stride,
                                         rect_data,
                                         rect.x, rect.y,
                                         0,
                                         error))
            goto fail;
        }
    }

  wl_shm_buffer_end_access (shm_buffer);
  return TRUE;

fail:
  wl_shm_buffer_end_access (shm_buffer);
  return FALSE;
}

void
meta_wayland_buffer_process_damage (MetaWaylandBuffer *buffer,
                                    MetaMultiTexture  *texture,
                                    MtkRegion         *region)
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
    case META_WAYLAND_BUFFER_TYPE_SINGLE_PIXEL:
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

static CoglScanout *
try_acquire_egl_image_scanout (MetaWaylandBuffer *buffer,
                               CoglOnscreen      *onscreen)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaContext *context =
    meta_wayland_compositor_get_context (buffer->compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaRendererNative *renderer_native = META_RENDERER_NATIVE (renderer);
  MetaGpuKms *gpu_kms;
  MetaDeviceFile *device_file;
  struct gbm_device *gbm_device;
  struct gbm_bo *gbm_bo;
  MetaDrmBufferFlags flags;
  g_autoptr (MetaDrmBufferGbm) fb = NULL;
  g_autoptr (CoglScanout) scanout = NULL;
  g_autoptr (GError) error = NULL;

  gpu_kms = meta_renderer_native_get_primary_gpu (renderer_native);
  device_file = meta_renderer_native_get_primary_device_file (renderer_native);
  gbm_device = meta_gbm_device_from_gpu (gpu_kms);

  gbm_bo = gbm_bo_import (gbm_device,
                          GBM_BO_IMPORT_WL_BUFFER, buffer->resource,
                          GBM_BO_USE_SCANOUT);
  if (!gbm_bo)
    return NULL;

  flags = META_DRM_BUFFER_FLAG_NONE;
  if (gbm_bo_get_modifier (gbm_bo) == DRM_FORMAT_MOD_INVALID)
    flags |= META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS;

  fb = meta_drm_buffer_gbm_new_take (device_file, gbm_bo, flags, &error);
  if (!fb)
    {
      g_debug ("Failed to create scanout buffer: %s", error->message);
      gbm_bo_destroy (gbm_bo);
      return NULL;
    }

  scanout = cogl_scanout_new (COGL_SCANOUT_BUFFER (g_steal_pointer (&fb)));
  if (!meta_onscreen_native_is_buffer_scanout_compatible (onscreen, scanout))
    return NULL;

  return g_steal_pointer (&scanout);
#else
  return NULL;
#endif
}

static void
scanout_destroyed (gpointer  data,
                   GObject  *where_the_object_was)
{
  MetaWaylandBuffer *buffer = data;

  meta_wayland_buffer_dec_use_count (buffer);
  g_object_unref (buffer);
}

CoglScanout *
meta_wayland_buffer_try_acquire_scanout (MetaWaylandBuffer     *buffer,
                                         CoglOnscreen          *onscreen,
                                         const graphene_rect_t *src_rect,
                                         const MtkRectangle    *dst_rect)
{
  CoglScanout *scanout = NULL;

  COGL_TRACE_BEGIN_SCOPED (MetaWaylandBufferTryScanout,
                           "Meta::WaylandBuffer::try_acquire_scanout()");

  if (buffer->tainted_scanout_onscreens &&
      g_hash_table_lookup (buffer->tainted_scanout_onscreens, onscreen))
    {
      meta_topic (META_DEBUG_RENDER, "Buffer scanout capability tainted");
      return NULL;
    }

  switch (buffer->type)
    {
    case META_WAYLAND_BUFFER_TYPE_SHM:
    case META_WAYLAND_BUFFER_TYPE_SINGLE_PIXEL:
#ifdef HAVE_WAYLAND_EGLSTREAM
    case META_WAYLAND_BUFFER_TYPE_EGL_STREAM:
#endif
      meta_topic (META_DEBUG_RENDER,
                  "Buffer type not scanout compatible");
      return NULL;
    case META_WAYLAND_BUFFER_TYPE_EGL_IMAGE:
      if (src_rect || dst_rect)
        {
          meta_topic (META_DEBUG_RENDER,
                      "Buffer type does not support scaling operations");
          return NULL;
        }
      scanout = try_acquire_egl_image_scanout (buffer, onscreen);
      break;
    case META_WAYLAND_BUFFER_TYPE_DMA_BUF:
      {
        scanout = meta_wayland_dma_buf_try_acquire_scanout (buffer,
                                                            onscreen,
                                                            src_rect,
                                                            dst_rect);
        break;
      }
    case META_WAYLAND_BUFFER_TYPE_UNKNOWN:
      g_warn_if_reached ();
      return NULL;
    }

  if (!scanout)
    return NULL;

  g_signal_connect (scanout, "scanout-failed",
                    G_CALLBACK (on_scanout_failed), buffer);

  g_object_ref (buffer);
  meta_wayland_buffer_inc_use_count (buffer);
  g_object_weak_ref (G_OBJECT (scanout), scanout_destroyed, buffer);

  return scanout;
}

static void
meta_wayland_buffer_finalize (GObject *object)
{
  MetaWaylandBuffer *buffer = META_WAYLAND_BUFFER (object);

  g_warn_if_fail (buffer->use_count == 0);

  clear_tainted_scanout_onscreens (buffer);
  g_clear_pointer (&buffer->tainted_scanout_onscreens, g_hash_table_unref);

  g_clear_object (&buffer->egl_image.texture);
#ifdef HAVE_WAYLAND_EGLSTREAM
  g_clear_object (&buffer->egl_stream.texture);
  g_clear_object (&buffer->egl_stream.stream);
#endif
  g_clear_object (&buffer->dma_buf.texture);
  g_clear_object (&buffer->dma_buf.dma_buf);
  g_clear_pointer (&buffer->single_pixel.single_pixel_buffer,
                   meta_wayland_single_pixel_buffer_free);
  g_clear_object (&buffer->single_pixel.texture);

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

  /**
   * MetaWaylandBuffer::resource-destroyed:
   *
   * Called when the underlying wl_resource was destroyed.
   */
  signals[RESOURCE_DESTROYED] = g_signal_new ("resource-destroyed",
                                              G_TYPE_FROM_CLASS (object_class),
                                              G_SIGNAL_RUN_LAST,
                                              0,
                                              NULL, NULL, NULL,
                                              G_TYPE_NONE, 0);
}

static gboolean
context_supports_format (CoglContext         *cogl_context,
                         const MetaFormatInfo *format_info)
{
  const MetaMultiTextureFormatInfo *mt_format_info;
  size_t i;

  if (format_info->multi_texture_format == META_MULTI_TEXTURE_FORMAT_INVALID)
    return FALSE;

  if (format_info->multi_texture_format == META_MULTI_TEXTURE_FORMAT_SIMPLE)
    {
      return cogl_context_format_supports_upload (cogl_context,
                                                  format_info->cogl_format);
    }

  mt_format_info =
    meta_multi_texture_format_get_info (format_info->multi_texture_format);

  for (i = 0; i < mt_format_info->n_planes; i++)
    {
      if (!cogl_context_format_supports_upload (cogl_context,
                                                mt_format_info->subformats[i]))
        return FALSE;
    }

  return TRUE;
}

void
meta_wayland_init_shm (MetaWaylandCompositor *compositor)
{
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  int i;

  static const enum wl_shm_format possible_formats[] = {
    WL_SHM_FORMAT_ARGB8888,
    WL_SHM_FORMAT_XRGB8888,
    WL_SHM_FORMAT_ABGR8888,
    WL_SHM_FORMAT_XBGR8888,
    WL_SHM_FORMAT_RGB565,
    WL_SHM_FORMAT_ARGB2101010,
    WL_SHM_FORMAT_XRGB2101010,
    WL_SHM_FORMAT_ABGR2101010,
    WL_SHM_FORMAT_XBGR2101010,
    WL_SHM_FORMAT_ARGB16161616F,
    WL_SHM_FORMAT_XRGB16161616F,
    WL_SHM_FORMAT_ABGR16161616F,
    WL_SHM_FORMAT_XBGR16161616F,
    WL_SHM_FORMAT_YUYV,
    WL_SHM_FORMAT_NV12,
    WL_SHM_FORMAT_P010,
    WL_SHM_FORMAT_YUV420,
  };

  wl_display_init_shm (compositor->wayland_display);

  n_supported_shm_formats = 0;

  for (i = 0; i < G_N_ELEMENTS (possible_formats); i++)
    {
      const MetaFormatInfo *format_info;
      uint32_t drm_format;

      drm_format = shm_to_drm_format (possible_formats[i]);
      format_info = meta_format_info_from_drm_format (drm_format);
      g_assert (format_info);

      if (!context_supports_format (cogl_context, format_info))
        continue;

      supported_shm_formats[n_supported_shm_formats++] = *format_info;

      if (possible_formats[i] != WL_SHM_FORMAT_ARGB8888 &&
          possible_formats[i] != WL_SHM_FORMAT_XRGB8888)
        {
          wl_display_add_shm_format (compositor->wayland_display,
                                     possible_formats[i]);
        }
    }
}
