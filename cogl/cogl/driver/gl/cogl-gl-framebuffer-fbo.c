/*
 * Copyright (C) 2007,2008,2009,2012 Intel Corporation.
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
 * Copyright (C) 2020 Red Hat
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
 */

#include "config.h"

#include "cogl/driver/gl/cogl-gl-framebuffer-fbo.h"

#include <gio/gio.h>

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-offscreen-private.h"
#include "cogl/driver/gl/cogl-texture-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

typedef struct _CoglGlFbo
{
  GLuint fbo_handle;
  GList *renderbuffers;
  int samples_per_pixel;
} CoglGlFbo;

struct _CoglGlFramebufferFbo
{
  CoglGlFramebuffer parent;

  CoglGlFbo gl_fbo;

  gboolean dirty_bitmasks;
  CoglFramebufferBits bits;
};

G_DEFINE_TYPE (CoglGlFramebufferFbo, cogl_gl_framebuffer_fbo,
               COGL_TYPE_GL_FRAMEBUFFER)

static gboolean
ensure_bits_initialized (CoglGlFramebufferFbo *gl_framebuffer_fbo)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_fbo);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglFramebufferBits *bits = &gl_framebuffer_fbo->bits;
  g_autoptr (GError) error = NULL;

  if (!gl_framebuffer_fbo->dirty_bitmasks)
    return TRUE;

  cogl_context_flush_framebuffer_state (ctx,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);

  if (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_QUERY_FRAMEBUFFER_BITS))
    {
      const struct {
        GLenum attachment, pname;
        size_t offset;
      } params[] = {
        {
          .attachment = GL_COLOR_ATTACHMENT0,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE,
          .offset = offsetof (CoglFramebufferBits, red),
        },
        {
          .attachment = GL_COLOR_ATTACHMENT0,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE,
          .offset = offsetof (CoglFramebufferBits, green),
        },
        {
          .attachment = GL_COLOR_ATTACHMENT0,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE,
          .offset = offsetof (CoglFramebufferBits, blue),
        },
        {
          .attachment = GL_COLOR_ATTACHMENT0,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE,
          .offset = offsetof (CoglFramebufferBits, alpha),
        },
        {
          .attachment = GL_DEPTH_ATTACHMENT,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,
          .offset = offsetof (CoglFramebufferBits, depth),
        },
        {
          .attachment = GL_STENCIL_ATTACHMENT,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,
          .offset = offsetof (CoglFramebufferBits, stencil),
        },
      };
      int i;

      for (i = 0; i < G_N_ELEMENTS (params); i++)
        {
          int *value =
            (int *) ((uint8_t *) bits + params[i].offset);

          GE (ctx, glGetFramebufferAttachmentParameteriv (GL_FRAMEBUFFER,
                                                          params[i].attachment,
                                                          params[i].pname,
                                                          value));
        }
    }
  else
    {
      return FALSE;
    }

  if (!_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_ALPHA_TEXTURES) &&
      (cogl_framebuffer_get_internal_format (framebuffer) ==
       COGL_PIXEL_FORMAT_A_8))
    {
      bits->alpha = bits->red;
      bits->red = 0;
    }

  COGL_NOTE (FRAMEBUFFER,
             "RGBA/D/S Bits for framebuffer[%p, %s]: %d, %d, %d, %d, %d, %d",
             framebuffer,
             G_OBJECT_TYPE_NAME (framebuffer),
             bits->red,
             bits->blue,
             bits->green,
             bits->alpha,
             bits->depth,
             bits->stencil);

  gl_framebuffer_fbo->dirty_bitmasks = FALSE;

  return TRUE;
}

static void
cogl_gl_framebuffer_fbo_query_bits (CoglFramebufferDriver *driver,
                                    CoglFramebufferBits   *bits)
{
  CoglGlFramebufferFbo *gl_framebuffer_fbo = COGL_GL_FRAMEBUFFER_FBO (driver);

  if (!ensure_bits_initialized (gl_framebuffer_fbo))
    return;

  *bits = gl_framebuffer_fbo->bits;
}

static void
cogl_gl_framebuffer_fbo_discard_buffers (CoglFramebufferDriver *driver,
                                         unsigned long          buffers)
{
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  GLenum attachments[3];
  int i = 0;

  if (!ctx->glDiscardFramebuffer)
    return;

  if (buffers & COGL_BUFFER_BIT_COLOR)
    attachments[i++] = GL_COLOR_ATTACHMENT0;
  if (buffers & COGL_BUFFER_BIT_DEPTH)
    attachments[i++] = GL_DEPTH_ATTACHMENT;
  if (buffers & COGL_BUFFER_BIT_STENCIL)
    attachments[i++] = GL_STENCIL_ATTACHMENT;

  cogl_context_flush_framebuffer_state (ctx,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);
  GE (ctx, glDiscardFramebuffer (GL_FRAMEBUFFER, i, attachments));
}

static void
cogl_gl_framebuffer_fbo_bind (CoglGlFramebuffer *gl_framebuffer,
                              GLenum             target)
{
  CoglGlFramebufferFbo *gl_framebuffer_fbo =
    COGL_GL_FRAMEBUFFER_FBO (gl_framebuffer);
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_fbo);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  GE (ctx, glBindFramebuffer (target, gl_framebuffer_fbo->gl_fbo.fbo_handle));
}

static void
cogl_gl_framebuffer_fbo_flush_stereo_mode_state (CoglGlFramebuffer *gl_framebuffer)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);

  switch (cogl_framebuffer_get_stereo_mode (framebuffer))
    {
    case COGL_STEREO_BOTH:
      break;
    case COGL_STEREO_LEFT:
    case COGL_STEREO_RIGHT:
      g_warn_if_reached ();
      break;
    }
}

static GList *
try_creating_renderbuffers (CoglContext                *ctx,
                            int                         width,
                            int                         height,
                            CoglOffscreenAllocateFlags  flags,
                            int                         n_samples)
{
  GList *renderbuffers = NULL;
  GLuint gl_depth_stencil_handle;

  if (flags & COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH_STENCIL)
    {
      GLenum format;

      /* WebGL adds a GL_DEPTH_STENCIL_ATTACHMENT and requires that we
       * use the GL_DEPTH_STENCIL format. */
      /* Although GL_OES_packed_depth_stencil is mostly equivalent to
       * GL_EXT_packed_depth_stencil, one notable difference is that
       * GL_OES_packed_depth_stencil doesn't allow GL_DEPTH_STENCIL to
       * be passed as an internal format to glRenderbufferStorage.
       */
      if (_cogl_has_private_feature
          (ctx, COGL_PRIVATE_FEATURE_EXT_PACKED_DEPTH_STENCIL))
        format = GL_DEPTH_STENCIL;
      else
        {
          g_return_val_if_fail (
            _cogl_has_private_feature (ctx,
              COGL_PRIVATE_FEATURE_OES_PACKED_DEPTH_STENCIL),
            NULL);
          format = GL_DEPTH24_STENCIL8;
        }

      /* Create a renderbuffer for depth and stenciling */
      GE (ctx, glGenRenderbuffers (1, &gl_depth_stencil_handle));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, gl_depth_stencil_handle));
      if (n_samples)
        GE (ctx, glRenderbufferStorageMultisampleIMG (GL_RENDERBUFFER,
                                                      n_samples,
                                                      format,
                                                      width, height));
      else
        GE (ctx, glRenderbufferStorage (GL_RENDERBUFFER, format,
                                        width, height));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, 0));


      GE (ctx, glFramebufferRenderbuffer (GL_FRAMEBUFFER,
                                          GL_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER,
                                          gl_depth_stencil_handle));
      GE (ctx, glFramebufferRenderbuffer (GL_FRAMEBUFFER,
                                          GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER,
                                          gl_depth_stencil_handle));
      renderbuffers =
        g_list_prepend (renderbuffers,
                        GUINT_TO_POINTER (gl_depth_stencil_handle));
    }

  if (flags & COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH)
    {
      GLuint gl_depth_handle;

      GE (ctx, glGenRenderbuffers (1, &gl_depth_handle));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, gl_depth_handle));
      /* For now we just ask for GL_DEPTH_COMPONENT16 since this is all that's
       * available under GLES */
      if (n_samples)
        GE (ctx, glRenderbufferStorageMultisampleIMG (GL_RENDERBUFFER,
                                                      n_samples,
                                                      GL_DEPTH_COMPONENT16,
                                                      width, height));
      else
        GE (ctx, glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
                                        width, height));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, 0));
      GE (ctx, glFramebufferRenderbuffer (GL_FRAMEBUFFER,
                                          GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER, gl_depth_handle));
      renderbuffers =
        g_list_prepend (renderbuffers, GUINT_TO_POINTER (gl_depth_handle));
    }

  if (flags & COGL_OFFSCREEN_ALLOCATE_FLAG_STENCIL)
    {
      GLuint gl_stencil_handle;

      GE (ctx, glGenRenderbuffers (1, &gl_stencil_handle));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, gl_stencil_handle));
      if (n_samples)
        GE (ctx, glRenderbufferStorageMultisampleIMG (GL_RENDERBUFFER,
                                                      n_samples,
                                                      GL_STENCIL_INDEX8,
                                                      width, height));
      else
        GE (ctx, glRenderbufferStorage (GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                                        width, height));
      GE (ctx, glBindRenderbuffer (GL_RENDERBUFFER, 0));
      GE (ctx, glFramebufferRenderbuffer (GL_FRAMEBUFFER,
                                          GL_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, gl_stencil_handle));
      renderbuffers =
        g_list_prepend (renderbuffers, GUINT_TO_POINTER (gl_stencil_handle));
    }

  return renderbuffers;
}

static void
delete_renderbuffers (CoglContext *ctx,
                      GList       *renderbuffers)
{
  GList *l;

  for (l = renderbuffers; l; l = l->next)
    {
      GLuint renderbuffer = GPOINTER_TO_UINT (l->data);
      GE (ctx, glDeleteRenderbuffers (1, &renderbuffer));
    }

  g_list_free (renderbuffers);
}

/*
 * NB: This function may be called with a standalone GLES2 context
 * bound so we can create a shadow framebuffer that wraps the same
 * CoglTexture as the given CoglOffscreen. This function shouldn't
 * modify anything in
 */
static gboolean
try_creating_fbo (CoglContext                 *ctx,
                  CoglTexture                 *texture,
                  int                          texture_level,
                  int                          texture_level_width,
                  int                          texture_level_height,
                  const CoglFramebufferConfig *config,
                  CoglOffscreenAllocateFlags   flags,
                  CoglGlFbo                   *gl_fbo)
{
  GLuint tex_gl_handle;
  GLenum tex_gl_target;
  GLenum status;
  int n_samples;

  if (!cogl_texture_get_gl_texture (texture, &tex_gl_handle, &tex_gl_target))
    return FALSE;

  if (tex_gl_target != GL_TEXTURE_2D
#ifdef HAVE_GL
      && tex_gl_target != GL_TEXTURE_RECTANGLE_ARB
#endif
      )
    return FALSE;

  if (config->samples_per_pixel)
    {
      if (!ctx->glFramebufferTexture2DMultisampleIMG)
        return FALSE;
      n_samples = config->samples_per_pixel;
    }
  else
    n_samples = 0;

  /* We are about to generate and bind a new fbo, so we pretend to
   * change framebuffer state so that the old framebuffer will be
   * rebound again before drawing. */
  ctx->current_draw_buffer_changes |= COGL_FRAMEBUFFER_STATE_BIND;

  /* Generate framebuffer */
  ctx->glGenFramebuffers (1, &gl_fbo->fbo_handle);
  GE (ctx, glBindFramebuffer (GL_FRAMEBUFFER, gl_fbo->fbo_handle));

  if (n_samples)
    {
      GE (ctx, glFramebufferTexture2DMultisampleIMG (GL_FRAMEBUFFER,
                                                     GL_COLOR_ATTACHMENT0,
                                                     tex_gl_target, tex_gl_handle,
                                                     n_samples,
                                                     texture_level));
    }
  else
    GE (ctx, glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     tex_gl_target, tex_gl_handle,
                                     texture_level));

  if (flags)
    {
      gl_fbo->renderbuffers =
        try_creating_renderbuffers (ctx,
                                    texture_level_width,
                                    texture_level_height,
                                    flags,
                                    n_samples);
    }

  /* Make sure it's complete */
  status = ctx->glCheckFramebufferStatus (GL_FRAMEBUFFER);

  if (status != GL_FRAMEBUFFER_COMPLETE)
    {
      GE (ctx, glDeleteFramebuffers (1, &gl_fbo->fbo_handle));

      delete_renderbuffers (ctx, gl_fbo->renderbuffers);
      gl_fbo->renderbuffers = NULL;

      return FALSE;
    }

  /* Update the real number of samples_per_pixel now that we have a
   * complete framebuffer */
  if (n_samples)
    {
      GLenum attachment = GL_COLOR_ATTACHMENT0;
      GLenum pname = GL_TEXTURE_SAMPLES_IMG;
      int texture_samples;

      GE( ctx, glGetFramebufferAttachmentParameteriv (GL_FRAMEBUFFER,
                                                      attachment,
                                                      pname,
                                                      &texture_samples) );
      gl_fbo->samples_per_pixel = texture_samples;
    }

  return TRUE;
}

CoglGlFramebufferFbo *
cogl_gl_framebuffer_fbo_new (CoglFramebuffer                    *framebuffer,
                             const CoglFramebufferDriverConfig  *driver_config,
                             GError                            **error)
{
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglOffscreen *offscreen;
  CoglTexture *texture;
  int texture_level;
  int level_width;
  int level_height;
  const CoglFramebufferConfig *config;
  CoglGlFbo *gl_fbo;
  CoglGlFramebufferFbo *gl_framebuffer_fbo;
  CoglOffscreenAllocateFlags allocate_flags;

  if (!COGL_IS_OFFSCREEN (framebuffer))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Incompatible framebuffer");
      return NULL;
    }

  offscreen = COGL_OFFSCREEN (framebuffer);
  texture = cogl_offscreen_get_texture (offscreen);
  texture_level = cogl_offscreen_get_texture_level (offscreen);

  g_return_val_if_fail (texture_level < _cogl_texture_get_n_levels (texture),
                        NULL);

  _cogl_texture_get_level_size (texture,
                                texture_level,
                                &level_width,
                                &level_height,
                                NULL);

  /* XXX: The framebuffer_object spec isn't clear in defining whether attaching
   * a texture as a renderbuffer with mipmap filtering enabled while the
   * mipmaps have not been uploaded should result in an incomplete framebuffer
   * object. (different drivers make different decisions)
   *
   * To avoid an error with drivers that do consider this a problem we
   * explicitly set non mipmapped filters here. These will later be reset when
   * the texture is actually used for rendering according to the filters set on
   * the corresponding CoglPipeline.
   */
  _cogl_texture_gl_flush_legacy_texobj_filters (texture,
                                                GL_NEAREST, GL_NEAREST);

  config = cogl_framebuffer_get_config (framebuffer);

  gl_framebuffer_fbo = g_object_new (COGL_TYPE_GL_FRAMEBUFFER_FBO,
                                     "framebuffer", framebuffer,
                                     NULL);
  gl_fbo = &gl_framebuffer_fbo->gl_fbo;

  if ((driver_config->disable_depth_and_stencil &&
       try_creating_fbo (context,
                         texture,
                         texture_level,
                         level_width,
                         level_height,
                         config,
                         allocate_flags = 0,
                         gl_fbo)) ||

      (context->have_last_offscreen_allocate_flags &&
       try_creating_fbo (context,
                         texture,
                         texture_level,
                         level_width,
                         level_height,
                         config,
                         allocate_flags = context->last_offscreen_allocate_flags,
                         gl_fbo)) ||

      (
       /* NB: WebGL introduces a DEPTH_STENCIL_ATTACHMENT and doesn't
        * need an extension to handle _FLAG_DEPTH_STENCIL */
       (_cogl_has_private_feature
        (context, COGL_PRIVATE_FEATURE_EXT_PACKED_DEPTH_STENCIL) ||
        _cogl_has_private_feature
        (context, COGL_PRIVATE_FEATURE_OES_PACKED_DEPTH_STENCIL)) &&
       try_creating_fbo (context,
                         texture,
                         texture_level,
                         level_width,
                         level_height,
                         config,
                         allocate_flags = COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH_STENCIL,
                         gl_fbo)) ||

      try_creating_fbo (context,
                        texture,
                        texture_level,
                        level_width,
                        level_height,
                        config,
                        allocate_flags = COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH |
                        COGL_OFFSCREEN_ALLOCATE_FLAG_STENCIL,
                        gl_fbo) ||

      try_creating_fbo (context,
                        texture,
                        texture_level,
                        level_width,
                        level_height,
                        config,
                        allocate_flags = COGL_OFFSCREEN_ALLOCATE_FLAG_STENCIL,
                        gl_fbo) ||

      try_creating_fbo (context,
                        texture,
                        texture_level,
                        level_width,
                        level_height,
                        config,
                        allocate_flags = COGL_OFFSCREEN_ALLOCATE_FLAG_DEPTH,
                        gl_fbo) ||

      try_creating_fbo (context,
                        texture,
                        texture_level,
                        level_width,
                        level_height,
                        config,
                        allocate_flags = 0,
                        gl_fbo))
    {
      cogl_framebuffer_update_samples_per_pixel (framebuffer,
                                                 gl_fbo->samples_per_pixel);

      if (!driver_config->disable_depth_and_stencil)
        {
          /* Record that the last set of flags succeeded so that we can
             try that set first next time */
          context->last_offscreen_allocate_flags = allocate_flags;
          context->have_last_offscreen_allocate_flags = TRUE;
        }

      return gl_framebuffer_fbo;
    }
  else
    {
      g_object_unref (gl_framebuffer_fbo);
      g_set_error (error, COGL_FRAMEBUFFER_ERROR,
                   COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                   "Failed to create an OpenGL framebuffer object");
      return NULL;
    }
}

static void
cogl_gl_framebuffer_fbo_dispose (GObject *object)
{
  CoglGlFramebufferFbo *gl_framebuffer_fbo = COGL_GL_FRAMEBUFFER_FBO (object);
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_fbo);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  delete_renderbuffers (ctx, gl_framebuffer_fbo->gl_fbo.renderbuffers);
  gl_framebuffer_fbo->gl_fbo.renderbuffers = NULL;

  if (gl_framebuffer_fbo->gl_fbo.fbo_handle)
    {
      GE (ctx, glDeleteFramebuffers (1,
                                     &gl_framebuffer_fbo->gl_fbo.fbo_handle));
      gl_framebuffer_fbo->gl_fbo.fbo_handle = 0;
    }

  G_OBJECT_CLASS (cogl_gl_framebuffer_fbo_parent_class)->dispose (object);
}

static void
cogl_gl_framebuffer_fbo_init (CoglGlFramebufferFbo *gl_framebuffer_fbo)
{
  gl_framebuffer_fbo->dirty_bitmasks = TRUE;
}

static void
cogl_gl_framebuffer_fbo_class_init (CoglGlFramebufferFboClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CoglFramebufferDriverClass *driver_class =
    COGL_FRAMEBUFFER_DRIVER_CLASS (klass);
  CoglGlFramebufferClass *gl_framebuffer_class =
    COGL_GL_FRAMEBUFFER_CLASS (klass);

  object_class->dispose = cogl_gl_framebuffer_fbo_dispose;

  driver_class->query_bits = cogl_gl_framebuffer_fbo_query_bits;
  driver_class->discard_buffers = cogl_gl_framebuffer_fbo_discard_buffers;

  gl_framebuffer_class->bind = cogl_gl_framebuffer_fbo_bind;
  gl_framebuffer_class->flush_stereo_mode_state =
    cogl_gl_framebuffer_fbo_flush_stereo_mode_state;
}
