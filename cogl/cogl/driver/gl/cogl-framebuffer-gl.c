/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2012 Intel Corporation.
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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
 */

#include "cogl-config.h"

#include "cogl-context-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-framebuffer.h"
#include "cogl-offscreen-private.h"
#include "cogl-texture-private.h"
#include "driver/gl/cogl-util-gl-private.h"
#include "driver/gl/cogl-framebuffer-gl-private.h"
#include "driver/gl/cogl-bitmap-gl-private.h"
#include "driver/gl/cogl-buffer-gl-private.h"
#include "driver/gl/cogl-texture-gl-private.h"

#include <glib.h>
#include <string.h>

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER		0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER		0x8D41
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT	0x8D00
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0	0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_STENCIL_INDEX8
#define GL_STENCIL_INDEX8       0x8D48
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL        0x84F9
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8     0x88F0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT     0x8D00
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16    0x81A5
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE      0x8212
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE    0x8213
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE     0x8214
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE    0x8215
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE    0x8216
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE  0x8217
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER               0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#endif
#ifndef GL_TEXTURE_SAMPLES_IMG
#define GL_TEXTURE_SAMPLES_IMG            0x9136
#endif
#ifndef GL_PACK_INVERT_MESA
#define GL_PACK_INVERT_MESA 0x8758
#endif
#ifndef GL_PACK_REVERSE_ROW_ORDER_ANGLE
#define GL_PACK_REVERSE_ROW_ORDER_ANGLE 0x93A4
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT				0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT				0x0403
#endif

#ifndef GL_COLOR
#define GL_COLOR 0x1800
#endif
#ifndef GL_DEPTH
#define GL_DEPTH 0x1801
#endif
#ifndef GL_STENCIL
#define GL_STENCIL 0x1802
#endif

typedef struct _CoglGlFbo
{
  GLuint fbo_handle;
  GList *renderbuffers;
  int samples_per_pixel;
} CoglGlFbo;

typedef struct _CoglGlFramebufferPrivate
{
  CoglGlFbo gl_fbo;

  gboolean dirty_bitmasks;
  CoglFramebufferBits bits;
} CoglGlFramebufferPrivate;

struct _CoglGlFramebuffer
{
  CoglFramebufferDriver parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (CoglGlFramebuffer, cogl_gl_framebuffer,
                            COGL_TYPE_FRAMEBUFFER_DRIVER)

static void
_cogl_framebuffer_gl_flush_viewport_state (CoglFramebuffer *framebuffer)
{
  float viewport_x, viewport_y, viewport_width, viewport_height;
  float gl_viewport_y;

  cogl_framebuffer_get_viewport4f (framebuffer,
                                   &viewport_x,
                                   &viewport_y,
                                   &viewport_width,
                                   &viewport_height);

  g_return_if_fail (viewport_width >= 0);
  g_return_if_fail (viewport_height >= 0);

  /* Convert the Cogl viewport y offset to an OpenGL viewport y offset
   * NB: OpenGL defines its window and viewport origins to be bottom
   * left, while Cogl defines them to be top left.
   * NB: We render upside down to offscreen framebuffers so we don't
   * need to convert the y offset in this case. */
  if (COGL_IS_OFFSCREEN (framebuffer))
    gl_viewport_y = viewport_y;
  else
    gl_viewport_y =
      cogl_framebuffer_get_height (framebuffer) -
      (viewport_y + viewport_height);

  COGL_NOTE (OPENGL, "Calling glViewport(%f, %f, %f, %f)",
             viewport_x,
             gl_viewport_y,
             viewport_width,
             viewport_height);

  GE (cogl_framebuffer_get_context (framebuffer),
      glViewport (viewport_x,
                  gl_viewport_y,
                  viewport_width,
                  viewport_height));
}

static void
_cogl_framebuffer_gl_flush_clip_state (CoglFramebuffer *framebuffer)
{
  _cogl_clip_stack_flush (_cogl_framebuffer_get_clip_stack (framebuffer),
                          framebuffer);
}

static void
_cogl_framebuffer_gl_flush_dither_state (CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  gboolean is_dither_enabled =
    cogl_framebuffer_get_dither_enabled (framebuffer);

  if (ctx->current_gl_dither_enabled != is_dither_enabled)
    {
      if (is_dither_enabled)
        GE (ctx, glEnable (GL_DITHER));
      else
        GE (ctx, glDisable (GL_DITHER));
      ctx->current_gl_dither_enabled = is_dither_enabled;
    }
}

static void
_cogl_framebuffer_gl_flush_modelview_state (CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglMatrixEntry *modelview_entry =
    _cogl_framebuffer_get_modelview_entry (framebuffer);

  _cogl_context_set_current_modelview_entry (ctx, modelview_entry);
}

static void
_cogl_framebuffer_gl_flush_projection_state (CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglMatrixEntry *projection_entry =
    _cogl_framebuffer_get_projection_entry (framebuffer);

  _cogl_context_set_current_projection_entry (ctx, projection_entry);
}

static void
_cogl_framebuffer_gl_flush_front_face_winding_state (CoglFramebuffer *framebuffer)
{
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglPipelineCullFaceMode mode;

  /* NB: The face winding state is actually owned by the current
   * CoglPipeline.
   *
   * If we don't have a current pipeline then we can just assume that
   * when we later do flush a pipeline we will check the current
   * framebuffer to know how to setup the winding */
  if (!context->current_pipeline)
    return;

  mode = cogl_pipeline_get_cull_face_mode (context->current_pipeline);

  /* If the current CoglPipeline has a culling mode that doesn't care
   * about the winding we can avoid forcing an update of the state and
   * bail out. */
  if (mode == COGL_PIPELINE_CULL_FACE_MODE_NONE ||
      mode == COGL_PIPELINE_CULL_FACE_MODE_BOTH)
    return;

  /* Since the winding state is really owned by the current pipeline
   * the way we "flush" an updated winding is to dirty the pipeline
   * state... */
  context->current_pipeline_changes_since_flush |=
    COGL_PIPELINE_STATE_CULL_FACE;
  context->current_pipeline_age--;
}

static void
_cogl_framebuffer_gl_flush_stereo_mode_state (CoglFramebuffer *framebuffer)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  GLenum draw_buffer = GL_BACK;

  if (COGL_IS_OFFSCREEN (framebuffer))
    return;

  if (!ctx->glDrawBuffer)
    return;

  /* The one-shot default draw buffer setting in _cogl_framebuffer_gl_bind
   * must have already happened. If not it would override what we set here. */
  g_assert (ctx->was_bound_to_onscreen);

  switch (cogl_framebuffer_get_stereo_mode (framebuffer))
    {
    case COGL_STEREO_BOTH:
      draw_buffer = GL_BACK;
      break;
    case COGL_STEREO_LEFT:
      draw_buffer = GL_BACK_LEFT;
      break;
    case COGL_STEREO_RIGHT:
      draw_buffer = GL_BACK_RIGHT;
      break;
    }

  if (ctx->current_gl_draw_buffer != draw_buffer)
    {
      GE (ctx, glDrawBuffer (draw_buffer));
      ctx->current_gl_draw_buffer = draw_buffer;
    }
}

void
cogl_gl_framebuffer_flush_state_differences (CoglGlFramebuffer *gl_framebuffer,
                                             unsigned long      differences)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  int bit;

  COGL_FLAGS_FOREACH_START (&differences, 1, bit)
    {
      /* XXX: We considered having an array of callbacks for each state index
       * that we'd call here but decided that this way the compiler is more
       * likely going to be able to in-line the flush functions and use the
       * index to jump straight to the required code. */
      switch (bit)
        {
        case COGL_FRAMEBUFFER_STATE_INDEX_VIEWPORT:
          _cogl_framebuffer_gl_flush_viewport_state (framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_CLIP:
          _cogl_framebuffer_gl_flush_clip_state (framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_DITHER:
          _cogl_framebuffer_gl_flush_dither_state (framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_MODELVIEW:
          _cogl_framebuffer_gl_flush_modelview_state (framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_PROJECTION:
          _cogl_framebuffer_gl_flush_projection_state (framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_FRONT_FACE_WINDING:
          _cogl_framebuffer_gl_flush_front_face_winding_state (framebuffer);
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_DEPTH_WRITE:
          /* Nothing to do for depth write state change; the state will always
           * be taken into account when flushing the pipeline's depth state. */
          break;
        case COGL_FRAMEBUFFER_STATE_INDEX_STEREO_MODE:
          _cogl_framebuffer_gl_flush_stereo_mode_state (framebuffer);
          break;
        default:
          g_warn_if_reached ();
        }
    }
  COGL_FLAGS_FOREACH_END;
}

static CoglGlFramebuffer *
cogl_gl_framebuffer_from_framebuffer (CoglFramebuffer *framebuffer)
{
  CoglFramebufferDriver *driver = cogl_framebuffer_get_driver (framebuffer);

  g_assert (driver);

  return COGL_GL_FRAMEBUFFER (driver);
}

void
_cogl_framebuffer_gl_bind (CoglFramebuffer *framebuffer, GLenum target)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  if (COGL_IS_OFFSCREEN (framebuffer))
    {
      CoglGlFramebuffer *gl_framebuffer =
        cogl_gl_framebuffer_from_framebuffer (framebuffer);
      CoglGlFramebufferPrivate *priv =
        cogl_gl_framebuffer_get_instance_private (gl_framebuffer);

      GE (ctx, glBindFramebuffer (target, priv->gl_fbo.fbo_handle));
    }
  else
    {
      const CoglWinsysVtable *winsys =
        _cogl_framebuffer_get_winsys (framebuffer);
      winsys->onscreen_bind (COGL_ONSCREEN (framebuffer));
      GE (ctx, glBindFramebuffer (target, 0));

      /* Initialise the glDrawBuffer state the first time the context
       * is bound to the default framebuffer. If the winsys is using a
       * surfaceless context for the initial make current then the
       * default draw buffer will be GL_NONE so we need to correct
       * that. We can't do it any earlier because binding GL_BACK when
       * there is no default framebuffer won't work */
      if (!ctx->was_bound_to_onscreen)
        {
          if (ctx->glDrawBuffer)
            {
              GE (ctx, glDrawBuffer (GL_BACK));
            }
          else if (ctx->glDrawBuffers)
            {
              /* glDrawBuffer isn't available on GLES 3.0 so we need
               * to be able to use glDrawBuffers as well. On GLES 2
               * neither is available but the state should always be
               * GL_BACK anyway so we don't need to set anything. On
               * desktop GL this must be GL_BACK_LEFT instead of
               * GL_BACK but as this code path will only be hit for
               * GLES we can just use GL_BACK. */
              static const GLenum buffers[] = { GL_BACK };

              GE (ctx, glDrawBuffers (G_N_ELEMENTS (buffers), buffers));
            }

          ctx->was_bound_to_onscreen = TRUE;
        }
    }
}

static GList *
try_creating_renderbuffers (CoglContext *ctx,
                            int width,
                            int height,
                            CoglOffscreenAllocateFlags flags,
                            int n_samples)
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
delete_renderbuffers (CoglContext *ctx, GList *renderbuffers)
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
#ifdef HAVE_COGL_GL
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

CoglFramebufferDriver *
_cogl_driver_gl_create_framebuffer_driver (CoglContext                        *context,
                                           CoglFramebuffer                    *framebuffer,
                                           const CoglFramebufferDriverConfig  *driver_config,
                                           GError                            **error)
{
  CoglOffscreenAllocateFlags allocate_flags;
  CoglOffscreen *offscreen;
  CoglGlFramebuffer *gl_framebuffer;
  CoglGlFramebufferPrivate *priv;
  CoglGlFbo *gl_fbo;
  const CoglFramebufferConfig *config;
  CoglTexture *texture;
  int texture_level;
  int level_width;
  int level_height;

  if (COGL_IS_ONSCREEN (framebuffer))
    {
      return g_object_new (COGL_TYPE_GL_FRAMEBUFFER,
                           "framebuffer", framebuffer,
                           NULL);
    }

  offscreen = COGL_OFFSCREEN (framebuffer);
  texture = cogl_offscreen_get_texture (offscreen);
  texture_level = cogl_offscreen_get_texture_level (offscreen);

  g_return_val_if_fail (texture_level < _cogl_texture_get_n_levels (texture),
                        FALSE);

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

  gl_framebuffer = g_object_new (COGL_TYPE_GL_FRAMEBUFFER,
                                 "framebuffer", framebuffer,
                                 NULL);
  priv = cogl_gl_framebuffer_get_instance_private (gl_framebuffer);
  gl_fbo = &priv->gl_fbo;

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

      return COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
    }
  else
    {
      g_object_unref (gl_framebuffer);
      g_set_error (error, COGL_FRAMEBUFFER_ERROR,
                   COGL_FRAMEBUFFER_ERROR_ALLOCATE,
                   "Failed to create an OpenGL framebuffer object");
      return NULL;
    }
}

static void
cogl_gl_framebuffer_dispose (GObject *object)
{
  CoglGlFramebuffer *gl_framebuffer = COGL_GL_FRAMEBUFFER (object);
  CoglGlFramebufferPrivate *priv =
    cogl_gl_framebuffer_get_instance_private (gl_framebuffer);
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (object);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  G_OBJECT_CLASS (cogl_gl_framebuffer_parent_class)->dispose (object);

  delete_renderbuffers (ctx, priv->gl_fbo.renderbuffers);

  GE (ctx, glDeleteFramebuffers (1, &priv->gl_fbo.fbo_handle));
}

void
_cogl_framebuffer_gl_clear (CoglFramebuffer *framebuffer,
                            unsigned long buffers,
                            float red,
                            float green,
                            float blue,
                            float alpha)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  GLbitfield gl_buffers = 0;

  if (buffers & COGL_BUFFER_BIT_COLOR)
    {
      GE( ctx, glClearColor (red, green, blue, alpha) );
      gl_buffers |= GL_COLOR_BUFFER_BIT;
    }

  if (buffers & COGL_BUFFER_BIT_DEPTH)
    {
      gboolean is_depth_writing_enabled;

      gl_buffers |= GL_DEPTH_BUFFER_BIT;

      is_depth_writing_enabled =
        cogl_framebuffer_get_depth_write_enabled (framebuffer);
      if (ctx->depth_writing_enabled_cache != is_depth_writing_enabled)
        {
          GE( ctx, glDepthMask (is_depth_writing_enabled));

          ctx->depth_writing_enabled_cache = is_depth_writing_enabled;

          /* Make sure the DepthMask is updated when the next primitive is drawn */
          ctx->current_pipeline_changes_since_flush |=
            COGL_PIPELINE_STATE_DEPTH;
          ctx->current_pipeline_age--;
        }
    }

  if (buffers & COGL_BUFFER_BIT_STENCIL)
    gl_buffers |= GL_STENCIL_BUFFER_BIT;


  GE (ctx, glClear (gl_buffers));
}

static inline void
_cogl_framebuffer_init_bits (CoglFramebuffer *framebuffer)
{
  CoglGlFramebuffer *gl_framebuffer =
    cogl_gl_framebuffer_from_framebuffer (framebuffer);
  CoglGlFramebufferPrivate *priv =
    cogl_gl_framebuffer_get_instance_private (gl_framebuffer);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  if (!priv->dirty_bitmasks)
    return;

  cogl_framebuffer_allocate (framebuffer, NULL);

  cogl_context_flush_framebuffer_state (ctx,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);

#ifdef HAVE_COGL_GL
  if ((ctx->driver == COGL_DRIVER_GL3 &&
       COGL_IS_ONSCREEN (framebuffer)) ||
      (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_QUERY_FRAMEBUFFER_BITS) &&
       COGL_IS_OFFSCREEN (framebuffer)))
    {
      gboolean is_offscreen = COGL_IS_OFFSCREEN (framebuffer);
      const struct {
        GLenum attachment, pname;
        size_t offset;
      } params[] = {
        { is_offscreen ? GL_COLOR_ATTACHMENT0 : GL_BACK_LEFT,
          GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE,
          offsetof (CoglFramebufferBits, red) },
        { is_offscreen ? GL_COLOR_ATTACHMENT0 : GL_BACK_LEFT,
          GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE,
          offsetof (CoglFramebufferBits, green) },
        { is_offscreen ? GL_COLOR_ATTACHMENT0 : GL_BACK_LEFT,
          GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE,
          offsetof (CoglFramebufferBits, blue) },
        { is_offscreen ? GL_COLOR_ATTACHMENT0 : GL_BACK_LEFT,
          GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE,
          offsetof (CoglFramebufferBits, alpha) },
        { is_offscreen ? GL_DEPTH_ATTACHMENT : GL_DEPTH,
          GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,
          offsetof (CoglFramebufferBits, depth) },
        { is_offscreen ? GL_STENCIL_ATTACHMENT : GL_STENCIL,
          GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,
          offsetof (CoglFramebufferBits, stencil) },
      };
      int i;

      for (i = 0; i < G_N_ELEMENTS (params); i++)
        {
          int *value =
            (int *) ((uint8_t *) &priv->bits + params[i].offset);
          GE( ctx, glGetFramebufferAttachmentParameteriv (GL_FRAMEBUFFER,
                                                          params[i].attachment,
                                                          params[i].pname,
                                                          value) );
        }
    }
  else
#endif /* HAVE_COGL_GL */
    {
      GE (ctx, glGetIntegerv (GL_RED_BITS, &priv->bits.red));
      GE (ctx, glGetIntegerv (GL_GREEN_BITS, &priv->bits.green));
      GE (ctx, glGetIntegerv (GL_BLUE_BITS, &priv->bits.blue));
      GE (ctx, glGetIntegerv (GL_ALPHA_BITS, &priv->bits.alpha));
      GE (ctx, glGetIntegerv (GL_DEPTH_BITS, &priv->bits.depth));
      GE (ctx, glGetIntegerv (GL_STENCIL_BITS, &priv->bits.stencil));
    }

  /* If we don't have alpha textures then the alpha bits are actually
   * stored in the red component */
  if (!_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_ALPHA_TEXTURES) &&
      COGL_IS_OFFSCREEN (framebuffer) &&
      cogl_framebuffer_get_internal_format (framebuffer) == COGL_PIXEL_FORMAT_A_8)
    {
      priv->bits.alpha = priv->bits.red;
      priv->bits.red = 0;
    }

  COGL_NOTE (OFFSCREEN,
             "RGBA/D/S Bits for framebuffer[%p, %s]: %d, %d, %d, %d, %d, %d",
             framebuffer,
             COGL_IS_OFFSCREEN (framebuffer) ? "offscreen" : "onscreen",
             priv->bits.red,
             priv->bits.blue,
             priv->bits.green,
             priv->bits.alpha,
             priv->bits.depth,
             priv->bits.stencil);

  priv->dirty_bitmasks = FALSE;
}

void
_cogl_framebuffer_gl_query_bits (CoglFramebuffer *framebuffer,
                                 CoglFramebufferBits *bits)
{
  CoglGlFramebuffer *gl_framebuffer =
    cogl_gl_framebuffer_from_framebuffer (framebuffer);
  CoglGlFramebufferPrivate *priv =
    cogl_gl_framebuffer_get_instance_private (gl_framebuffer);

  _cogl_framebuffer_init_bits (framebuffer);

  /* TODO: cache these in some driver specific location not
   * directly as part of CoglFramebuffer. */
  *bits = priv->bits;
}

void
_cogl_framebuffer_gl_finish (CoglFramebuffer *framebuffer)
{
  GE (cogl_framebuffer_get_context (framebuffer), glFinish ());
}

void
_cogl_framebuffer_gl_flush (CoglFramebuffer *framebuffer)
{
  GE (cogl_framebuffer_get_context (framebuffer), glFlush ());
}

void
_cogl_framebuffer_gl_discard_buffers (CoglFramebuffer *framebuffer,
                                      unsigned long buffers)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  if (ctx->glDiscardFramebuffer)
    {
      GLenum attachments[3];
      int i = 0;

      if (COGL_IS_ONSCREEN (framebuffer))
        {
          if (buffers & COGL_BUFFER_BIT_COLOR)
            attachments[i++] = GL_COLOR;
          if (buffers & COGL_BUFFER_BIT_DEPTH)
            attachments[i++] = GL_DEPTH;
          if (buffers & COGL_BUFFER_BIT_STENCIL)
            attachments[i++] = GL_STENCIL;
        }
      else
        {
          if (buffers & COGL_BUFFER_BIT_COLOR)
            attachments[i++] = GL_COLOR_ATTACHMENT0;
          if (buffers & COGL_BUFFER_BIT_DEPTH)
            attachments[i++] = GL_DEPTH_ATTACHMENT;
          if (buffers & COGL_BUFFER_BIT_STENCIL)
            attachments[i++] = GL_STENCIL_ATTACHMENT;
        }

      cogl_context_flush_framebuffer_state (ctx,
                                            framebuffer,
                                            framebuffer,
                                            COGL_FRAMEBUFFER_STATE_BIND);
      GE (ctx, glDiscardFramebuffer (GL_FRAMEBUFFER, i, attachments));
    }
}

void
_cogl_framebuffer_gl_draw_attributes (CoglFramebuffer *framebuffer,
                                      CoglPipeline *pipeline,
                                      CoglVerticesMode mode,
                                      int first_vertex,
                                      int n_vertices,
                                      CoglAttribute **attributes,
                                      int n_attributes,
                                      CoglDrawFlags flags)
{
  _cogl_flush_attributes_state (framebuffer, pipeline, flags,
                                attributes, n_attributes);

  GE (cogl_framebuffer_get_context (framebuffer),
      glDrawArrays ((GLenum)mode, first_vertex, n_vertices));
}

static size_t
sizeof_index_type (CoglIndicesType type)
{
  switch (type)
    {
    case COGL_INDICES_TYPE_UNSIGNED_BYTE:
      return 1;
    case COGL_INDICES_TYPE_UNSIGNED_SHORT:
      return 2;
    case COGL_INDICES_TYPE_UNSIGNED_INT:
      return 4;
    }
  g_return_val_if_reached (0);
}

void
_cogl_framebuffer_gl_draw_indexed_attributes (CoglFramebuffer *framebuffer,
                                              CoglPipeline *pipeline,
                                              CoglVerticesMode mode,
                                              int first_vertex,
                                              int n_vertices,
                                              CoglIndices *indices,
                                              CoglAttribute **attributes,
                                              int n_attributes,
                                              CoglDrawFlags flags)
{
  CoglBuffer *buffer;
  uint8_t *base;
  size_t buffer_offset;
  size_t index_size;
  GLenum indices_gl_type = 0;

  _cogl_flush_attributes_state (framebuffer, pipeline, flags,
                                attributes, n_attributes);

  buffer = COGL_BUFFER (cogl_indices_get_buffer (indices));

  /* Note: we don't try and catch errors with binding the index buffer
   * here since OOM errors at this point indicate that nothing has yet
   * been uploaded to the indices buffer which we consider to be a
   * programmer error.
   */
  base = _cogl_buffer_gl_bind (buffer,
                               COGL_BUFFER_BIND_TARGET_INDEX_BUFFER, NULL);
  buffer_offset = cogl_indices_get_offset (indices);
  index_size = sizeof_index_type (cogl_indices_get_type (indices));

  switch (cogl_indices_get_type (indices))
    {
    case COGL_INDICES_TYPE_UNSIGNED_BYTE:
      indices_gl_type = GL_UNSIGNED_BYTE;
      break;
    case COGL_INDICES_TYPE_UNSIGNED_SHORT:
      indices_gl_type = GL_UNSIGNED_SHORT;
      break;
    case COGL_INDICES_TYPE_UNSIGNED_INT:
      indices_gl_type = GL_UNSIGNED_INT;
      break;
    }

  GE (cogl_framebuffer_get_context (framebuffer),
      glDrawElements ((GLenum)mode,
                      n_vertices,
                      indices_gl_type,
                      base + buffer_offset + index_size * first_vertex));

  _cogl_buffer_gl_unbind (buffer);
}

gboolean
_cogl_framebuffer_gl_read_pixels_into_bitmap (CoglFramebuffer *framebuffer,
                                              int x,
                                              int y,
                                              CoglReadPixelsFlags source,
                                              CoglBitmap *bitmap,
                                              GError **error)
{
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  int framebuffer_height = cogl_framebuffer_get_height (framebuffer);
  int width = cogl_bitmap_get_width (bitmap);
  int height = cogl_bitmap_get_height (bitmap);
  CoglPixelFormat format = cogl_bitmap_get_format (bitmap);
  CoglPixelFormat internal_format =
    cogl_framebuffer_get_internal_format (framebuffer);
  CoglPixelFormat required_format;
  GLenum gl_intformat;
  GLenum gl_format;
  GLenum gl_type;
  GLenum gl_pack_enum = GL_FALSE;
  gboolean pack_invert_set;
  int status = FALSE;

  g_return_val_if_fail (cogl_pixel_format_get_n_planes (format) == 1, FALSE);

  cogl_context_flush_framebuffer_state (ctx,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);

  /* The y coordinate should be given in OpenGL's coordinate system
   * so 0 is the bottom row
   *
   * NB: all offscreen rendering is done upside down so no conversion
   * is necissary in this case.
   */
  if (!COGL_IS_OFFSCREEN (framebuffer))
    y = framebuffer_height - y - height;

  required_format = ctx->driver_vtable->pixel_format_to_gl (ctx,
                                                            format,
                                                            &gl_intformat,
                                                            &gl_format,
                                                            &gl_type);

  /* NB: All offscreen rendering is done upside down so there is no need
   * to flip in this case... */
  if (_cogl_has_private_feature (ctx, COGL_PRIVATE_FEATURE_MESA_PACK_INVERT) &&
      (source & COGL_READ_PIXELS_NO_FLIP) == 0 &&
      !COGL_IS_OFFSCREEN (framebuffer))
    {
      if (ctx->driver == COGL_DRIVER_GLES2)
        gl_pack_enum = GL_PACK_REVERSE_ROW_ORDER_ANGLE;
      else
        gl_pack_enum = GL_PACK_INVERT_MESA;

      GE (ctx, glPixelStorei (gl_pack_enum, TRUE));
      pack_invert_set = TRUE;
    }
  else
    pack_invert_set = FALSE;

  /* Under GLES only GL_RGBA with GL_UNSIGNED_BYTE as well as an
     implementation specific format under
     GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES and
     GL_IMPLEMENTATION_COLOR_READ_TYPE_OES is supported. We could try
     to be more clever and check if the requested type matches that
     but we would need some reliable functions to convert from GL
     types to Cogl types. For now, lets just always read in
     GL_RGBA/GL_UNSIGNED_BYTE and convert if necessary. We also need
     to use this intermediate buffer if the rowstride has padding
     because GLES does not support setting GL_ROW_LENGTH */
  if ((!_cogl_has_private_feature
       (ctx, COGL_PRIVATE_FEATURE_READ_PIXELS_ANY_FORMAT) &&
       (gl_format != GL_RGBA || gl_type != GL_UNSIGNED_BYTE ||
        cogl_bitmap_get_rowstride (bitmap) != 4 * width)) ||
      (required_format & ~COGL_PREMULT_BIT) != (format & ~COGL_PREMULT_BIT))
    {
      CoglBitmap *tmp_bmp;
      CoglPixelFormat read_format;
      int bpp, rowstride;
      uint8_t *tmp_data;
      gboolean succeeded;

      if (_cogl_has_private_feature
          (ctx, COGL_PRIVATE_FEATURE_READ_PIXELS_ANY_FORMAT))
        read_format = required_format;
      else
        {
          read_format = COGL_PIXEL_FORMAT_RGBA_8888;
          gl_format = GL_RGBA;
          gl_type = GL_UNSIGNED_BYTE;
        }

      if (COGL_PIXEL_FORMAT_CAN_HAVE_PREMULT (read_format))
        {
          read_format = ((read_format & ~COGL_PREMULT_BIT) |
                         (internal_format & COGL_PREMULT_BIT));
        }

      tmp_bmp = _cogl_bitmap_new_with_malloc_buffer (ctx,
                                                     width, height,
                                                     read_format,
                                                     error);
      if (!tmp_bmp)
        goto EXIT;

      bpp = cogl_pixel_format_get_bytes_per_pixel (read_format, 0);
      rowstride = cogl_bitmap_get_rowstride (tmp_bmp);

      ctx->texture_driver->prep_gl_for_pixels_download (ctx,
                                                        rowstride,
                                                        width,
                                                        bpp);

      /* Note: we don't worry about catching errors here since we know
       * we won't be lazily allocating storage for this buffer so it
       * won't fail due to lack of memory. */
      tmp_data = _cogl_bitmap_gl_bind (tmp_bmp,
                                       COGL_BUFFER_ACCESS_WRITE,
                                       COGL_BUFFER_MAP_HINT_DISCARD,
                                       NULL);

      GE( ctx, glReadPixels (x, y, width, height,
                             gl_format, gl_type,
                             tmp_data) );

      _cogl_bitmap_gl_unbind (tmp_bmp);

      succeeded = _cogl_bitmap_convert_into_bitmap (tmp_bmp, bitmap, error);

      cogl_object_unref (tmp_bmp);

      if (!succeeded)
        goto EXIT;
    }
  else
    {
      CoglBitmap *shared_bmp;
      CoglPixelFormat bmp_format;
      int bpp, rowstride;
      gboolean succeeded = FALSE;
      uint8_t *pixels;
      GError *internal_error = NULL;

      rowstride = cogl_bitmap_get_rowstride (bitmap);

      /* We match the premultiplied state of the target buffer to the
       * premultiplied state of the framebuffer so that it will get
       * converted to the right format below */
      if (COGL_PIXEL_FORMAT_CAN_HAVE_PREMULT (format))
        bmp_format = ((format & ~COGL_PREMULT_BIT) |
                      (internal_format & COGL_PREMULT_BIT));
      else
        bmp_format = format;

      if (bmp_format != format)
        shared_bmp = _cogl_bitmap_new_shared (bitmap,
                                              bmp_format,
                                              width, height,
                                              rowstride);
      else
        shared_bmp = cogl_object_ref (bitmap);

      bpp = cogl_pixel_format_get_bytes_per_pixel (bmp_format, 0);

      ctx->texture_driver->prep_gl_for_pixels_download (ctx,
                                                        rowstride,
                                                        width,
                                                        bpp);

      pixels = _cogl_bitmap_gl_bind (shared_bmp,
                                     COGL_BUFFER_ACCESS_WRITE,
                                     0, /* hints */
                                     &internal_error);
      /* NB: _cogl_bitmap_gl_bind() can return NULL in successful
       * cases so we have to explicitly check the cogl error pointer
       * to know if there was a problem */
      if (internal_error)
        {
          cogl_object_unref (shared_bmp);
          g_propagate_error (error, internal_error);
          goto EXIT;
        }

      GE( ctx, glReadPixels (x, y,
                             width, height,
                             gl_format, gl_type,
                             pixels) );

      _cogl_bitmap_gl_unbind (shared_bmp);

      /* Convert to the premult format specified by the caller
         in-place. This will do nothing if the premult status is already
         correct. */
      if (_cogl_bitmap_convert_premult_status (shared_bmp, format, error))
        succeeded = TRUE;

      cogl_object_unref (shared_bmp);

      if (!succeeded)
        goto EXIT;
    }

  /* NB: All offscreen rendering is done upside down so there is no need
   * to flip in this case... */
  if (!COGL_IS_OFFSCREEN (framebuffer) &&
      (source & COGL_READ_PIXELS_NO_FLIP) == 0 &&
      !pack_invert_set)
    {
      uint8_t *temprow;
      int rowstride;
      uint8_t *pixels;

      rowstride = cogl_bitmap_get_rowstride (bitmap);
      pixels = _cogl_bitmap_map (bitmap,
                                 COGL_BUFFER_ACCESS_READ |
                                 COGL_BUFFER_ACCESS_WRITE,
                                 0, /* hints */
                                 error);

      if (pixels == NULL)
        goto EXIT;

      temprow = g_alloca (rowstride * sizeof (uint8_t));

      /* vertically flip the buffer in-place */
      for (y = 0; y < height / 2; y++)
        {
          if (y != height - y - 1) /* skip center row */
            {
              memcpy (temprow,
                      pixels + y * rowstride, rowstride);
              memcpy (pixels + y * rowstride,
                      pixels + (height - y - 1) * rowstride, rowstride);
              memcpy (pixels + (height - y - 1) * rowstride,
                      temprow,
                      rowstride);
            }
        }

      _cogl_bitmap_unmap (bitmap);
    }

  status = TRUE;

EXIT:

  /* Currently this function owns the pack_invert state and we don't want this
   * to interfere with other Cogl components so all other code can assume that
   * we leave the pack_invert state off. */
  if (pack_invert_set)
    GE (ctx, glPixelStorei (gl_pack_enum, FALSE));

  return status;
}

static void
cogl_gl_framebuffer_init (CoglGlFramebuffer *gl_framebuffer)
{
  CoglGlFramebufferPrivate *priv =
    cogl_gl_framebuffer_get_instance_private (gl_framebuffer);

  priv->dirty_bitmasks = TRUE;
}

static void
cogl_gl_framebuffer_class_init (CoglGlFramebufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = cogl_gl_framebuffer_dispose;
}
