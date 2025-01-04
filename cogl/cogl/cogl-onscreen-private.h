/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011,2013 Intel Corporation.
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

#pragma once

#include "cogl/cogl-onscreen.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-list.h"
#include "mtk/mtk-rectangle.h"

#include <glib.h>

typedef struct _CoglOnscreenEvent
{
  CoglList link;

  CoglOnscreen *onscreen;
  CoglFrameInfo *info;
  CoglFrameEvent type;
} CoglOnscreenEvent;

typedef struct _CoglOnscreenQueuedDirty
{
  CoglList link;

  CoglOnscreen *onscreen;
  MtkRectangle info;
} CoglOnscreenQueuedDirty;

void
_cogl_framebuffer_winsys_update_size (CoglFramebuffer *framebuffer,
                                      int width, int height);

COGL_EXPORT void
_cogl_onscreen_notify_frame_sync (CoglOnscreen *onscreen, CoglFrameInfo *info);

COGL_EXPORT void
_cogl_onscreen_notify_complete (CoglOnscreen *onscreen, CoglFrameInfo *info);

void
_cogl_onscreen_queue_dirty (CoglOnscreen       *onscreen,
                            const MtkRectangle *info);

void
cogl_onscreen_bind (CoglOnscreen *onscreen);

COGL_EXPORT CoglFrameInfo *
cogl_onscreen_peek_head_frame_info (CoglOnscreen *onscreen);

COGL_EXPORT CoglFrameInfo *
cogl_onscreen_peek_tail_frame_info (CoglOnscreen *onscreen);

COGL_EXPORT CoglFrameInfo *
cogl_onscreen_pop_head_frame_info (CoglOnscreen *onscreen);
