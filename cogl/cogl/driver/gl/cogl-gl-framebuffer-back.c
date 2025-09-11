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

#include "cogl/driver/gl/cogl-gl-framebuffer-back.h"

#include <gio/gio.h>

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-offscreen-private.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"
#include "cogl/driver/gl/cogl-util-gl-private.h"

struct _CoglGlFramebufferBack
{
  CoglGlFramebuffer parent;

  gboolean dirty_bitmasks;
  CoglFramebufferBits bits;
};

G_DEFINE_FINAL_TYPE (CoglGlFramebufferBack, cogl_gl_framebuffer_back,
                     COGL_TYPE_GL_FRAMEBUFFER)

static gboolean
ensure_bits_initialized (CoglGlFramebufferBack *gl_framebuffer_back)
{
  CoglFramebufferDriver *fb_driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_back);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (fb_driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglDriver *driver = cogl_context_get_driver (ctx);
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

          GE (driver, glGetFramebufferAttachmentParameteriv (GL_FRAMEBUFFER,
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
cogl_gl_framebuffer_back_query_bits (CoglFramebufferDriver *driver,
                                     CoglFramebufferBits   *bits)
{
  CoglGlFramebufferBack *gl_framebuffer_back = COGL_GL_FRAMEBUFFER_BACK (driver);

  if (!ensure_bits_initialized (gl_framebuffer_back))
    return;

  *bits = gl_framebuffer_back->bits;
}

static void
cogl_gl_framebuffer_back_discard_buffers (CoglFramebufferDriver *fb_driver,
                                          unsigned long          buffers)
{
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (fb_driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglDriver *driver = cogl_context_get_driver (ctx);
  GLenum attachments[3];
  int i = 0;

  if (!GE_HAS (driver, glDiscardFramebuffer))
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
  GE (driver, glDiscardFramebuffer (GL_FRAMEBUFFER, i, attachments));
}

static void
cogl_gl_framebuffer_back_bind (CoglGlFramebuffer *gl_framebuffer,
                               GLenum             target)
{
  CoglGlFramebufferBack *gl_framebuffer_back =
    COGL_GL_FRAMEBUFFER_BACK (gl_framebuffer);
  CoglFramebufferDriver *fb_driver = COGL_FRAMEBUFFER_DRIVER (gl_framebuffer_back);
  CoglFramebuffer *framebuffer =
    cogl_framebuffer_driver_get_framebuffer (fb_driver);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglDriver *driver = cogl_context_get_driver (ctx);

  cogl_onscreen_bind (COGL_ONSCREEN (framebuffer));

  GE (driver, glBindFramebuffer (target, 0));

  /* Initialise the glDrawBuffer state the first time the context
   * is bound to the default framebuffer. If the winsys is using a
   * surfaceless context for the initial make current then the
   * default draw buffer will be GL_NONE so we need to correct
   * that. We can't do it any earlier because binding GL_BACK when
   * there is no default framebuffer won't work */
  if (!ctx->was_bound_to_onscreen)
    {
      if (GE_HAS (driver, glDrawBuffer))
        {
          GE (driver, glDrawBuffer (GL_BACK));
        }
      else if (GE_HAS (driver, glDrawBuffers))
        {
          /* glDrawBuffer isn't available on GLES 3.0 so we need
           * to be able to use glDrawBuffers as well. On GLES 2
           * neither is available but the state should always be
           * GL_BACK anyway so we don't need to set anything. On
           * desktop GL this must be GL_BACK_LEFT instead of
           * GL_BACK but as this code path will only be hit for
           * GLES we can just use GL_BACK. */
          static const GLenum buffers[] = { GL_BACK };

          GE (driver, glDrawBuffers (G_N_ELEMENTS (buffers), buffers));
        }

      ctx->was_bound_to_onscreen = TRUE;
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
}
