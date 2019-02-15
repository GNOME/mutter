/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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
#include "cogl-texture-rectangle-private.h"
#include "cogl-texture-driver.h"
#include "cogl-context-private.h"
#include "cogl-object-private.h"
#include "cogl-journal-private.h"
#include "cogl-error-private.h"
#include "cogl-gtype-private.h"
#include "driver/gl/cogl-pipeline-opengl-private.h"
#include "driver/gl/cogl-util-gl-private.h"

#include <string.h>
#include <math.h>

/* These aren't defined under GLES */
#ifndef GL_TEXTURE_RECTANGLE_ARB
#define GL_TEXTURE_RECTANGLE_ARB 0x84F5
#endif
#ifndef GL_CLAMP
#define GL_CLAMP                 0x2900
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER       0x812D
#endif

static void _cogl_texture_rectangle_free (CoglTextureRectangle *tex_rect);

COGL_TEXTURE_DEFINE (TextureRectangle, texture_rectangle);
COGL_GTYPE_DEFINE_CLASS (TextureRectangle, texture_rectangle,
                         COGL_GTYPE_IMPLEMENT_INTERFACE (texture));

static const CoglTextureVtable cogl_texture_rectangle_vtable;

static gboolean
can_use_wrap_mode (GLenum wrap_mode)
{
  return (wrap_mode == GL_CLAMP ||
          wrap_mode == GL_CLAMP_TO_EDGE ||
          wrap_mode == GL_CLAMP_TO_BORDER);
}

static void
_cogl_texture_rectangle_gl_flush_legacy_texobj_wrap_modes (CoglTexture *tex,
                                                           GLenum wrap_mode_s,
                                                           GLenum wrap_mode_t,
                                                           GLenum wrap_mode_p)
{
  CoglTextureRectangle *tex_rect = COGL_TEXTURE_RECTANGLE (tex);
  CoglContext *ctx = tex->context;

  /* Only set the wrap mode if it's different from the current value
     to avoid too many GL calls. Texture rectangle doesn't make use of
     the r coordinate so we can ignore its wrap mode */
  if (tex_rect->gl_legacy_texobj_wrap_mode_s != wrap_mode_s ||
      tex_rect->gl_legacy_texobj_wrap_mode_t != wrap_mode_t)
    {
      g_assert (can_use_wrap_mode (wrap_mode_s));
      g_assert (can_use_wrap_mode (wrap_mode_t));

      _cogl_bind_gl_texture_transient (GL_TEXTURE_RECTANGLE_ARB,
                                       tex_rect->gl_texture,
                                       tex_rect->is_foreign);
      GE( ctx, glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
                                GL_TEXTURE_WRAP_S, wrap_mode_s) );
      GE( ctx, glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
                                GL_TEXTURE_WRAP_T, wrap_mode_t) );

      tex_rect->gl_legacy_texobj_wrap_mode_s = wrap_mode_s;
      tex_rect->gl_legacy_texobj_wrap_mode_t = wrap_mode_t;
    }
}

static void
_cogl_texture_rectangle_free (CoglTextureRectangle *tex_rect)
{
  if (!tex_rect->is_foreign && tex_rect->gl_texture)
    _cogl_delete_gl_texture (tex_rect->gl_texture);

  /* Chain up */
  _cogl_texture_free (COGL_TEXTURE (tex_rect));
}

static gboolean
_cogl_texture_rectangle_can_create (CoglContext *ctx,
                                    unsigned int width,
                                    unsigned int height,
                                    CoglPixelFormat internal_format,
                                    CoglError **error)
{
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;

  if (!cogl_has_feature (ctx, COGL_FEATURE_ID_TEXTURE_RECTANGLE))
    {
      _cogl_set_error (error,
                       COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_TYPE,
                       "The CoglTextureRectangle feature isn't available");
      return FALSE;
    }

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          internal_format,
                                          &gl_intformat,
                                          &gl_format,
                                          &gl_type);

  /* Check that the driver can create a texture with that size */
  if (!ctx->texture_driver->size_supported (ctx,
                                            GL_TEXTURE_RECTANGLE_ARB,
                                            gl_intformat,
                                            gl_format,
                                            gl_type,
                                            width,
                                            height))
    {
      _cogl_set_error (error,
                       COGL_TEXTURE_ERROR,
                       COGL_TEXTURE_ERROR_SIZE,
                       "The requested texture size + format is unsupported");
      return FALSE;
    }

  return TRUE;
}

static void
_cogl_texture_rectangle_set_auto_mipmap (CoglTexture *tex,
                                         gboolean value)
{
  /* Rectangle textures currently never support mipmapping so there's
     no point in doing anything here */
}

static CoglTextureRectangle *
_cogl_texture_rectangle_create_base (CoglContext *ctx,
                                     int width,
                                     int height,
                                     CoglPixelFormat internal_format,
                                     CoglTextureLoader *loader)
{
  CoglTextureRectangle *tex_rect = g_new (CoglTextureRectangle, 1);
  CoglTexture *tex = COGL_TEXTURE (tex_rect);

  _cogl_texture_init (tex, ctx, width, height,
                      internal_format, loader,
                      &cogl_texture_rectangle_vtable);

  tex_rect->gl_texture = 0;
  tex_rect->is_foreign = FALSE;

  /* We default to GL_LINEAR for both filters */
  tex_rect->gl_legacy_texobj_min_filter = GL_LINEAR;
  tex_rect->gl_legacy_texobj_mag_filter = GL_LINEAR;

  /* Wrap mode not yet set */
  tex_rect->gl_legacy_texobj_wrap_mode_s = GL_FALSE;
  tex_rect->gl_legacy_texobj_wrap_mode_t = GL_FALSE;

  return _cogl_texture_rectangle_object_new (tex_rect);
}

CoglTextureRectangle *
cogl_texture_rectangle_new_with_size (CoglContext *ctx,
                                      int width,
                                      int height)
{
  CoglTextureLoader *loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_SIZED;
  loader->src.sized.width = width;
  loader->src.sized.height = height;

  return _cogl_texture_rectangle_create_base (ctx, width, height,
                                              COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                              loader);
}

static gboolean
allocate_with_size (CoglTextureRectangle *tex_rect,
                    CoglTextureLoader *loader,
                    CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_rect);
  CoglContext *ctx = tex->context;
  CoglPixelFormat internal_format;
  int width = loader->src.sized.width;
  int height = loader->src.sized.height;
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;
  GLenum gl_texture;

  internal_format =
    _cogl_texture_determine_internal_format (tex, COGL_PIXEL_FORMAT_ANY);

  if (!_cogl_texture_rectangle_can_create (ctx,
                                           width,
                                           height,
                                           internal_format,
                                           error))
    return FALSE;

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          internal_format,
                                          &gl_intformat,
                                          &gl_format,
                                          &gl_type);

  gl_texture =
    ctx->texture_driver->gen (ctx,
                              GL_TEXTURE_RECTANGLE_ARB,
                              internal_format);
  _cogl_bind_gl_texture_transient (GL_TEXTURE_RECTANGLE_ARB,
                                   gl_texture,
                                   tex_rect->is_foreign);

  /* Clear any GL errors */
  _cogl_gl_util_clear_gl_errors (ctx);

  ctx->glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, gl_intformat,
                     width, height, 0, gl_format, gl_type, NULL);

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    {
      GE( ctx, glDeleteTextures (1, &gl_texture) );
      return FALSE;
    }

  tex_rect->internal_format = internal_format;

  tex_rect->gl_texture = gl_texture;
  tex_rect->gl_format = gl_intformat;

  _cogl_texture_set_allocated (COGL_TEXTURE (tex_rect),
                               internal_format, width, height);

  return TRUE;
}

static gboolean
allocate_from_bitmap (CoglTextureRectangle *tex_rect,
                      CoglTextureLoader *loader,
                      CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_rect);
  CoglContext *ctx = tex->context;
  CoglPixelFormat internal_format;
  CoglBitmap *bmp = loader->src.bitmap.bitmap;
  int width = cogl_bitmap_get_width (bmp);
  int height = cogl_bitmap_get_height (bmp);
  gboolean can_convert_in_place = loader->src.bitmap.can_convert_in_place;
  CoglBitmap *upload_bmp;
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;

  internal_format =
    _cogl_texture_determine_internal_format (tex, cogl_bitmap_get_format (bmp));

  if (!_cogl_texture_rectangle_can_create (ctx,
                                           width,
                                           height,
                                           internal_format,
                                           error))
    return FALSE;

  upload_bmp = _cogl_bitmap_convert_for_upload (bmp,
                                                internal_format,
                                                can_convert_in_place,
                                                error);
  if (upload_bmp == NULL)
    return FALSE;

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          cogl_bitmap_get_format (upload_bmp),
                                          NULL, /* internal format */
                                          &gl_format,
                                          &gl_type);
  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          internal_format,
                                          &gl_intformat,
                                          NULL,
                                          NULL);

  tex_rect->gl_texture =
    ctx->texture_driver->gen (ctx,
                              GL_TEXTURE_RECTANGLE_ARB,
                              internal_format);
  if (!ctx->texture_driver->upload_to_gl (ctx,
                                          GL_TEXTURE_RECTANGLE_ARB,
                                          tex_rect->gl_texture,
                                          FALSE,
                                          upload_bmp,
                                          gl_intformat,
                                          gl_format,
                                          gl_type,
                                          error))
    {
      cogl_object_unref (upload_bmp);
      return FALSE;
    }

  tex_rect->gl_format = gl_intformat;
  tex_rect->internal_format = internal_format;

  cogl_object_unref (upload_bmp);

  _cogl_texture_set_allocated (COGL_TEXTURE (tex_rect),
                               internal_format, width, height);

  return TRUE;
}

static gboolean
allocate_from_gl_foreign (CoglTextureRectangle *tex_rect,
                          CoglTextureLoader *loader,
                          CoglError **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_rect);
  CoglContext *ctx = tex->context;
  CoglPixelFormat format = loader->src.gl_foreign.format;
  GLint gl_compressed = GL_FALSE;
  GLenum gl_int_format = 0;

  if (!ctx->texture_driver->allows_foreign_gl_target (ctx,
                                                      GL_TEXTURE_RECTANGLE_ARB))
    {
      _cogl_set_error (error,
                       COGL_SYSTEM_ERROR,
                       COGL_SYSTEM_ERROR_UNSUPPORTED,
                       "Foreign GL_TEXTURE_RECTANGLE textures are not "
                       "supported by your system");
      return FALSE;
    }

  /* Make sure binding succeeds */
  _cogl_gl_util_clear_gl_errors (ctx);

  _cogl_bind_gl_texture_transient (GL_TEXTURE_RECTANGLE_ARB,
                                   loader->src.gl_foreign.gl_handle, TRUE);
  if (_cogl_gl_util_get_error (ctx) != GL_NO_ERROR)
    {
      _cogl_set_error (error,
                       COGL_SYSTEM_ERROR,
                       COGL_SYSTEM_ERROR_UNSUPPORTED,
                       "Failed to bind foreign GL_TEXTURE_RECTANGLE texture");
      return FALSE;
    }

  /* Obtain texture parameters */

#ifdef HAVE_COGL_GL
  if (_cogl_has_private_feature
      (ctx, COGL_PRIVATE_FEATURE_QUERY_TEXTURE_PARAMETERS))
    {
      GLint val;

      GE( ctx, glGetTexLevelParameteriv (GL_TEXTURE_RECTANGLE_ARB, 0,
                                         GL_TEXTURE_COMPRESSED,
                                         &gl_compressed) );

      GE( ctx, glGetTexLevelParameteriv (GL_TEXTURE_RECTANGLE_ARB, 0,
                                         GL_TEXTURE_INTERNAL_FORMAT,
                                         &val) );

      gl_int_format = val;

      /* If we can query GL for the actual pixel format then we'll ignore
         the passed in format and use that. */
      if (!ctx->driver_vtable->pixel_format_from_gl_internal (ctx,
                                                              gl_int_format,
                                                              &format))
        {
          _cogl_set_error (error,
                           COGL_SYSTEM_ERROR,
                           COGL_SYSTEM_ERROR_UNSUPPORTED,
                           "Unsupported internal format for foreign texture");
          return FALSE;
        }
    }
  else
#endif
    {
      /* Otherwise we'll assume we can derive the GL format from the
         passed in format */
      ctx->driver_vtable->pixel_format_to_gl (ctx,
                                              format,
                                              &gl_int_format,
                                              NULL,
                                              NULL);
    }

  /* Compressed texture images not supported */
  if (gl_compressed == GL_TRUE)
    {
      _cogl_set_error (error,
                       COGL_SYSTEM_ERROR,
                       COGL_SYSTEM_ERROR_UNSUPPORTED,
                       "Compressed foreign textures aren't currently supported");
      return FALSE;
    }

  /* Setup bitmap info */
  tex_rect->is_foreign = TRUE;

  tex_rect->gl_texture = loader->src.gl_foreign.gl_handle;
  tex_rect->gl_format = gl_int_format;

  /* Unknown filter */
  tex_rect->gl_legacy_texobj_min_filter = GL_FALSE;
  tex_rect->gl_legacy_texobj_mag_filter = GL_FALSE;

  tex_rect->internal_format = format;

  _cogl_texture_set_allocated (COGL_TEXTURE (tex_rect),
                               format,
                               loader->src.gl_foreign.width,
                               loader->src.gl_foreign.height);

  return TRUE;
}

static gboolean
_cogl_texture_rectangle_allocate (CoglTexture *tex,
                                  CoglError **error)
{
  CoglTextureRectangle *tex_rect = COGL_TEXTURE_RECTANGLE (tex);
  CoglTextureLoader *loader = tex->loader;

  _COGL_RETURN_VAL_IF_FAIL (loader, FALSE);

  switch (loader->src_type)
    {
    case COGL_TEXTURE_SOURCE_TYPE_SIZED:
      return allocate_with_size (tex_rect, loader, error);
    case COGL_TEXTURE_SOURCE_TYPE_BITMAP:
      return allocate_from_bitmap (tex_rect, loader, error);
    case COGL_TEXTURE_SOURCE_TYPE_GL_FOREIGN:
      return allocate_from_gl_foreign (tex_rect, loader, error);
    default:
      break;
    }

  g_return_val_if_reached (FALSE);
}

CoglTextureRectangle *
cogl_texture_rectangle_new_from_bitmap (CoglBitmap *bmp)
{
  CoglTextureLoader *loader;

  _COGL_RETURN_VAL_IF_FAIL (cogl_is_bitmap (bmp), NULL);

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_BITMAP;
  loader->src.bitmap.bitmap = cogl_object_ref (bmp);
  loader->src.bitmap.can_convert_in_place = FALSE; /* TODO add api for this */

  return _cogl_texture_rectangle_create_base (_cogl_bitmap_get_context (bmp),
                                              cogl_bitmap_get_width (bmp),
                                              cogl_bitmap_get_height (bmp),
                                              cogl_bitmap_get_format (bmp),
                                              loader);
}

CoglTextureRectangle *
cogl_texture_rectangle_new_from_foreign (CoglContext *ctx,
                                         unsigned int gl_handle,
                                         int width,
                                         int height,
                                         CoglPixelFormat format)
{
  CoglTextureLoader *loader;

  /* NOTE: width, height and internal format are not queriable in
   * GLES, hence such a function prototype. Also in the case of full
   * opengl the user may be creating a Cogl texture for a
   * texture_from_pixmap object where glTexImage2D may not have been
   * called and the texture_from_pixmap spec doesn't clarify that it
   * is reliable to query back the size from OpenGL.
   */

  /* Assert that it is a valid GL texture object */
  _COGL_RETURN_VAL_IF_FAIL (ctx->glIsTexture (gl_handle), NULL);

  /* Validate width and height */
  _COGL_RETURN_VAL_IF_FAIL (width > 0 && height > 0, NULL);

  loader = _cogl_texture_create_loader ();
  loader->src_type = COGL_TEXTURE_SOURCE_TYPE_GL_FOREIGN;
  loader->src.gl_foreign.gl_handle = gl_handle;
  loader->src.gl_foreign.width = width;
  loader->src.gl_foreign.height = height;
  loader->src.gl_foreign.format = format;

  return _cogl_texture_rectangle_create_base (ctx, width, height,
                                              format, loader);
}

static int
_cogl_texture_rectangle_get_max_waste (CoglTexture *tex)
{
  return -1;
}

static gboolean
_cogl_texture_rectangle_is_sliced (CoglTexture *tex)
{
  return FALSE;
}

static gboolean
_cogl_texture_rectangle_can_hardware_repeat (CoglTexture *tex)
{
  return FALSE;
}

static void
_cogl_texture_rectangle_transform_coords_to_gl (CoglTexture *tex,
                                                float *s,
                                                float *t)
{
  *s *= tex->width;
  *t *= tex->height;
}

static CoglTransformResult
_cogl_texture_rectangle_transform_quad_coords_to_gl (CoglTexture *tex,
                                                     float *coords)
{
  gboolean need_repeat = FALSE;
  int i;

  for (i = 0; i < 4; i++)
    {
      if (coords[i] < 0.0f || coords[i] > 1.0f)
        need_repeat = TRUE;
      coords[i] *= (i & 1) ? tex->height : tex->width;
    }

  return (need_repeat ? COGL_TRANSFORM_SOFTWARE_REPEAT
          : COGL_TRANSFORM_NO_REPEAT);
}

static gboolean
_cogl_texture_rectangle_get_gl_texture (CoglTexture *tex,
                                        GLuint *out_gl_handle,
                                        GLenum *out_gl_target)
{
  CoglTextureRectangle *tex_rect = COGL_TEXTURE_RECTANGLE (tex);

  if (out_gl_handle)
    *out_gl_handle = tex_rect->gl_texture;

  if (out_gl_target)
    *out_gl_target = GL_TEXTURE_RECTANGLE_ARB;

  return TRUE;
}

static void
_cogl_texture_rectangle_gl_flush_legacy_texobj_filters (CoglTexture *tex,
                                                        GLenum min_filter,
                                                        GLenum mag_filter)
{
  CoglTextureRectangle *tex_rect = COGL_TEXTURE_RECTANGLE (tex);
  CoglContext *ctx = tex->context;

  if (min_filter == tex_rect->gl_legacy_texobj_min_filter
      && mag_filter == tex_rect->gl_legacy_texobj_mag_filter)
    return;

  /* Rectangle textures don't support mipmapping */
  g_assert (min_filter == GL_LINEAR || min_filter == GL_NEAREST);

  /* Store new values */
  tex_rect->gl_legacy_texobj_min_filter = min_filter;
  tex_rect->gl_legacy_texobj_mag_filter = mag_filter;

  /* Apply new filters to the texture */
  _cogl_bind_gl_texture_transient (GL_TEXTURE_RECTANGLE_ARB,
                                   tex_rect->gl_texture,
                                   tex_rect->is_foreign);
  GE( ctx, glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
                            mag_filter) );
  GE( ctx, glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
                            min_filter) );
}

static void
_cogl_texture_rectangle_pre_paint (CoglTexture *tex,
                                   CoglTexturePrePaintFlags flags)
{
  /* Rectangle textures don't support mipmaps */
  g_assert ((flags & COGL_TEXTURE_NEEDS_MIPMAP) == 0);
}

static void
_cogl_texture_rectangle_ensure_non_quad_rendering (CoglTexture *tex)
{
  /* Nothing needs to be done */
}

static gboolean
_cogl_texture_rectangle_set_region (CoglTexture *tex,
                                    int src_x,
                                    int src_y,
                                    int dst_x,
                                    int dst_y,
                                    int dst_width,
                                    int dst_height,
                                    int level,
                                    CoglBitmap *bmp,
                                    CoglError **error)
{
  CoglBitmap *upload_bmp;
  GLenum gl_format;
  GLenum gl_type;
  CoglContext *ctx = tex->context;
  gboolean status;

  upload_bmp =
    _cogl_bitmap_convert_for_upload (bmp,
                                     _cogl_texture_get_format (tex),
                                     FALSE, /* can't convert in place */
                                     error);
  if (upload_bmp == NULL)
    return FALSE;

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          cogl_bitmap_get_format (upload_bmp),
                                          NULL, /* internal format */
                                          &gl_format,
                                          &gl_type);

  /* Send data to GL */
  status =
    ctx->texture_driver->upload_subregion_to_gl (ctx,
                                                 tex,
                                                 FALSE,
                                                 src_x, src_y,
                                                 dst_x, dst_y,
                                                 dst_width, dst_height,
                                                 level,
                                                 upload_bmp,
                                                 gl_format,
                                                 gl_type,
                                                 error);

  cogl_object_unref (upload_bmp);

  return status;
}

static gboolean
_cogl_texture_rectangle_get_data (CoglTexture *tex,
                                  CoglPixelFormat format,
                                  int rowstride,
                                  uint8_t *data)
{
  CoglTextureRectangle *tex_rect = COGL_TEXTURE_RECTANGLE (tex);
  CoglContext *ctx = tex->context;
  int bpp;
  GLenum gl_format;
  GLenum gl_type;

  bpp = _cogl_pixel_format_get_bytes_per_pixel (format);

  ctx->driver_vtable->pixel_format_to_gl (ctx,
                                          format,
                                          NULL, /* internal format */
                                          &gl_format,
                                          &gl_type);

  ctx->texture_driver->prep_gl_for_pixels_download (ctx,
                                                    rowstride,
                                                    tex->width,
                                                    bpp);

  _cogl_bind_gl_texture_transient (GL_TEXTURE_RECTANGLE_ARB,
                                   tex_rect->gl_texture,
                                   tex_rect->is_foreign);
  return ctx->texture_driver->gl_get_tex_image (ctx,
                                                GL_TEXTURE_RECTANGLE_ARB,
                                                gl_format,
                                                gl_type,
                                                data);
}

static CoglPixelFormat
_cogl_texture_rectangle_get_format (CoglTexture *tex)
{
  return COGL_TEXTURE_RECTANGLE (tex)->internal_format;
}

static GLenum
_cogl_texture_rectangle_get_gl_format (CoglTexture *tex)
{
  return COGL_TEXTURE_RECTANGLE (tex)->gl_format;
}

static gboolean
_cogl_texture_rectangle_is_foreign (CoglTexture *tex)
{
  return COGL_TEXTURE_RECTANGLE (tex)->is_foreign;
}

static CoglTextureType
_cogl_texture_rectangle_get_type (CoglTexture *tex)
{
  return COGL_TEXTURE_TYPE_RECTANGLE;
}

static const CoglTextureVtable
cogl_texture_rectangle_vtable =
  {
    TRUE, /* primitive */
    _cogl_texture_rectangle_allocate,
    _cogl_texture_rectangle_set_region,
    NULL, /* is_get_data_supported */
    _cogl_texture_rectangle_get_data,
    NULL, /* foreach_sub_texture_in_region */
    _cogl_texture_rectangle_get_max_waste,
    _cogl_texture_rectangle_is_sliced,
    _cogl_texture_rectangle_can_hardware_repeat,
    _cogl_texture_rectangle_transform_coords_to_gl,
    _cogl_texture_rectangle_transform_quad_coords_to_gl,
    _cogl_texture_rectangle_get_gl_texture,
    _cogl_texture_rectangle_gl_flush_legacy_texobj_filters,
    _cogl_texture_rectangle_pre_paint,
    _cogl_texture_rectangle_ensure_non_quad_rendering,
    _cogl_texture_rectangle_gl_flush_legacy_texobj_wrap_modes,
    _cogl_texture_rectangle_get_format,
    _cogl_texture_rectangle_get_gl_format,
    _cogl_texture_rectangle_get_type,
    _cogl_texture_rectangle_is_foreign,
    _cogl_texture_rectangle_set_auto_mipmap
  };
