/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 *
 * Authors:
 *  Neil Roberts   <neil@linux.intel.com>
 */

#include "cogl-config.h"

#include "cogl-private.h"
#include "cogl-util.h"
#include "cogl-texture-private.h"
#include "cogl-texture-2d-private.h"
#include "cogl-texture-driver.h"
#include "cogl-context-private.h"
#include "cogl-object-private.h"
#include "cogl-journal-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-error-private.h"
#include "cogl-gtype-private.h"
#include "driver/gl/cogl-texture-2d-gl-private.h"
#include "driver/gl/cogl-pipeline-opengl-private.h"
#ifdef COGL_HAS_EGL_SUPPORT
#include "winsys/cogl-winsys-egl-private.h"
#endif

#include <string.h>
#include <math.h>

#ifdef COGL_HAS_WAYLAND_EGL_SERVER_SUPPORT
#include "cogl-wayland-server.h"
#endif

static void _cogl_texture_2d_free (CoglTexture2D *tex_2d);

COGL_TEXTURE_DEFINE (Texture2D, texture_2d);
COGL_GTYPE_DEFINE_CLASS (Texture2D, texture_2d,
                         COGL_GTYPE_IMPLEMENT_INTERFACE (texture));

static const CoglTextureVtable cogl_texture_2d_vtable;

typedef struct _CoglTexture2DManualRepeatData
{
  CoglTexture2D *tex_2d;
  CoglMetaTextureCallback callback;
  void *user_data;
} CoglTexture2DManualRepeatData;

static void
_cogl_texture_2d_free (CoglTexture2D *tex_2d)
{
  CoglContext *ctx = COGL_TEXTURE (tex_2d)->context;

  ctx->driver_vtable->texture_2d_free (tex_2d);

  /* Chain up */
  _cogl_texture_free (COGL_TEXTURE (tex_2d));
}

void
_cogl_texture_2d_set_auto_mipmap (CoglTexture *tex,
                                  CoglBool value)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  tex_2d->auto_mipmap = value;
}

CoglTexture2D *
_cogl_texture_2d_create_base (CoglContext *ctx,
                              int width,
                              int height,
                              CoglPixelFormat internal_format,
                              CoglTextureLoader *loader)
{
  CoglTexture2D *tex_2d = g_new (CoglTexture2D, 1);
  CoglTexture *tex = COGL_TEXTURE (tex_2d);

  _cogl_texture_init (tex, ctx, width, height, internal_format, loader,
                      &cogl_texture_2d_vtable);

  tex_2d->mipmaps_dirty = TRUE;
  tex_2d->auto_mipmap = TRUE;

  tex_2d->gl_target = GL_TEXTURE_2D;

  tex_2d->is_foreign = FALSE;

  ctx->driver_vtable->texture_2d_init (tex_2d);

  return _cogl_texture_2d_object_new (tex_2d);
}

CoglTexture2D *
cogl_texture_2d_new_with_size (CoglContext *ctx,
                               int width,
                               int height)
{
  CoglTextureLoader *loader;

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_SIZED;
  loader->src.sized.width = width;
  loader->src.sized.height = height;

  return _cogl_texture_2d_create_base (ctx, width, height,
                                       COGL_PIXEL_FORMAT_RGBA_8888_PRE, loader);
}

static CoglBool
_cogl_texture_2d_allocate (CoglTexture *tex,
                           CoglError **error)
{
  CoglContext *ctx = tex->context;

  return ctx->driver_vtable->texture_2d_allocate (tex, error);
}

CoglTexture2D *
_cogl_texture_2d_new_from_bitmap (CoglBitmap *bmp,
                                  CoglBool can_convert_in_place)
{
  CoglTextureLoader *loader;

  _COGL_RETURN_VAL_IF_FAIL (bmp != NULL, NULL);

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_BITMAP;
  loader->src.bitmap.bitmap = cogl_object_ref (bmp);
  loader->src.bitmap.can_convert_in_place = can_convert_in_place;

  return  _cogl_texture_2d_create_base (_cogl_bitmap_get_context (bmp),
                                        cogl_bitmap_get_width (bmp),
                                        cogl_bitmap_get_height (bmp),
                                        cogl_bitmap_get_format (bmp),
                                        loader);
}

CoglTexture2D *
cogl_texture_2d_new_from_bitmap (CoglBitmap *bmp)
{
  return _cogl_texture_2d_new_from_bitmap (bmp,
                                           FALSE); /* can't convert in place */
}

CoglTexture2D *
cogl_texture_2d_new_from_file (CoglContext *ctx,
                               const char *filename,
                               CoglError **error)
{
  CoglBitmap *bmp;
  CoglTexture2D *tex_2d = NULL;

  _COGL_RETURN_VAL_IF_FAIL (error == NULL || *error == NULL, NULL);

  bmp = _cogl_bitmap_from_file (ctx, filename, error);
  if (bmp == NULL)
    return NULL;

  tex_2d = _cogl_texture_2d_new_from_bitmap (bmp,
                                             TRUE); /* can convert in-place */

  cogl_object_unref (bmp);

  return tex_2d;
}

CoglTexture2D *
cogl_texture_2d_new_from_data (CoglContext *ctx,
                               int width,
                               int height,
                               CoglPixelFormat format,
                               int rowstride,
                               const uint8_t *data,
                               CoglError **error)
{
  CoglBitmap *bmp;
  CoglTexture2D *tex_2d;

  _COGL_RETURN_VAL_IF_FAIL (format != COGL_PIXEL_FORMAT_ANY, NULL);
  _COGL_RETURN_VAL_IF_FAIL (data != NULL, NULL);

  /* Rowstride from width if not given */
  if (rowstride == 0)
    rowstride = width * _cogl_pixel_format_get_bytes_per_pixel (format);

  /* Wrap the data into a bitmap */
  bmp = cogl_bitmap_new_for_data (ctx,
                                  width, height,
                                  format,
                                  rowstride,
                                  (uint8_t *) data);

  tex_2d = cogl_texture_2d_new_from_bitmap (bmp);

  cogl_object_unref (bmp);

  if (tex_2d &&
      !cogl_texture_allocate (COGL_TEXTURE (tex_2d), error))
    {
      cogl_object_unref (tex_2d);
      return NULL;
    }

  return tex_2d;
}

#if defined (COGL_HAS_EGL_SUPPORT) && defined (EGL_KHR_image_base)
/* NB: The reason we require the width, height and format to be passed
 * even though they may seem redundant is because GLES 1/2 don't
 * provide a way to query these properties. */
CoglTexture2D *
cogl_egl_texture_2d_new_from_image (CoglContext *ctx,
                                    int width,
                                    int height,
                                    CoglPixelFormat format,
                                    EGLImageKHR image,
                                    CoglError **error)
{
  CoglTextureLoader *loader;
  CoglTexture2D *tex;

  _COGL_RETURN_VAL_IF_FAIL (_cogl_context_get_winsys (ctx)->constraints &
                            COGL_RENDERER_CONSTRAINT_USES_EGL,
                            NULL);

  _COGL_RETURN_VAL_IF_FAIL (_cogl_has_private_feature
                            (ctx,
                             COGL_PRIVATE_FEATURE_TEXTURE_2D_FROM_EGL_IMAGE),
                            NULL);

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE;
  loader->src.egl_image.image = image;
  loader->src.egl_image.width = width;
  loader->src.egl_image.height = height;
  loader->src.egl_image.format = format;

  tex = _cogl_texture_2d_create_base (ctx, width, height, format, loader);

  if (!cogl_texture_allocate (COGL_TEXTURE (tex), error))
    {
      cogl_object_unref (tex);
      return NULL;
    }

  return tex;
}
#endif /* defined (COGL_HAS_EGL_SUPPORT) && defined (EGL_KHR_image_base) */

#ifdef COGL_HAS_WAYLAND_EGL_SERVER_SUPPORT
static void
shm_buffer_get_cogl_pixel_format (struct wl_shm_buffer *shm_buffer,
                                  CoglPixelFormat *format_out,
                                  CoglTextureComponents *components_out)
{
  CoglPixelFormat format;
  CoglTextureComponents components = COGL_TEXTURE_COMPONENTS_RGBA;

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
    default:
      g_warn_if_reached ();
      format = COGL_PIXEL_FORMAT_ARGB_8888;
    }

  if (format_out)
    *format_out = format;
  if (components_out)
    *components_out = components;
}

CoglBool
cogl_wayland_texture_set_region_from_shm_buffer (CoglTexture *texture,
                                                 int src_x,
                                                 int src_y,
                                                 int width,
                                                 int height,
                                                 struct wl_shm_buffer *
                                                   shm_buffer,
                                                 int dst_x,
                                                 int dst_y,
                                                 int level,
                                                 CoglError **error)
{
  const uint8_t *data = wl_shm_buffer_get_data (shm_buffer);
  int32_t stride = wl_shm_buffer_get_stride (shm_buffer);
  CoglPixelFormat format;
  int bpp;

  shm_buffer_get_cogl_pixel_format (shm_buffer, &format, NULL);
  bpp = _cogl_pixel_format_get_bytes_per_pixel (format);

  return _cogl_texture_set_region (COGL_TEXTURE (texture),
                                   width, height,
                                   format,
                                   stride,
                                   data + src_x * bpp + src_y * stride,
                                   dst_x, dst_y,
                                   level,
                                   error);
}

CoglTexture2D *
cogl_wayland_texture_2d_new_from_buffer (CoglContext *ctx,
                                         struct wl_resource *buffer,
                                         CoglError **error)
{
  struct wl_shm_buffer *shm_buffer;
  CoglTexture2D *tex = NULL;

  shm_buffer = wl_shm_buffer_get (buffer);

  if (shm_buffer)
    {
      int stride = wl_shm_buffer_get_stride (shm_buffer);
      int width = wl_shm_buffer_get_width (shm_buffer);
      int height = wl_shm_buffer_get_height (shm_buffer);
      CoglPixelFormat format;
      CoglTextureComponents components;
      CoglBitmap *bmp;

      shm_buffer_get_cogl_pixel_format (shm_buffer, &format, &components);

      bmp = cogl_bitmap_new_for_data (ctx,
                                      width, height,
                                      format,
                                      stride,
                                      wl_shm_buffer_get_data (shm_buffer));

      tex = cogl_texture_2d_new_from_bitmap (bmp);

      cogl_texture_set_components (COGL_TEXTURE (tex), components);

      cogl_object_unref (bmp);

      if (!cogl_texture_allocate (COGL_TEXTURE (tex), error))
        {
          cogl_object_unref (tex);
          return NULL;
        }
      else
        return tex;
    }
#ifdef COGL_HAS_EGL_SUPPORT
  else
    {
      int format, width, height;

      if (_cogl_egl_query_wayland_buffer (ctx,
                                          buffer,
                                          EGL_TEXTURE_FORMAT,
                                          &format) &&
          _cogl_egl_query_wayland_buffer (ctx,
                                          buffer,
                                          EGL_WIDTH,
                                          &width) &&
          _cogl_egl_query_wayland_buffer (ctx,
                                          buffer,
                                          EGL_HEIGHT,
                                          &height))
        {
          EGLImageKHR image;
          CoglPixelFormat internal_format;

          _COGL_RETURN_VAL_IF_FAIL (_cogl_context_get_winsys (ctx)->constraints &
                                    COGL_RENDERER_CONSTRAINT_USES_EGL,
                                    NULL);

          switch (format)
            {
            case EGL_TEXTURE_RGB:
              internal_format = COGL_PIXEL_FORMAT_RGB_888;
              break;
            case EGL_TEXTURE_RGBA:
              internal_format = COGL_PIXEL_FORMAT_RGBA_8888_PRE;
              break;
            default:
              _cogl_set_error (error,
                               COGL_SYSTEM_ERROR,
                               COGL_SYSTEM_ERROR_UNSUPPORTED,
                               "Can't create texture from unknown "
                               "wayland buffer format %d\n", format);
              return NULL;
            }

          image = _cogl_egl_create_image (ctx,
                                          EGL_WAYLAND_BUFFER_WL,
                                          buffer,
                                          NULL);
          tex = cogl_egl_texture_2d_new_from_image (ctx,
                                                    width, height,
                                                    internal_format,
                                                    image,
                                                    error);
          _cogl_egl_destroy_image (ctx, image);
          return tex;
        }
    }
#endif /* COGL_HAS_EGL_SUPPORT */

  _cogl_set_error (error,
                   COGL_SYSTEM_ERROR,
                   COGL_SYSTEM_ERROR_UNSUPPORTED,
                   "Can't create texture from unknown "
                   "wayland buffer type\n");
  return NULL;
}
#endif /* COGL_HAS_WAYLAND_EGL_SERVER_SUPPORT */

void
_cogl_texture_2d_externally_modified (CoglTexture *texture)
{
  if (!cogl_is_texture_2d (texture))
    return;

  COGL_TEXTURE_2D (texture)->mipmaps_dirty = TRUE;
}

void
_cogl_texture_2d_copy_from_framebuffer (CoglTexture2D *tex_2d,
                                        int src_x,
                                        int src_y,
                                        int width,
                                        int height,
                                        CoglFramebuffer *src_fb,
                                        int dst_x,
                                        int dst_y,
                                        int level)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = tex->context;

  /* Assert that the storage for this texture has been allocated */
  cogl_texture_allocate (tex, NULL); /* (abort on error) */

  ctx->driver_vtable->texture_2d_copy_from_framebuffer (tex_2d,
                                                        src_x,
                                                        src_y,
                                                        width,
                                                        height,
                                                        src_fb,
                                                        dst_x,
                                                        dst_y,
                                                        level);

  tex_2d->mipmaps_dirty = TRUE;
}

static int
_cogl_texture_2d_get_max_waste (CoglTexture *tex)
{
  return -1;
}

static CoglBool
_cogl_texture_2d_is_sliced (CoglTexture *tex)
{
  return FALSE;
}

static CoglBool
_cogl_texture_2d_can_hardware_repeat (CoglTexture *tex)
{
  CoglContext *ctx = tex->context;

  if (cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_NPOT_REPEAT) ||
      (_cogl_util_is_pot (tex->width) &&
       _cogl_util_is_pot (tex->height)))
    return TRUE;
  else
    return FALSE;
}

static void
_cogl_texture_2d_transform_coords_to_gl (CoglTexture *tex,
                                         float *s,
                                         float *t)
{
  /* The texture coordinates map directly so we don't need to do
     anything */
}

static CoglTransformResult
_cogl_texture_2d_transform_quad_coords_to_gl (CoglTexture *tex,
                                              float *coords)
{
  /* The texture coordinates map directly so we don't need to do
     anything other than check for repeats */

  int i;

  for (i = 0; i < 4; i++)
    if (coords[i] < 0.0f || coords[i] > 1.0f)
      {
        /* Repeat is needed */
        return (_cogl_texture_2d_can_hardware_repeat (tex) ?
                COGL_TRANSFORM_HARDWARE_REPEAT :
                COGL_TRANSFORM_SOFTWARE_REPEAT);
      }

  /* No repeat is needed */
  return COGL_TRANSFORM_NO_REPEAT;
}

static CoglBool
_cogl_texture_2d_get_gl_texture (CoglTexture *tex,
                                 GLuint *out_gl_handle,
                                 GLenum *out_gl_target)
{
  CoglContext *ctx = tex->context;
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  if (ctx->driver_vtable->texture_2d_get_gl_handle)
    {
      GLuint handle;

      if (out_gl_target)
        *out_gl_target = tex_2d->gl_target;

      handle = ctx->driver_vtable->texture_2d_get_gl_handle (tex_2d);

      if (out_gl_handle)
        *out_gl_handle = handle;

      return handle ? TRUE : FALSE;
    }
  else
    return FALSE;
}

static void
_cogl_texture_2d_pre_paint (CoglTexture *tex, CoglTexturePrePaintFlags flags)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  /* Only update if the mipmaps are dirty */
  if ((flags & COGL_TEXTURE_NEEDS_MIPMAP) &&
      tex_2d->auto_mipmap && tex_2d->mipmaps_dirty)
    {
      CoglContext *ctx = tex->context;

      ctx->driver_vtable->texture_2d_generate_mipmap (tex_2d);

      tex_2d->mipmaps_dirty = FALSE;
    }
}

static void
_cogl_texture_2d_ensure_non_quad_rendering (CoglTexture *tex)
{
  /* Nothing needs to be done */
}

static CoglBool
_cogl_texture_2d_set_region (CoglTexture *tex,
                             int src_x,
                             int src_y,
                             int dst_x,
                             int dst_y,
                             int width,
                             int height,
                             int level,
                             CoglBitmap *bmp,
                             CoglError **error)
{
  CoglContext *ctx = tex->context;
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  if (!ctx->driver_vtable->texture_2d_copy_from_bitmap (tex_2d,
                                                        src_x,
                                                        src_y,
                                                        width,
                                                        height,
                                                        bmp,
                                                        dst_x,
                                                        dst_y,
                                                        level,
                                                        error))
    {
      return FALSE;
    }

  tex_2d->mipmaps_dirty = TRUE;

  return TRUE;
}

static CoglBool
_cogl_texture_2d_is_get_data_supported (CoglTexture *tex)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglContext *ctx = tex->context;

  return ctx->driver_vtable->texture_2d_is_get_data_supported (tex_2d);
}

static CoglBool
_cogl_texture_2d_get_data (CoglTexture *tex,
                           CoglPixelFormat format,
                           int rowstride,
                           uint8_t *data)
{
  CoglContext *ctx = tex->context;

  if (ctx->driver_vtable->texture_2d_get_data)
    {
      CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
      ctx->driver_vtable->texture_2d_get_data (tex_2d, format, rowstride, data);
      return TRUE;
    }
  else
    return FALSE;
}

static CoglPixelFormat
_cogl_texture_2d_get_format (CoglTexture *tex)
{
  return COGL_TEXTURE_2D (tex)->internal_format;
}

static GLenum
_cogl_texture_2d_get_gl_format (CoglTexture *tex)
{
  return COGL_TEXTURE_2D (tex)->gl_internal_format;
}

static CoglBool
_cogl_texture_2d_is_foreign (CoglTexture *tex)
{
  return COGL_TEXTURE_2D (tex)->is_foreign;
}

static CoglTextureType
_cogl_texture_2d_get_type (CoglTexture *tex)
{
  return COGL_TEXTURE_TYPE_2D;
}

static const CoglTextureVtable
cogl_texture_2d_vtable =
  {
    TRUE, /* primitive */
    _cogl_texture_2d_allocate,
    _cogl_texture_2d_set_region,
    _cogl_texture_2d_is_get_data_supported,
    _cogl_texture_2d_get_data,
    NULL, /* foreach_sub_texture_in_region */
    _cogl_texture_2d_get_max_waste,
    _cogl_texture_2d_is_sliced,
    _cogl_texture_2d_can_hardware_repeat,
    _cogl_texture_2d_transform_coords_to_gl,
    _cogl_texture_2d_transform_quad_coords_to_gl,
    _cogl_texture_2d_get_gl_texture,
    _cogl_texture_2d_gl_flush_legacy_texobj_filters,
    _cogl_texture_2d_pre_paint,
    _cogl_texture_2d_ensure_non_quad_rendering,
    _cogl_texture_2d_gl_flush_legacy_texobj_wrap_modes,
    _cogl_texture_2d_get_format,
    _cogl_texture_2d_get_gl_format,
    _cogl_texture_2d_get_type,
    _cogl_texture_2d_is_foreign,
    _cogl_texture_2d_set_auto_mipmap
  };
