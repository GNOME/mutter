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

#include "cogl/driver/gl/cogl-framebuffer-back-gl-private.h"

#include <gio/gio.h>

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-offscreen-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

struct _CoglFramebufferBackGL
{
  CoglFramebufferGL parent;

  gboolean dirty_bitmasks;
  CoglFramebufferBits bits;
};

G_DEFINE_FINAL_TYPE (CoglFramebufferBackGL, cogl_framebuffer_back_gl,
                     COGL_TYPE_FRAMEBUFFER_GL)

static gboolean
ensure_bits_initialized (CoglFramebufferBackGL *gl_framebuffer_back)
{
  CoglFramebufferDriver *driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_back);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglFramebufferBits *bits = &gl_framebuffer_back->bits;

  if (!gl_framebuffer_back->dirty_bitmasks)
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
    {
      return FALSE;
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
cogl_framebuffer_back_gl_query_bits (CoglFramebufferDriver *driver,
                                     CoglFramebufferBits   *bits)
{
  CoglFramebufferBackGL *gl_framebuffer_back = COGL_FRAMEBUFFER_BACK_GL (driver);

  if (!ensure_bits_initialized (gl_framebuffer_back))
    return;

  *bits = gl_framebuffer_back->bits;
}

static void
cogl_framebuffer_back_gl_discard_buffers (CoglFramebufferDriver *driver,
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
cogl_framebuffer_back_gl_bind (CoglFramebufferGL *gl_framebuffer,
                               GLenum             target)
{
  CoglFramebufferBackGL *gl_framebuffer_back =
    COGL_FRAMEBUFFER_BACK_GL (gl_framebuffer);
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

CoglFramebufferBackGL *
cogl_framebuffer_back_gl_new (CoglFramebuffer                    *framebuffer,
                              const CoglFramebufferDriverConfig  *driver_config,
                              GError                            **error)
{
  if (!COGL_IS_ONSCREEN (framebuffer))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Incompatible framebuffer");
      return NULL;
    }

  return g_object_new (COGL_TYPE_FRAMEBUFFER_BACK_GL,
                       "framebuffer", framebuffer,
                       NULL);
}

static void
cogl_framebuffer_back_gl_init (CoglFramebufferBackGL *framebuffer_back)
{
  framebuffer_back->dirty_bitmasks = TRUE;
}

static void
cogl_framebuffer_back_gl_class_init (CoglFramebufferBackGLClass *klass)
{
  CoglFramebufferDriverClass *driver_class =
    COGL_FRAMEBUFFER_DRIVER_CLASS (klass);
  CoglFramebufferGLClass *framebuffer_class =
    COGL_FRAMEBUFFER_GL_CLASS (klass);

  driver_class->query_bits = cogl_framebuffer_back_gl_query_bits;
  driver_class->discard_buffers = cogl_framebuffer_back_gl_discard_buffers;

  framebuffer_class->bind = cogl_framebuffer_back_gl_bind;
}
