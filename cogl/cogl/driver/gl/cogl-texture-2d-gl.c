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
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-renderer.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"
#include "cogl/driver/gl/cogl-texture-driver-gl-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"

#if defined (HAVE_EGL)
#define GL_TEXTURE_EXTERNAL_OES           0x8D65
#endif /* defined (HAVE_EGL) */

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
cogl_texture_2d_gl_pre_paint (CoglTexture              *tex,
                              CoglTexturePrePaintFlags  flags)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  if ((flags & COGL_TEXTURE_NEEDS_MIPMAP) &&
      tex_2d->auto_mipmap && tex_2d->mipmaps_dirty)
    {
      CoglContext *ctx = cogl_texture_get_context (tex);
      CoglDriver *driver = cogl_context_get_driver (ctx);

      _cogl_texture_flush_journal_rendering (tex);

      if (cogl_driver_has_feature (driver, COGL_FEATURE_ID_QUIRK_GENERATE_MIPMAP_NEEDS_FLUSH) &&
          _cogl_texture_get_associated_framebuffers (tex))
        GE (driver, glFlush ());

      COGL_TEXTURE_2D_GET_CLASS (tex_2d)->generate_mipmap (tex_2d);

      tex_2d->mipmaps_dirty = FALSE;
    }
}

static gboolean
allocate_with_size (CoglTexture2D     *tex_2d,
                    CoglTextureLoader *loader,
                    GError           **error)
{
  CoglTexture2DGL *tex_gl = COGL_TEXTURE_2D_GL (tex_2d);
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglPixelFormat internal_format;
  int width = loader->src.sized.width;
  int height = loader->src.sized.height;
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
  CoglDriverGLClass *driver_klass = COGL_DRIVER_GL_GET_CLASS (driver_gl);
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglTextureDriverGL *tex_driver_gl =
    COGL_TEXTURE_DRIVER_GL (tex_driver);
  CoglTextureDriverGLClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GL_GET_CLASS (tex_driver_gl);
  CoglTextureDriverClass *tex_driver_class =
    COGL_TEXTURE_DRIVER_GET_CLASS (tex_driver);
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;
  GLenum gl_texture;

  internal_format =
    _cogl_texture_determine_internal_format (tex, loader->src.sized.format);

  if (!tex_driver_class->texture_2d_can_create (tex_driver,
                                                ctx,
                                                width,
                                                height,
                                                internal_format))
    {
      g_set_error_literal (error, COGL_TEXTURE_ERROR,
                           COGL_TEXTURE_ERROR_SIZE,
                           "Failed to create texture 2d due to size/format"
                           " constraints");
      return FALSE;
    }

  driver_klass->pixel_format_to_gl (driver_gl,
                                    internal_format,
                                    &gl_intformat,
                                    &gl_format,
                                    &gl_type);

  gl_texture = tex_driver_klass->gen (tex_driver_gl,
                                      ctx,
                                      GL_TEXTURE_2D,
                                      internal_format);

  tex_gl->gl_internal_format = gl_intformat;

  _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                   gl_texture);

  cogl_driver_gl_clear_gl_errors (COGL_DRIVER_GL (driver));

  GE (driver, glTexImage2D (GL_TEXTURE_2D, 0, gl_intformat,
                            width, height, 0, gl_format, gl_type, NULL));

  if (cogl_driver_gl_catch_out_of_memory (COGL_DRIVER_GL (driver), error))
    {
      GE (driver, glDeleteTextures (1, &gl_texture));
      return FALSE;
    }

  tex_gl->gl_texture = gl_texture;
  tex_gl->gl_internal_format = gl_intformat;

  tex_2d->internal_format = internal_format;
  tex_2d->is_get_data_supported =
    cogl_renderer_get_driver_id (cogl_context_get_renderer (ctx)) != COGL_DRIVER_ID_GLES2;

  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

static gboolean
allocate_from_bitmap (CoglTexture2D     *tex_2d,
                      CoglTextureLoader *loader,
                      GError           **error)
{
  CoglTexture2DGL *tex_gl = COGL_TEXTURE_2D_GL (tex_2d);
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglBitmap *bmp = loader->src.bitmap.bitmap;
  CoglContext *ctx = _cogl_bitmap_get_context (bmp);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
  CoglDriverGLClass *driver_klass = COGL_DRIVER_GL_GET_CLASS (driver_gl);
  CoglTextureDriverGL *tex_driver_gl =
    COGL_TEXTURE_DRIVER_GL (tex_driver);
  CoglTextureDriverGLClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GL_GET_CLASS (tex_driver_gl);
  CoglTextureDriverClass *tex_driver_class =
    COGL_TEXTURE_DRIVER_GET_CLASS (tex_driver);
  CoglPixelFormat internal_format;
  int width = cogl_bitmap_get_width (bmp);
  int height = cogl_bitmap_get_height (bmp);
  CoglBitmap *upload_bmp;
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;

  internal_format =
    _cogl_texture_determine_internal_format (tex, cogl_bitmap_get_format (bmp));

  if (!tex_driver_class->texture_2d_can_create (tex_driver,
                                                ctx,
                                                width,
                                                height,
                                                internal_format))
    {
      g_set_error_literal (error, COGL_TEXTURE_ERROR,
                           COGL_TEXTURE_ERROR_SIZE,
                           "Failed to create texture 2d due to size/format"
                           " constraints");
      return FALSE;
    }

  upload_bmp = _cogl_bitmap_convert_for_upload (bmp,
                                                internal_format,
                                                error);
  if (upload_bmp == NULL)
    return FALSE;

  driver_klass->pixel_format_to_gl (driver_gl,
                                    cogl_bitmap_get_format (upload_bmp),
                                    NULL, /* internal format */
                                    &gl_format,
                                    &gl_type);
  driver_klass->pixel_format_to_gl (driver_gl,
                                    internal_format,
                                    &gl_intformat,
                                    NULL,
                                    NULL);

  tex_gl->gl_texture = tex_driver_klass->gen (tex_driver_gl,
                                              ctx,
                                              GL_TEXTURE_2D,
                                              internal_format);
  if (!tex_driver_klass->upload_to_gl (tex_driver_gl,
                                       ctx,
                                       GL_TEXTURE_2D,
                                       tex_gl->gl_texture,
                                       upload_bmp,
                                       gl_intformat,
                                       gl_format,
                                       gl_type,
                                       error))
    {
      g_object_unref (upload_bmp);
      return FALSE;
    }

  tex_gl->gl_internal_format = gl_intformat;

  g_object_unref (upload_bmp);

  tex_2d->internal_format = internal_format;
  tex_2d->is_get_data_supported =
    cogl_renderer_get_driver_id (cogl_context_get_renderer (ctx)) != COGL_DRIVER_ID_GLES2;

  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

#if defined (HAVE_EGL) && defined (EGL_KHR_image_base)
static gboolean
allocate_from_egl_image (CoglTexture2D     *tex_2d,
                         CoglTextureLoader *loader,
                         GError           **error)
{
  CoglTexture2DGL *tex_gl = COGL_TEXTURE_2D_GL (tex_2d);
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglPixelFormat internal_format = loader->src.egl_image.format;
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglTextureDriverGL *tex_driver_gl =
    COGL_TEXTURE_DRIVER_GL (tex_driver);
  CoglTextureDriverGLClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GL_GET_CLASS (tex_driver_gl);

  tex_gl->gl_texture = tex_driver_klass->gen (tex_driver_gl,
                                              ctx,
                                              GL_TEXTURE_2D,
                                              internal_format);

  if (!cogl_texture_2d_gl_bind_egl_image (tex_2d,
                                          loader->src.egl_image.image,
                                          error))
    {
      CoglDriver *driver = cogl_context_get_driver (ctx);

      GE (driver, glDeleteTextures (1, &tex_gl->gl_texture));
      return FALSE;
    }

  tex_2d->internal_format = internal_format;
  tex_2d->is_get_data_supported =
    !(loader->src.egl_image.flags & COGL_EGL_IMAGE_FLAG_NO_GET_DATA);

  _cogl_texture_set_allocated (tex,
                               internal_format,
                               loader->src.egl_image.width,
                               loader->src.egl_image.height);

  return TRUE;
}
#endif

static gboolean
cogl_texture_2d_gl_allocate (CoglTexture *tex,
                             GError     **error)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);
  CoglTextureLoader *loader = cogl_texture_get_loader (tex);

  g_return_val_if_fail (loader, FALSE);

  switch (loader->src_type)
    {
    case COGL_TEXTURE_SOURCE_TYPE_SIZE:
      return allocate_with_size (tex_2d, loader, error);
    case COGL_TEXTURE_SOURCE_TYPE_BITMAP:
      return allocate_from_bitmap (tex_2d, loader, error);
    case COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE:
#if defined (HAVE_EGL) && defined (EGL_KHR_image_base)
      return allocate_from_egl_image (tex_2d, loader, error);
#else
      g_return_val_if_reached (FALSE);
#endif
    }

  g_return_val_if_reached (FALSE);
}

static void
cogl_texture_gl_set_max_level (CoglTexture *texture,
                               int          max_level)
{
  CoglContext *ctx = cogl_texture_get_context (texture);
  CoglDriver *driver = cogl_context_get_driver (ctx);

  if (cogl_driver_has_feature (driver, COGL_FEATURE_ID_TEXTURE_MAX_LEVEL))
    {
      GLuint gl_handle;
      GLenum gl_target;

      cogl_texture_get_gl_texture (texture, &gl_handle, &gl_target);

      cogl_texture_set_max_level_set (texture, max_level);

      _cogl_bind_gl_texture_transient (ctx, gl_target,
                                       gl_handle);

      GE (driver, glTexParameteri (gl_target,
                                   GL_TEXTURE_MAX_LEVEL,
                                   cogl_texture_get_max_level_set (texture)));
    }
}

static void
cogl_texture_2d_gl_generate_mipmap (CoglTexture2D *tex_2d)
{
  CoglTexture *texture = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (texture);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  int n_levels = _cogl_texture_get_n_levels (texture);
  GLuint gl_handle;
  GLenum gl_target;

  if (cogl_texture_get_max_level_set (texture) != n_levels - 1)
    cogl_texture_gl_set_max_level (texture, n_levels - 1);

  cogl_texture_get_gl_texture (texture, &gl_handle, &gl_target);

  _cogl_bind_gl_texture_transient (ctx, gl_target,
                                   gl_handle);
  GE (driver, glGenerateMipmap (gl_target));
}

static void
cogl_texture_2d_gl_copy_from_framebuffer (CoglTexture2D   *tex_2d,
                                          int              src_x,
                                          int              src_y,
                                          int              width,
                                          int              height,
                                          CoglFramebuffer *src_fb,
                                          int              dst_x,
                                          int              dst_y,
                                          int              level)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglDriver *driver = cogl_context_get_driver (ctx);

  cogl_context_flush_framebuffer_state (ctx,
                                        cogl_context_get_current_draw_buffer (ctx),
                                        src_fb,
                                        (COGL_FRAMEBUFFER_STATE_ALL &
                                         ~COGL_FRAMEBUFFER_STATE_CLIP));

  _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                   COGL_TEXTURE_2D_GL (tex_2d)->gl_texture);

  GE (driver, glCopyTexSubImage2D (GL_TEXTURE_2D,
                                   0, /* level */
                                   dst_x, dst_y,
                                   src_x, src_y,
                                   width, height));
}

static gboolean
cogl_texture_2d_gl_copy_from_bitmap (CoglTexture2D *tex_2d,
                                     int            src_x,
                                     int            src_y,
                                     int            width,
                                     int            height,
                                     CoglBitmap    *bmp,
                                     int            dst_x,
                                     int            dst_y,
                                     int            level,
                                     GError       **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
  CoglDriverGLClass *driver_klass = COGL_DRIVER_GL_GET_CLASS (driver_gl);
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglTextureDriverGL *tex_driver_gl =
    COGL_TEXTURE_DRIVER_GL (tex_driver);
  CoglTextureDriverGLClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GL_GET_CLASS (tex_driver_gl);
  CoglBitmap *upload_bmp;
  CoglPixelFormat upload_format;
  GLenum gl_format;
  GLenum gl_type;
  gboolean status = TRUE;

  upload_bmp =
    _cogl_bitmap_convert_for_upload (bmp,
                                     cogl_texture_get_format (tex),
                                     error);
  if (upload_bmp == NULL)
    return FALSE;

  upload_format = cogl_bitmap_get_format (upload_bmp);

  /* Only support single plane formats */
  if (upload_format == COGL_PIXEL_FORMAT_ANY ||
      cogl_pixel_format_get_n_planes (upload_format) != 1)
    return FALSE;

  driver_klass->pixel_format_to_gl (driver_gl,
                                    upload_format,
                                    NULL, /* internal gl format */
                                    &gl_format,
                                    &gl_type);

  if (cogl_texture_get_max_level_set (tex) < level)
    cogl_texture_gl_set_max_level (tex, level);

  status = tex_driver_klass->upload_subregion_to_gl (tex_driver_gl,
                                                     ctx,
                                                     tex,
                                                     src_x, src_y,
                                                     dst_x, dst_y,
                                                     width, height,
                                                     level,
                                                     upload_bmp,
                                                     gl_format,
                                                     gl_type,
                                                     error);

  g_object_unref (upload_bmp);

  return status;
}

static gboolean
cogl_texture_2d_gl_get_data (CoglTexture    *tex,
                             CoglPixelFormat format,
                             int             rowstride,
                             uint8_t        *data)
{
  CoglTexture2D *tex_2d = COGL_TEXTURE_2D (tex);

  if (!tex_2d->is_get_data_supported)
    return FALSE;

  CoglTexture2DGL *tex_gl = COGL_TEXTURE_2D_GL (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglTextureDriverGL *tex_driver_gl =
    COGL_TEXTURE_DRIVER_GL (tex_driver);
  CoglTextureDriverGLClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GL_GET_CLASS (tex_driver_gl);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
  CoglDriverGLClass *driver_klass = COGL_DRIVER_GL_GET_CLASS (driver_gl);
  uint8_t bpp;
  int width = cogl_texture_get_width (tex);
  GLenum gl_format;
  GLenum gl_type;

  g_return_val_if_fail (format != COGL_PIXEL_FORMAT_ANY, FALSE);
  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, FALSE);

  bpp = cogl_pixel_format_get_bytes_per_pixel (format, 0);

  driver_klass->pixel_format_to_gl (driver_gl,
                                    format,
                                    NULL, /* internal format */
                                    &gl_format,
                                    &gl_type);

  driver_klass->prep_gl_for_pixels_download (driver_gl,
                                             width,
                                             rowstride,
                                             bpp);

  _cogl_bind_gl_texture_transient (ctx, tex_gl->gl_target,
                                   tex_gl->gl_texture);

  tex_driver_klass->gl_get_tex_image (tex_driver_gl,
                                      ctx,
                                      tex_gl->gl_target,
                                      gl_format,
                                      gl_type,
                                      data);

  return TRUE;
}

static void
cogl_texture_2d_gl_class_init (CoglTexture2DGLClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  CoglTextureClass *texture_class = COGL_TEXTURE_CLASS (klass);
  CoglTexture2DClass *tex2d_class = COGL_TEXTURE_2D_CLASS (klass);

  gobject_class->dispose = cogl_texture_2d_gl_dispose;
  texture_class->pre_paint = cogl_texture_2d_gl_pre_paint;
  texture_class->allocate = cogl_texture_2d_gl_allocate;
  texture_class->get_data = cogl_texture_2d_gl_get_data;
  tex2d_class->copy_from_framebuffer = cogl_texture_2d_gl_copy_from_framebuffer;
  tex2d_class->generate_mipmap = cogl_texture_2d_gl_generate_mipmap;
  tex2d_class->copy_from_bitmap = cogl_texture_2d_gl_copy_from_bitmap;
}

#if defined (HAVE_EGL) && defined (EGL_KHR_image_base)
/* NB: The reason we require the width, height and format to be passed
 * even though they may seem redundant is because GLES 1/2 don't
 * provide a way to query these properties. */
CoglTexture *
cogl_texture_2d_new_from_egl_image (CoglContext *ctx,
                                    int width,
                                    int height,
                                    CoglPixelFormat format,
                                    EGLImageKHR image,
                                    CoglEglImageFlags flags,
                                    GError **error)
{
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglTextureLoader *loader;
  CoglTexture *tex;

  g_return_val_if_fail (cogl_driver_has_feature
                        (driver,
                        COGL_FEATURE_ID_TEXTURE_2D_FROM_EGL_IMAGE),
                        NULL);

  loader = cogl_texture_loader_new (COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE);
  loader->src.egl_image.image = image;
  loader->src.egl_image.width = width;
  loader->src.egl_image.height = height;
  loader->src.egl_image.format = format;
  loader->src.egl_image.flags = flags;

  tex = _cogl_texture_2d_create_base (ctx, width, height, format, loader);

  if (!cogl_texture_allocate (COGL_TEXTURE (tex), error))
    {
      g_object_unref (tex);
      return NULL;
    }

  return tex;
}
#endif /* defined (HAVE_EGL) && defined (EGL_KHR_image_base) */

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
