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
#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"

G_DEFINE_FINAL_TYPE (CoglTexture2DGL, cogl_texture_2d_gl, COGL_TYPE_TEXTURE_2D)

static void
cogl_texture_2d_gl_dispose (GObject *object)
{
  CoglTexture2DGL *tex_gl = COGL_TEXTURE_2D_GL (object);
  CoglTexture *tex = COGL_TEXTURE (object);
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglDriver *driver = cogl_texture_driver_get_driver (tex_driver);

  if (tex_gl->gl_texture)
    {
      CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
      CoglDriverGLPrivate *priv = cogl_driver_gl_get_private (driver_gl);

      for (int i = 0; i < priv->texture_units->len; i++)
        {
          CoglTextureUnit *unit =
            &g_array_index (priv->texture_units, CoglTextureUnit, i);

          if (unit->gl_texture == tex_gl->gl_texture)
            {
              unit->gl_texture = 0;
              unit->gl_target = 0;
              unit->dirty_gl_texture = FALSE;
            }
        }

      GE (driver, glDeleteTextures (1, &tex_gl->gl_texture));
    }

  G_OBJECT_CLASS (cogl_texture_2d_gl_parent_class)->dispose (object);
}

static void
cogl_texture_2d_gl_init (CoglTexture2DGL *self)
{
  self->gl_texture = 0;
  self->gl_target = GL_TEXTURE_2D;
  self->gl_legacy_texobj_min_filter = GL_LINEAR;
  self->gl_legacy_texobj_mag_filter = GL_LINEAR;
  self->gl_legacy_texobj_wrap_mode_s = GL_FALSE;
  self->gl_legacy_texobj_wrap_mode_t = GL_FALSE;
}

static void
cogl_texture_2d_gl_class_init (CoglTexture2DGLClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = cogl_texture_2d_gl_dispose;
}

#if defined (HAVE_EGL)
gboolean
cogl_texture_2d_gl_bind_egl_image (CoglTexture2D *tex_2d,
                                   EGLImageKHR    image,
                                   GError       **error)
{
  CoglTexture2DGL *tex_gl = COGL_TEXTURE_2D_GL (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (COGL_TEXTURE (tex_2d));
  CoglDriver *driver = cogl_context_get_driver (ctx);

  _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                   tex_gl->gl_texture);
  cogl_driver_gl_clear_gl_errors (COGL_DRIVER_GL (driver));

  GE (driver, glEGLImageTargetTexture2D (GL_TEXTURE_2D, image));
  if (cogl_driver_gl_get_gl_error (COGL_DRIVER_GL (driver)) != GL_NO_ERROR)
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
#endif /* defined (HAVE_EGL) */

typedef struct
{
  GLenum min_filter;
  GLenum mag_filter;
} FlushFiltersData;

static void
flush_legacy_texobj_filters_cb (CoglTexture2D *tex_2d,
                                void          *user_data)
{
  CoglTexture2DGL *tex_gl = COGL_TEXTURE_2D_GL (tex_2d);
  FlushFiltersData *d = user_data;
  CoglContext *ctx = cogl_texture_get_context (COGL_TEXTURE (tex_2d));
  CoglDriver *driver = cogl_context_get_driver (ctx);

  if (d->min_filter == tex_gl->gl_legacy_texobj_min_filter
      && d->mag_filter == tex_gl->gl_legacy_texobj_mag_filter)
    return;

  tex_gl->gl_legacy_texobj_min_filter = d->min_filter;
  tex_gl->gl_legacy_texobj_mag_filter = d->mag_filter;

  /* Apply new filters to the texture */
  _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                   tex_gl->gl_texture);
  GE (driver, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, d->mag_filter));
  GE (driver, glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, d->min_filter));

  if (cogl_driver_has_feature (driver, COGL_FEATURE_ID_TEXTURE_LOD_BIAS) &&
      d->min_filter != GL_NEAREST &&
      d->min_filter != GL_LINEAR)
    {
      GLfloat bias = _cogl_texture_min_filter_get_lod_bias (d->min_filter);

      GE (driver, glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, bias));
    }
}

void
_cogl_texture_gl_flush_legacy_texobj_filters (CoglTexture *tex,
                                              GLenum       min_filter,
                                              GLenum       mag_filter)
{
  FlushFiltersData d = { min_filter, mag_filter };

  cogl_texture_foreach_leaf (tex, flush_legacy_texobj_filters_cb, &d);
}

typedef struct
{
  GLenum wrap_mode_s;
  GLenum wrap_mode_t;
} FlushWrapData;

static void
flush_legacy_texobj_wrap_modes_cb (CoglTexture2D *tex_2d,
                                   void          *user_data)
{
  CoglTexture2DGL *tex_gl = COGL_TEXTURE_2D_GL (tex_2d);
  FlushWrapData *d = user_data;
  CoglContext *ctx = cogl_texture_get_context (COGL_TEXTURE (tex_2d));
  CoglDriver *driver = cogl_context_get_driver (ctx);


  /* Only set the wrap mode if it's different from the current value
     to avoid too many GL calls. Texture 2D doesn't make use of the r
     coordinate so we can ignore its wrap mode */
  if (tex_gl->gl_legacy_texobj_wrap_mode_s != d->wrap_mode_s ||
      tex_gl->gl_legacy_texobj_wrap_mode_t != d->wrap_mode_t)
    {
      _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                       tex_gl->gl_texture);
      GE (driver, glTexParameteri (GL_TEXTURE_2D,
                                   GL_TEXTURE_WRAP_S,
                                   d->wrap_mode_s));
      GE (driver, glTexParameteri (GL_TEXTURE_2D,
                                   GL_TEXTURE_WRAP_T,
                                   d->wrap_mode_t));

      tex_gl->gl_legacy_texobj_wrap_mode_s = d->wrap_mode_s;
      tex_gl->gl_legacy_texobj_wrap_mode_t = d->wrap_mode_t;
    }
}

void
_cogl_texture_gl_flush_legacy_texobj_wrap_modes (CoglTexture *tex,
                                                 GLenum       wrap_mode_s,
                                                 GLenum       wrap_mode_t)
{
  FlushWrapData d = { wrap_mode_s, wrap_mode_t };

  cogl_texture_foreach_leaf (tex, flush_legacy_texobj_wrap_modes_cb, &d);
}
