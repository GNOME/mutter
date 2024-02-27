/*
 * Copyright (C) 2019 Red Hat Inc.
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
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "cogl/cogl-types.h"
#include "cogl/cogl-framebuffer.h"
#include "cogl/cogl-onscreen.h"
#include "mtk/mtk.h"

#include <glib-object.h>

#define COGL_TYPE_SCANOUT (cogl_scanout_get_type ())
COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglScanout, cogl_scanout,
                      COGL, SCANOUT, GObject)

#define COGL_TYPE_SCANOUT_BUFFER (cogl_scanout_buffer_get_type ())
COGL_EXPORT
G_DECLARE_INTERFACE (CoglScanoutBuffer, cogl_scanout_buffer,
                     COGL, SCANOUT_BUFFER, GObject)

struct _CoglScanoutBufferInterface
{
  GTypeInterface parent_iface;

  gboolean (*blit_to_framebuffer) (CoglScanout      *scanout,
                                   CoglFramebuffer  *framebuffer,
                                   int               x,
                                   int               y,
                                   GError          **error);

  int (*get_width) (CoglScanoutBuffer *scanout_buffer);
  int (*get_height) (CoglScanoutBuffer *scanout_buffer);
};

COGL_EXPORT
gboolean cogl_scanout_blit_to_framebuffer (CoglScanout      *scanout,
                                           CoglFramebuffer  *framebuffer,
                                           int               x,
                                           int               y,
                                           GError          **error);

int cogl_scanout_buffer_get_width (CoglScanoutBuffer *scanout_buffer);
int cogl_scanout_buffer_get_height (CoglScanoutBuffer *scanout_buffer);

/**
 * cogl_scanout_get_buffer:
 *
 * Returns: (transfer none): a #CoglScanoutBuffer
 */
COGL_EXPORT
CoglScanoutBuffer * cogl_scanout_get_buffer (CoglScanout *scanout);

COGL_EXPORT
void cogl_scanout_notify_failed (CoglScanout  *scanout,
                                 CoglOnscreen *onscreen);

COGL_EXPORT
CoglScanout * cogl_scanout_new (CoglScanoutBuffer *scanout_buffer);

COGL_EXPORT
void cogl_scanout_get_src_rect (CoglScanout     *scanout,
                                graphene_rect_t *rect);

COGL_EXPORT
void cogl_scanout_set_src_rect (CoglScanout           *scanout,
                                const graphene_rect_t *rect);

COGL_EXPORT
void cogl_scanout_get_dst_rect (CoglScanout  *scanout,
                                MtkRectangle *rect);

COGL_EXPORT
void cogl_scanout_set_dst_rect (CoglScanout        *scanout,
                                const MtkRectangle *rect);
