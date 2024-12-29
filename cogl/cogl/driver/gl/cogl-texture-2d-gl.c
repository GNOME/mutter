/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009,2010,2011,2012 Intel Corporation.
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
 *  Robert Bragg   <robert@linux.intel.com>
 */

#include "config.h"

#include <string.h>

#include "cogl/cogl-private.h"
#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

#if defined (HAVE_EGL)
gboolean
cogl_texture_2d_gl_bind_egl_image (CoglTexture2D *tex_2d,
                                   EGLImageKHR    image,
                                   GError       **error)
{
  CoglContext *ctx = cogl_texture_get_context (COGL_TEXTURE (tex_2d));

  _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                   tex_2d->gl_texture);
  _cogl_gl_util_clear_gl_errors (ctx);

  ctx->glEGLImageTargetTexture2D (GL_TEXTURE_2D, image);
  if (_cogl_gl_util_get_error (ctx) != GL_NO_ERROR)
    {
      g_set_error_literal (error,
                           COGL_TEXTURE_ERROR,
                           COGL_TEXTURE_ERROR_BAD_PARAMETER,
                           "Could not bind the given EGLImage to a "
                           "CoglTexture2D");
      return FALSE;
    }

  return TRUE;
}

CoglTexture *
cogl_texture_2d_new_from_egl_image_external (CoglContext *ctx,
                                             int width,
                                             int height,
                                             CoglTexture2DEGLImageExternalAlloc alloc,
                                             gpointer user_data,
                                             GDestroyNotify destroy,
                                             GError **error)
{
  CoglTextureLoader *loader;
  CoglTexture2D *tex_2d;
  CoglPixelFormat internal_format = COGL_PIXEL_FORMAT_ANY;

  g_return_val_if_fail (_cogl_context_get_winsys (ctx)->constraints &
                        COGL_RENDERER_CONSTRAINT_USES_EGL,
                        NULL);

  g_return_val_if_fail (cogl_context_has_feature (ctx,
                                                  COGL_FEATURE_ID_TEXTURE_EGL_IMAGE_EXTERNAL),
                        NULL);

  loader = cogl_texture_loader_new (COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE_EXTERNAL);
  loader->src.egl_image_external.width = width;
  loader->src.egl_image_external.height = height;
  loader->src.egl_image_external.alloc = alloc;
  loader->src.egl_image_external.format = internal_format;

  tex_2d = COGL_TEXTURE_2D (_cogl_texture_2d_create_base (ctx, width, height,
                                                          internal_format, loader));


  tex_2d->egl_image_external.user_data = user_data;
  tex_2d->egl_image_external.destroy = destroy;

  return COGL_TEXTURE (tex_2d);
}
#endif /* defined (HAVE_EGL) */

void
_cogl_texture_2d_gl_flush_legacy_texobj_filters (CoglTexture *tex,
                                                 GLenum min_filter,
                                                 GLenum mag_filter)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglContext *ctx = cogl_texture_get_context (tex);

  if (min_filter == tex_2d->gl_legacy_texobj_min_filter
      && mag_filter == tex_2d->gl_legacy_texobj_mag_filter)
    return;

  /* Store new values */
  tex_2d->gl_legacy_texobj_min_filter = min_filter;
  tex_2d->gl_legacy_texobj_mag_filter = mag_filter;

  /* Apply new filters to the texture */
  _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                   tex_2d->gl_texture);
  GE( ctx, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter) );
  GE( ctx, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter) );

  if (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_TEXTURE_LOD_BIAS) &&
      min_filter != GL_NEAREST &&
      min_filter != GL_LINEAR)
    {
      GLfloat bias = _cogl_texture_min_filter_get_lod_bias (min_filter);

      GE (ctx, glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, bias));
    }
}

void
_cogl_texture_2d_gl_flush_legacy_texobj_wrap_modes (CoglTexture *tex,
                                                    GLenum wrap_mode_s,
                                                    GLenum wrap_mode_t)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglContext *ctx = cogl_texture_get_context (tex);

  /* Only set the wrap mode if it's different from the current value
     to avoid too many GL calls. Texture 2D doesn't make use of the r
     coordinate so we can ignore its wrap mode */
  if (tex_2d->gl_legacy_texobj_wrap_mode_s != wrap_mode_s ||
      tex_2d->gl_legacy_texobj_wrap_mode_t != wrap_mode_t)
    {
      _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                       tex_2d->gl_texture);
      GE( ctx, glTexParameteri (GL_TEXTURE_2D,
                                GL_TEXTURE_WRAP_S,
                                wrap_mode_s) );
      GE( ctx, glTexParameteri (GL_TEXTURE_2D,
                                GL_TEXTURE_WRAP_T,
                                wrap_mode_t) );

      tex_2d->gl_legacy_texobj_wrap_mode_s = wrap_mode_s;
      tex_2d->gl_legacy_texobj_wrap_mode_t = wrap_mode_t;
    }
}
