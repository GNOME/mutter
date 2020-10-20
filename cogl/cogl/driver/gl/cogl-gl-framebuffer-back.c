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

#include "cogl-config.h"

#include "driver/gl/cogl-gl-framebuffer-back.h"

#include <gio/gio.h>

#include "cogl-context-private.h"
#include "cogl-framebuffer-private.h"
#include "cogl-offscreen-private.h"
#include "driver/gl/cogl-util-gl-private.h"

struct _CoglGlFramebufferBack
{
  CoglGlFramebuffer parent;

  gboolean dirty_bitmasks;
  CoglFramebufferBits bits;
};

G_DEFINE_TYPE (CoglGlFramebufferBack, cogl_gl_framebuffer_back,
               COGL_TYPE_GL_FRAMEBUFFER)

static gboolean
ensure_bits_initialized (CoglGlFramebufferBack *gl_framebuffer_back)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_back);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglFramebufferBits *bits = &gl_framebuffer_back->bits;
  g_autoptr (GError) error = NULL;

  if (!gl_framebuffer_back->dirty_bitmasks)
    return TRUE;

  cogl_context_flush_framebuffer_state (ctx,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);

#ifdef HAVE_COGL_GL
  if (ctx->driver == COGL_DRIVER_GL3)
    {
      const struct {
        GLenum attachment, pname;
        size_t offset;
      } params[] = {
        { 
          .attachment = GL_BACK_LEFT,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE,
          .offset = offsetof (CoglFramebufferBits, red),
        },
        { 
          .attachment = GL_BACK_LEFT,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE,
          .offset = offsetof (CoglFramebufferBits, green),
        },
        { 
          .attachment = GL_BACK_LEFT,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE,
          .offset = offsetof (CoglFramebufferBits, blue),
        },
        { 
          .attachment = GL_BACK_LEFT,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE,
          .offset = offsetof (CoglFramebufferBits, alpha),
        },
        { 
          .attachment = GL_DEPTH,
          .pname = GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,
          .offset = offsetof (CoglFramebufferBits, depth),
        },
        { 
          .attachment = GL_STENCIL,
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
#endif /* HAVE_COGL_GL */
    {
      GE (ctx, glGetIntegerv (GL_RED_BITS, &bits->red));
      GE (ctx, glGetIntegerv (GL_GREEN_BITS, &bits->green));
      GE (ctx, glGetIntegerv (GL_BLUE_BITS, &bits->blue));
      GE (ctx, glGetIntegerv (GL_ALPHA_BITS, &bits->alpha));
      GE (ctx, glGetIntegerv (GL_DEPTH_BITS, &bits->depth));
      GE (ctx, glGetIntegerv (GL_STENCIL_BITS, &bits->stencil));
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

  gl_framebuffer_back->dirty_bitmasks = FALSE;

  return TRUE;
}

static void
cogl_gl_framebuffer_back_query_bits (CoglFramebufferDriver *driver,
                                     CoglFramebufferBits   *bits)
{
  CoglGlFramebufferBack *gl_framebuffer_back = COGL_GL_FRAMEBUFFER_BACK (driver);

  if (!ensure_bits_initialized (gl_framebuffer_back))
    return;

  *bits = gl_framebuffer_back->bits;
}

static void
cogl_gl_framebuffer_back_discard_buffers (CoglFramebufferDriver *driver,
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
    attachments[i++] = GL_COLOR;
  if (buffers & COGL_BUFFER_BIT_DEPTH)
    attachments[i++] = GL_DEPTH;
  if (buffers & COGL_BUFFER_BIT_STENCIL)
    attachments[i++] = GL_STENCIL;

  cogl_context_flush_framebuffer_state (ctx,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);
  GE (ctx, glDiscardFramebuffer (GL_FRAMEBUFFER, i, attachments));
}

static void
cogl_gl_framebuffer_back_bind (CoglGlFramebuffer *gl_framebuffer,
                               GLenum             target)
{
  CoglGlFramebufferBack *gl_framebuffer_back =
    COGL_GL_FRAMEBUFFER_BACK (gl_framebuffer);
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_back);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);

  cogl_onscreen_bind (COGL_ONSCREEN (framebuffer));

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

static void
cogl_gl_framebuffer_back_flush_stereo_mode_state (CoglGlFramebuffer *gl_framebuffer)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  GLenum draw_buffer = GL_BACK;

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

CoglGlFramebufferBack *
cogl_gl_framebuffer_back_new (CoglFramebuffer                    *framebuffer,
                              const CoglFramebufferDriverConfig  *driver_config,
                              GError                            **error)
{
  if (!COGL_IS_ONSCREEN (framebuffer))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Incompatible framebuffer");
      return NULL;
    }

  return g_object_new (COGL_TYPE_GL_FRAMEBUFFER_BACK,
                       "framebuffer", framebuffer,
                       NULL);
}

static void
cogl_gl_framebuffer_back_init (CoglGlFramebufferBack *gl_framebuffer_back)
{
  gl_framebuffer_back->dirty_bitmasks = TRUE;
}

static void
cogl_gl_framebuffer_back_class_init (CoglGlFramebufferBackClass *klass)
{
  CoglFramebufferDriverClass *driver_class =
    COGL_FRAMEBUFFER_DRIVER_CLASS (klass);
  CoglGlFramebufferClass *gl_framebuffer_class =
    COGL_GL_FRAMEBUFFER_CLASS (klass);

  driver_class->query_bits = cogl_gl_framebuffer_back_query_bits;
  driver_class->discard_buffers = cogl_gl_framebuffer_back_discard_buffers;

  gl_framebuffer_class->bind = cogl_gl_framebuffer_back_bind;
  gl_framebuffer_class->flush_stereo_mode_state =
    cogl_gl_framebuffer_back_flush_stereo_mode_state;
}
