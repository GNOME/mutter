/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2024 Red Hat.
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
 */

#include "config.h"

#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-pipeline-gl-private.h"
#include "cogl/driver/gl/cogl-texture-driver-gl-private.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"
#include "cogl/cogl-texture-2d-private.h"

#if defined (HAVE_EGL)
/* We need this define from GLES2, but can't include the header
   as its type definitions may conflict with the GL ones
 */
#define GL_TEXTURE_EXTERNAL_OES           0x8D65
#endif /* defined (HAVE_EGL) */



G_DEFINE_TYPE (CoglTextureDriverGL, cogl_texture_driver_gl, COGL_TYPE_TEXTURE_DRIVER)

static void
cogl_texture_driver_gl_texture_2d_free (CoglTextureDriver *driver,
                                        CoglTexture2D     *tex_2d)
{
  if (tex_2d->gl_texture)
    _cogl_delete_gl_texture (cogl_texture_get_context (COGL_TEXTURE (tex_2d)),
                             cogl_texture_driver_get_driver (driver),
                             tex_2d->gl_texture);

#if defined (HAVE_EGL)
  g_clear_pointer (&tex_2d->egl_image_external.user_data,
                   tex_2d->egl_image_external.destroy);
#endif
}

static gboolean
cogl_texture_driver_gl_texture_2d_can_create (CoglTextureDriver *tex_driver,
                                              CoglContext       *ctx,
                                              int                width,
                                              int                height,
                                              CoglPixelFormat    internal_format)
{
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
  CoglDriverGLClass *driver_klass = COGL_DRIVER_GL_GET_CLASS (driver_gl);
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;

  /* We only support single plane formats for now */
  if (cogl_pixel_format_get_n_planes (internal_format) != 1)
    return FALSE;

  driver_klass->pixel_format_to_gl (driver_gl,
                                    ctx,
                                    internal_format,
                                    &gl_intformat,
                                    &gl_format,
                                    &gl_type);

  /* Check that the driver can create a texture with that size */
  if (!driver_klass->texture_size_supported (driver_gl,
                                             ctx,
                                             GL_TEXTURE_2D,
                                             gl_intformat,
                                             gl_format,
                                             gl_type,
                                             width,
                                             height))
    return FALSE;

  return TRUE;
}

static gboolean
allocate_with_size (CoglTexture2D     *tex_2d,
                    CoglTextureLoader *loader,
                    GError           **error)
{
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
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;
  GLenum gl_texture;

  internal_format =
    _cogl_texture_determine_internal_format (tex, loader->src.sized.format);

  if (!cogl_texture_driver_gl_texture_2d_can_create (tex_driver,
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
                                    ctx,
                                    internal_format,
                                    &gl_intformat,
                                    &gl_format,
                                    &gl_type);

  gl_texture = tex_driver_klass->gen (tex_driver_gl,
                                      ctx,
                                      GL_TEXTURE_2D,
                                      internal_format);

  tex_2d->gl_internal_format = gl_intformat;

  _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                   gl_texture);

  /* Clear any GL errors */
  _cogl_gl_util_clear_gl_errors (ctx);

  ctx->glTexImage2D (GL_TEXTURE_2D, 0, gl_intformat,
                     width, height, 0, gl_format, gl_type, NULL);

  if (_cogl_gl_util_catch_out_of_memory (ctx, error))
    {
      GE ( ctx, glDeleteTextures (1, &gl_texture) );
      return FALSE;
    }

  tex_2d->gl_texture = gl_texture;
  tex_2d->gl_internal_format = gl_intformat;

  tex_2d->internal_format = internal_format;

  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

static gboolean
allocate_from_bitmap (CoglTexture2D     *tex_2d,
                      CoglTextureLoader *loader,
                      GError           **error)
{
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
  CoglPixelFormat internal_format;
  int width = cogl_bitmap_get_width (bmp);
  int height = cogl_bitmap_get_height (bmp);
  CoglBitmap *upload_bmp;
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;

  internal_format =
    _cogl_texture_determine_internal_format (tex, cogl_bitmap_get_format (bmp));

  if (!cogl_texture_driver_gl_texture_2d_can_create (tex_driver,
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
                                    ctx,
                                    cogl_bitmap_get_format (upload_bmp),
                                    NULL, /* internal format */
                                    &gl_format,
                                    &gl_type);
  driver_klass->pixel_format_to_gl (driver_gl,
                                    ctx,
                                    internal_format,
                                    &gl_intformat,
                                    NULL,
                                    NULL);

  tex_2d->gl_texture = tex_driver_klass->gen (tex_driver_gl,
                                              ctx,
                                              GL_TEXTURE_2D,
                                              internal_format);
  if (!tex_driver_klass->upload_to_gl (tex_driver_gl,
                                       ctx,
                                       GL_TEXTURE_2D,
                                       tex_2d->gl_texture,
                                       upload_bmp,
                                       gl_intformat,
                                       gl_format,
                                       gl_type,
                                       error))
    {
      g_object_unref (upload_bmp);
      return FALSE;
    }

  tex_2d->gl_internal_format = gl_intformat;

  g_object_unref (upload_bmp);

  tex_2d->internal_format = internal_format;

  _cogl_texture_set_allocated (tex, internal_format, width, height);

  return TRUE;
}

#if defined (HAVE_EGL) && defined (EGL_KHR_image_base)
static gboolean
allocate_from_egl_image (CoglTexture2D     *tex_2d,
                         CoglTextureLoader *loader,
                         GError           **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglPixelFormat internal_format = loader->src.egl_image.format;
  CoglTextureDriver *tex_driver = cogl_texture_get_driver (tex);
  CoglTextureDriverGL *tex_driver_gl =
    COGL_TEXTURE_DRIVER_GL (tex_driver);
  CoglTextureDriverGLClass *tex_driver_klass =
    COGL_TEXTURE_DRIVER_GL_GET_CLASS (tex_driver_gl);

  tex_2d->gl_texture = tex_driver_klass->gen (tex_driver_gl,
                                              ctx,
                                              GL_TEXTURE_2D,
                                              internal_format);

  if (!cogl_texture_2d_gl_bind_egl_image (tex_2d,
                                          loader->src.egl_image.image,
                                          error))
    {
      GE ( ctx, glDeleteTextures (1, &tex_2d->gl_texture) );
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

#if defined (HAVE_EGL)
static gboolean
allocate_custom_egl_image_external (CoglTexture2D     *tex_2d,
                                    CoglTextureLoader *loader,
                                    GError           **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglPixelFormat external_format;
  CoglPixelFormat internal_format;

  external_format = loader->src.egl_image_external.format;
  internal_format = _cogl_texture_determine_internal_format (tex,
                                                             external_format);

  _cogl_gl_util_clear_gl_errors (ctx);

  GE (ctx, glActiveTexture (GL_TEXTURE0));
  GE (ctx, glGenTextures (1, &tex_2d->gl_texture));

  GE (ctx, glBindTexture (GL_TEXTURE_EXTERNAL_OES,
                          tex_2d->gl_texture));

  if (_cogl_gl_util_get_error (ctx) != GL_NO_ERROR)
    {
      g_set_error_literal (error,
                           COGL_TEXTURE_ERROR,
                           COGL_TEXTURE_ERROR_BAD_PARAMETER,
                           "Could not create a CoglTexture2D from a given "
                           "EGLImage");
      GE ( ctx, glDeleteTextures (1, &tex_2d->gl_texture) );
      return FALSE;
    }

  GE (ctx, glTexParameteri (GL_TEXTURE_EXTERNAL_OES,
                            GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
  GE (ctx, glTexParameteri (GL_TEXTURE_EXTERNAL_OES,
                            GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

  if (!loader->src.egl_image_external.alloc (tex_2d,
                                             tex_2d->egl_image_external.user_data,
                                             error))
    {
      GE (ctx, glBindTexture (GL_TEXTURE_EXTERNAL_OES, 0));
      GE (ctx, glDeleteTextures (1, &tex_2d->gl_texture));
      return FALSE;
    }

  GE (ctx, glBindTexture (GL_TEXTURE_EXTERNAL_OES, 0));

  tex_2d->internal_format = internal_format;
  tex_2d->gl_target = GL_TEXTURE_EXTERNAL_OES;
  tex_2d->is_get_data_supported = FALSE;

  return TRUE;
}
#endif

static gboolean
cogl_texture_driver_gl_texture_2d_allocate (CoglTextureDriver *driver,
                                            CoglTexture       *tex,
                                            GError           **error)
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
    case COGL_TEXTURE_SOURCE_TYPE_EGL_IMAGE_EXTERNAL:
#if defined (HAVE_EGL)
      return allocate_custom_egl_image_external (tex_2d, loader, error);
#else
      g_return_val_if_reached (FALSE);
#endif
    }

  g_return_val_if_reached (FALSE);
}

static void
cogl_texture_driver_gl_texture_2d_copy_from_framebuffer (CoglTextureDriver *driver,
                                                         CoglTexture2D     *tex_2d,
                                                         int                src_x,
                                                         int                src_y,
                                                         int                width,
                                                         int                height,
                                                         CoglFramebuffer   *src_fb,
                                                         int                dst_x,
                                                         int                dst_y,
                                                         int                level)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (tex);

  /* Make sure the current framebuffers are bound, though we don't need to
   * flush the clip state here since we aren't going to draw to the
   * framebuffer. */
  cogl_context_flush_framebuffer_state (ctx,
                                        ctx->current_draw_buffer,
                                        src_fb,
                                        (COGL_FRAMEBUFFER_STATE_ALL &
                                         ~COGL_FRAMEBUFFER_STATE_CLIP));

  _cogl_bind_gl_texture_transient (ctx, GL_TEXTURE_2D,
                                   tex_2d->gl_texture);

  ctx->glCopyTexSubImage2D (GL_TEXTURE_2D,
                            0, /* level */
                            dst_x, dst_y,
                            src_x, src_y,
                            width, height);
}

static void
cogl_texture_gl_set_max_level (CoglTexture *texture,
                               int          max_level)
{
  CoglContext *ctx = cogl_texture_get_context (texture);

  if (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_TEXTURE_MAX_LEVEL))
    {
      GLuint gl_handle;
      GLenum gl_target;

      cogl_texture_get_gl_texture (texture, &gl_handle, &gl_target);

      cogl_texture_set_max_level_set (texture, max_level);

      _cogl_bind_gl_texture_transient (ctx, gl_target,
                                       gl_handle);

      GE( ctx, glTexParameteri (gl_target,
                                GL_TEXTURE_MAX_LEVEL, cogl_texture_get_max_level_set (texture)));
    }
}

static void
cogl_texture_driver_gl_texture_2d_generate_mipmap (CoglTextureDriver *driver,
                                                   CoglTexture2D     *tex_2d)
{
  CoglTexture *texture = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (texture);
  int n_levels = _cogl_texture_get_n_levels (texture);
  GLuint gl_handle;
  GLenum gl_target;

  if (cogl_texture_get_max_level_set (texture) != n_levels - 1)
    cogl_texture_gl_set_max_level (texture, n_levels - 1);

  cogl_texture_get_gl_texture (texture, &gl_handle, &gl_target);

  _cogl_bind_gl_texture_transient (ctx, gl_target,
                                   gl_handle);
  GE( ctx, glGenerateMipmap (gl_target) );
}

static gboolean
cogl_texture_driver_gl_texture_2d_copy_from_bitmap (CoglTextureDriver *tex_driver,
                                                    CoglTexture2D     *tex_2d,
                                                    int                src_x,
                                                    int                src_y,
                                                    int                width,
                                                    int                height,
                                                    CoglBitmap        *bmp,
                                                    int                dst_x,
                                                    int                dst_y,
                                                    int                level,
                                                    GError           **error)
{
  CoglTexture *tex = COGL_TEXTURE (tex_2d);
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverGL *driver_gl = COGL_DRIVER_GL (driver);
  CoglDriverGLClass *driver_klass = COGL_DRIVER_GL_GET_CLASS (driver_gl);
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
                                    ctx,
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

static void
cogl_texture_driver_gl_class_init (CoglTextureDriverGLClass *klass)
{
  CoglTextureDriverClass *driver_klass = COGL_TEXTURE_DRIVER_CLASS (klass);

  driver_klass->texture_2d_free = cogl_texture_driver_gl_texture_2d_free;
  driver_klass->texture_2d_can_create = cogl_texture_driver_gl_texture_2d_can_create;
  driver_klass->texture_2d_allocate = cogl_texture_driver_gl_texture_2d_allocate;
  driver_klass->texture_2d_copy_from_framebuffer = cogl_texture_driver_gl_texture_2d_copy_from_framebuffer;
  driver_klass->texture_2d_generate_mipmap = cogl_texture_driver_gl_texture_2d_generate_mipmap;
  driver_klass->texture_2d_copy_from_bitmap = cogl_texture_driver_gl_texture_2d_copy_from_bitmap;
}

static void
cogl_texture_driver_gl_init (CoglTextureDriverGL *driver)
{
}
