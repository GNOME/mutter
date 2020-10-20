/*
 * Copyright (C) 2007,2008,2009,2012 Intel Corporation.
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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

#ifndef COGL_FRAMEBUFFER_DRIVER_H
#define COGL_FRAMEBUFFER_DRIVER_H

#include "cogl-attribute-private.h"
#include "cogl-framebuffer.h"

typedef struct _CoglFramebufferBits CoglFramebufferBits;

#define COGL_TYPE_FRAMEBUFFER_DRIVER (cogl_framebuffer_driver_get_type ())
G_DECLARE_DERIVABLE_TYPE (CoglFramebufferDriver,
                          cogl_framebuffer_driver,
                          COGL, FRAMEBUFFER_DRIVER,
                          GObject)

struct _CoglFramebufferDriverClass
{
  GObjectClass parent_cleass;

  void (* query_bits) (CoglFramebufferDriver *driver,
                       CoglFramebufferBits   *bits);

  void (* clear) (CoglFramebufferDriver *driver,
                  unsigned long          buffers,
                  float                  red,
                  float                  green,
                  float                  blue,
                  float                  alpha);

  void (* finish) (CoglFramebufferDriver *driver);

  void (* flush) (CoglFramebufferDriver *driver);

  void (* discard_buffers) (CoglFramebufferDriver *driver,
                            unsigned long          buffers);

  void (* draw_attributes) (CoglFramebufferDriver  *driver,
                            CoglPipeline           *pipeline,
                            CoglVerticesMode        mode,
                            int                     first_vertex,
                            int                     n_vertices,
                            CoglAttribute         **attributes,
                            int                     n_attributes,
                            CoglDrawFlags           flags);

  void (* draw_indexed_attributes) (CoglFramebufferDriver  *driver,
                                    CoglPipeline           *pipeline,
                                    CoglVerticesMode        mode,
                                    int                     first_vertex,
                                    int                     n_vertices,
                                    CoglIndices            *indices,
                                    CoglAttribute         **attributes,
                                    int                     n_attributes,
                                    CoglDrawFlags           flags);

  gboolean (* read_pixels_into_bitmap) (CoglFramebufferDriver  *driver,
                                        int                     x,
                                        int                     y,
                                        CoglReadPixelsFlags     source,
                                        CoglBitmap             *bitmap,
                                        GError                **error);
};

CoglFramebuffer *
cogl_framebuffer_driver_get_framebuffer (CoglFramebufferDriver *driver);

void
cogl_framebuffer_driver_query_bits (CoglFramebufferDriver *driver,
                                    CoglFramebufferBits   *bits);

void
cogl_framebuffer_driver_clear (CoglFramebufferDriver *driver,
                               unsigned long          buffers,
                               float                  red,
                               float                  green,
                               float                  blue,
                               float                  alpha);

void
cogl_framebuffer_driver_finish (CoglFramebufferDriver *driver);

void
cogl_framebuffer_driver_flush (CoglFramebufferDriver *driver);

void
cogl_framebuffer_driver_discard_buffers (CoglFramebufferDriver *driver,
                                         unsigned long          buffers);

void
cogl_framebuffer_driver_draw_attributes (CoglFramebufferDriver  *driver,
                                         CoglPipeline           *pipeline,
                                         CoglVerticesMode        mode,
                                         int                     first_vertex,
                                         int                     n_vertices,
                                         CoglAttribute         **attributes,
                                         int                     n_attributes,
                                         CoglDrawFlags           flags);

void
cogl_framebuffer_driver_draw_indexed_attributes (CoglFramebufferDriver  *driver,
                                                 CoglPipeline           *pipeline,
                                                 CoglVerticesMode        mode,
                                                 int                     first_vertex,
                                                 int                     n_vertices,
                                                 CoglIndices            *indices,
                                                 CoglAttribute         **attributes,
                                                 int                     n_attributes,
                                                 CoglDrawFlags           flags);

gboolean
cogl_framebuffer_driver_read_pixels_into_bitmap (CoglFramebufferDriver  *driver,
                                                 int                     x,
                                                 int                     y,
                                                 CoglReadPixelsFlags     source,
                                                 CoglBitmap             *bitmap,
                                                 GError                **error);

#endif /* COGL_FRAMEBUFFER_DRIVER_H */
