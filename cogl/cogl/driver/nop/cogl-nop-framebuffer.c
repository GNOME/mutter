/*
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

#include "cogl-nop-framebuffer.h"

#include "cogl-framebuffer-private.h"

struct _CoglNopFramebuffer
{
  CoglFramebufferDriver parent;
};

G_DEFINE_TYPE (CoglNopFramebuffer, cogl_nop_framebuffer,
               COGL_TYPE_FRAMEBUFFER_DRIVER)

static void
cogl_nop_framebuffer_query_bits (CoglFramebufferDriver *driver,
                                 CoglFramebufferBits   *bits)
{
  memset (bits, 0, sizeof (CoglFramebufferBits));
}

static void
cogl_nop_framebuffer_clear (CoglFramebufferDriver *driver,
                            unsigned long          buffers,
                            float                  red,
                            float                  green,
                            float                  blue,
                            float                  alpha)
{
}

static void
cogl_nop_framebuffer_finish (CoglFramebufferDriver *driver)
{
}

static void
cogl_nop_framebuffer_flush (CoglFramebufferDriver *driver)
{
}

static void
cogl_nop_framebuffer_discard_buffers (CoglFramebufferDriver *driver,
                                      unsigned long          buffers)
{
}

static void
cogl_nop_framebuffer_draw_attributes (CoglFramebufferDriver  *driver,
                                      CoglPipeline           *pipeline,
                                      CoglVerticesMode        mode,
                                      int                     first_vertex,
                                      int                     n_vertices,
                                      CoglAttribute         **attributes,
                                      int                     n_attributes,
                                      CoglDrawFlags           flags)
{
}

static void
cogl_nop_framebuffer_draw_indexed_attributes (CoglFramebufferDriver *driver,
                                              CoglPipeline          *pipeline,
                                              CoglVerticesMode       mode,
                                              int                    first_vertex,
                                              int                    n_vertices,
                                              CoglIndices           *indices,
                                              CoglAttribute        **attributes,
                                              int                    n_attributes,
                                              CoglDrawFlags          flags)
{
}

static gboolean
cogl_nop_framebuffer_read_pixels_into_bitmap (CoglFramebufferDriver  *framebuffer,
                                              int                     x,
                                              int                     y,
                                              CoglReadPixelsFlags     source,
                                              CoglBitmap             *bitmap,
                                              GError                **error)
{
  return TRUE;
}

static void
cogl_nop_framebuffer_init (CoglNopFramebuffer *nop_framebuffer)
{
}

static void
cogl_nop_framebuffer_class_init (CoglNopFramebufferClass *klass)
{
  CoglFramebufferDriverClass *driver_class =
    COGL_FRAMEBUFFER_DRIVER_CLASS (klass);

  driver_class->query_bits = cogl_nop_framebuffer_query_bits;
  driver_class->clear = cogl_nop_framebuffer_clear;
  driver_class->finish = cogl_nop_framebuffer_finish;
  driver_class->flush = cogl_nop_framebuffer_flush;
  driver_class->discard_buffers = cogl_nop_framebuffer_discard_buffers;
  driver_class->draw_attributes = cogl_nop_framebuffer_draw_attributes;
  driver_class->draw_indexed_attributes =
    cogl_nop_framebuffer_draw_indexed_attributes;
  driver_class->read_pixels_into_bitmap =
    cogl_nop_framebuffer_read_pixels_into_bitmap;
}
