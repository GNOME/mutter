/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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
 *  Matthew Allum  <mallum@openedhand.com>
 *  Neil Roberts   <neil@linux.intel.com>
 *  Robert Bragg   <robert@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-private.h"
#include "cogl/cogl-util.h"
#include "cogl/cogl-bitmap.h"
#include "cogl/cogl-bitmap-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/cogl-pipeline.h"
#include "cogl/cogl-context-private.h"
#include "cogl/driver/gl/gl3/cogl-texture-driver-gl3-private.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"
#include "cogl/driver/gl/cogl-bitmap-gl-private.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef GL_TEXTURE_SWIZZLE_RGBA
#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#endif

struct _CoglTextureDriverGL3
{
  CoglTextureDriverGL parent_instance;
};

G_DEFINE_FINAL_TYPE (CoglTextureDriverGL3,
                     cogl_texture_driver_gl3,
                     COGL_TYPE_TEXTURE_DRIVER_GL)

static GLuint
cogl_texture_driver_gl3_gen (CoglTextureDriverGL *driver,
                             CoglContext         *ctx,
                             GLenum               gl_target,
                             CoglPixelFormat      internal_format)
{
  GLuint tex;

  GE (ctx, glGenTextures (1, &tex));

  _cogl_bind_gl_texture_transient (ctx, gl_target, tex);

  switch (gl_target)
    {
    case GL_TEXTURE_2D:
      /* In case automatic mipmap generation gets disabled for this
       * texture but a minification filter depending on mipmap
       * interpolation is selected then we initialize the max mipmap
       * level to 0 so OpenGL will consider the texture storage to be
       * "complete".
       */
      GE( ctx, glTexParameteri (gl_target, GL_TEXTURE_MAX_LEVEL, 0));

      /* GL_TEXTURE_MAG_FILTER defaults to GL_LINEAR, no need to set it */
      GE( ctx, glTexParameteri (gl_target,
                                GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR) );
      break;

    case GL_TEXTURE_RECTANGLE_ARB:
      /* Texture rectangles already default to GL_LINEAR so nothing
         needs to be done */
      break;

    default:
      g_assert_not_reached();
    }

  /* As the driver doesn't support alpha textures directly then we'll
   * fake them by setting the swizzle parameters */
  if (internal_format == COGL_PIXEL_FORMAT_A_8 &&
      _cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_TEXTURE_SWIZZLE))
    {
      static const GLint red_swizzle[] = { GL_ZERO, GL_ZERO, GL_ZERO, GL_RED };

      GE( ctx, glTexParameteriv (gl_target,
                                 GL_TEXTURE_SWIZZLE_RGBA,
                                 red_swizzle) );
    }

  return tex;
}

/* OpenGL - unlike GLES - can upload a sub region of pixel data from a larger
 * source buffer */
static void
prep_gl_for_pixels_upload_full (CoglContext *ctx,
                                int pixels_rowstride,
                                int image_height,
                                int pixels_src_x,
                                int pixels_src_y,
                                int pixels_bpp)
{
  GE( ctx, glPixelStorei (GL_UNPACK_ROW_LENGTH,
                          pixels_rowstride / pixels_bpp) );

  GE( ctx, glPixelStorei (GL_UNPACK_SKIP_PIXELS, pixels_src_x) );
  GE( ctx, glPixelStorei (GL_UNPACK_SKIP_ROWS, pixels_src_y) );

  _cogl_texture_gl_prep_alignment_for_pixels_upload (ctx, pixels_rowstride);
}

static gboolean
cogl_texture_driver_gl3_upload_subregion_to_gl (CoglTextureDriverGL  *driver,
                                                CoglContext          *ctx,
                                                CoglTexture          *texture,
                                                int                   src_x,
                                                int                   src_y,
                                                int                   dst_x,
                                                int                   dst_y,
                                                int                   width,
                                                int                   height,
                                                int                   level,
                                                CoglBitmap           *source_bmp,
                                                GLuint                source_gl_format,
                                                GLuint                source_gl_type,
                                                GError              **error)
{
  GLenum gl_target;
  GLuint gl_handle;
  uint8_t *data;
  CoglPixelFormat source_format = cogl_bitmap_get_format (source_bmp);
  int bpp;
  gboolean status = TRUE;
  GError *internal_error = NULL;
  int level_width;
  int level_height;

  g_return_val_if_fail (source_format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (source_format) == 1,
                        FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (source_format, 0);
  cogl_texture_get_gl_texture (texture, &gl_handle, &gl_target);

  data = _cogl_bitmap_gl_bind (source_bmp, COGL_BUFFER_ACCESS_READ, 0, &internal_error);

  /* NB: _cogl_bitmap_gl_bind() may return NULL when successful so we
   * have to explicitly check the cogl error pointer to catch
   * problems... */
  if (internal_error)
    {
      g_propagate_error (error, internal_error);
      return FALSE;
    }

  /* Setup gl alignment to match rowstride and top-left corner */
  prep_gl_for_pixels_upload_full (ctx,
                                  cogl_bitmap_get_rowstride (source_bmp),
                                  0,
                                  src_x,
                                  src_y,
                                  bpp);

  _cogl_bind_gl_texture_transient (ctx, gl_target, gl_handle);

  /* Clear any GL errors */
  _cogl_gl_util_clear_gl_errors (ctx);

  _cogl_texture_get_level_size (texture,
                                level,
                                &level_width,
                                &level_height,
                                NULL);

  if (level_width == width && level_height == height)
    {
      /* GL gets upset if you use glTexSubImage2D to initialize the
       * contents of a mipmap level so we make sure to use
       * glTexImage2D if we are uploading a full mipmap level.
       */
      ctx->glTexImage2D (gl_target,
                         level,
                         _cogl_texture_gl_get_format (texture),
                         width,
                         height,
                         0,
                         source_gl_format,
                         source_gl_type,
                         data);

    }
  else
    {
      /* GL gets upset if you use glTexSubImage2D to initialize the
       * contents of a mipmap level so if this is the first time
       * we've seen a request to upload to this level we call
       * glTexImage2D first to assert that the storage for this
       * level exists.
       */
      if (cogl_texture_get_max_level_set (texture) < level)
        {
          ctx->glTexImage2D (gl_target,
                             level,
                             _cogl_texture_gl_get_format (texture),
                             level_width,
                             level_height,
                             0,
                             source_gl_format,
                             source_gl_type,
                             NULL);
        }

      ctx->glTexSubImage2D (gl_target,
                            level,
                            dst_x, dst_y,
                            width, height,
                            source_gl_format,
                            source_gl_type,
                            data);
    }

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    status = FALSE;

  _cogl_bitmap_gl_unbind (source_bmp);

  return status;
}

static gboolean
cogl_texture_driver_gl3_upload_to_gl (CoglTextureDriverGL *driver,
                                      CoglContext         *ctx,
                                      GLenum               gl_target,
                                      GLuint               gl_handle,
                                      CoglBitmap          *source_bmp,
                                      GLint                internal_gl_format,
                                      GLuint               source_gl_format,
                                      GLuint               source_gl_type,
                                      GError             **error)
{
  uint8_t *data;
  CoglPixelFormat source_format = cogl_bitmap_get_format (source_bmp);
  int bpp;
  gboolean status = TRUE;
  GError *internal_error = NULL;

  g_return_val_if_fail (source_format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (source_format) == 1,
                        FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (source_format, 0);

  data = _cogl_bitmap_gl_bind (source_bmp,
                               COGL_BUFFER_ACCESS_READ,
                               0, /* hints */
                               &internal_error);

  /* NB: _cogl_bitmap_gl_bind() may return NULL when successful so we
   * have to explicitly check the cogl error pointer to catch
   * problems... */
  if (internal_error)
    {
      g_propagate_error (error, internal_error);
      return FALSE;
    }

  /* Setup gl alignment to match rowstride and top-left corner */
  prep_gl_for_pixels_upload_full (ctx,
                                  cogl_bitmap_get_rowstride (source_bmp),
                                  0, 0, 0, bpp);

  _cogl_bind_gl_texture_transient (ctx, gl_target, gl_handle);

  /* Clear any GL errors */
  _cogl_gl_util_clear_gl_errors (ctx);

  ctx->glTexImage2D (gl_target, 0,
                     internal_gl_format,
                     cogl_bitmap_get_width (source_bmp),
                     cogl_bitmap_get_height (source_bmp),
                     0,
                     source_gl_format,
                     source_gl_type,
                     data);

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    status = FALSE;

  _cogl_bitmap_gl_unbind (source_bmp);

  return status;
}

static gboolean
cogl_texture_driver_gl3_gl_get_tex_image (CoglTextureDriverGL *driver,
                                          CoglContext         *ctx,
                                          GLenum               gl_target,
                                          GLenum               dest_gl_format,
                                          GLenum               dest_gl_type,
                                          uint8_t             *dest)
{
  GE (ctx, glGetTexImage (gl_target,
                          0, /* level */
                          dest_gl_format,
                          dest_gl_type,
                          (GLvoid *)dest));
  return TRUE;
}

static CoglPixelFormat
cogl_texture_driver_gl3_find_best_gl_get_data_format (CoglTextureDriverGL *tex_driver,
                                                      CoglContext         *context,
                                                      CoglPixelFormat      format,
                                                      GLenum              *closest_gl_format,
                                                      GLenum              *closest_gl_type)
{
  CoglDriver *driver = cogl_context_get_driver (context);
  CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
  CoglDriverGLClass *driver_klass = COGL_DRIVER_GL_GET_CLASS (driver_gl);

  return driver_klass->pixel_format_to_gl (driver_gl,
                                           context,
                                           format,
                                           NULL, /* don't need */
                                           closest_gl_format,
                                           closest_gl_type);
}

static gboolean
cogl_texture_driver_gl3_is_get_data_supported (CoglTextureDriver *driver,
                                               CoglTexture2D     *tex_2d)
{
  return tex_2d->is_get_data_supported;
}

static void
cogl_texture_driver_gl3_texture_2d_gl_get_data (CoglTextureDriver *tex_driver,
                                                CoglTexture2D     *tex_2d,
                                                CoglPixelFormat    format,
                                                int                rowstride,
                                                uint8_t           *data)
{
  CoglContext *ctx = cogl_texture_get_context (COGL_TEXTURE (tex_2d));
  CoglTextureDriverGL *tex_driver_gl =
    COGL_TEXTURE_DRIVER_GL (tex_driver);
  CoglTextureDriverGLClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GL_GET_CLASS (tex_driver_gl);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
  CoglDriverGLClass *driver_klass = COGL_DRIVER_GL_GET_CLASS (driver_gl);
  uint8_t bpp;
  int width = cogl_texture_get_width (COGL_TEXTURE (tex_2d));
  GLenum gl_format;
  GLenum gl_type;

  g_return_if_fail (format != COGL_PIXEL_FORMAT_ANY);
  g_return_if_fail (cogl_pixel_format_get_n_planes (format) == 1);

  bpp = cogl_pixel_format_get_bytes_per_pixel (format, 0);

  driver_klass->pixel_format_to_gl (driver_gl,
                                    ctx,
                                    format,
                                    NULL, /* internal format */
                                    &gl_format,
                                    &gl_type);

  driver_klass->prep_gl_for_pixels_download (driver_gl,
                                             ctx,
                                             width,
                                             rowstride,
                                             bpp);

  _cogl_bind_gl_texture_transient (ctx, tex_2d->gl_target,
                                   tex_2d->gl_texture);

  tex_driver_klass->gl_get_tex_image (tex_driver_gl,
                                      ctx,
                                      tex_2d->gl_target,
                                      gl_format,
                                      gl_type,
                                      data);
}

static void
cogl_texture_driver_gl3_class_init (CoglTextureDriverGL3Class *klass)
{
  CoglTextureDriverClass *driver_klass = COGL_TEXTURE_DRIVER_CLASS (klass);
  CoglTextureDriverGLClass *driver_gl_klass = COGL_TEXTURE_DRIVER_GL_CLASS (klass);

  driver_klass->texture_2d_is_get_data_supported = cogl_texture_driver_gl3_is_get_data_supported;
  driver_klass->texture_2d_get_data = cogl_texture_driver_gl3_texture_2d_gl_get_data;

  driver_gl_klass->gen = cogl_texture_driver_gl3_gen;
  driver_gl_klass->upload_subregion_to_gl = cogl_texture_driver_gl3_upload_subregion_to_gl;
  driver_gl_klass->upload_to_gl = cogl_texture_driver_gl3_upload_to_gl;
  driver_gl_klass->gl_get_tex_image = cogl_texture_driver_gl3_gl_get_tex_image;
  driver_gl_klass->find_best_gl_get_data_format = cogl_texture_driver_gl3_find_best_gl_get_data_format;
}

static void
cogl_texture_driver_gl3_init (CoglTextureDriverGL3 *driver)
{
}
