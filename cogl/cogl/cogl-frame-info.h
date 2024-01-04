/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Red Hat, Inc.
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
 *
 * Authors:
 *   Owen Taylor <otaylor@redhat.com>
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-types.h"
#include "cogl/cogl-output.h"

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

/**
 * CoglFrameInfo:
 *
 * Frame information.
 */
typedef struct _CoglFrameInfo CoglFrameInfo;

#define COGL_TYPE_FRAME_INFO (cogl_frame_info_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglFrameInfo,
                      cogl_frame_info,
                      COGL,
                      FRAME_INFO,
                      GObject)

/**
 * cogl_frame_info_get_frame_counter:
 * @info: a #CoglFrameInfo object
 *
 * Gets the frame counter for the #CoglOnscreen that corresponds
 * to this frame.
 *
 * Return value: The frame counter value
 */
COGL_EXPORT
int64_t cogl_frame_info_get_frame_counter (CoglFrameInfo *info);

/**
 * cogl_frame_info_get_presentation_time_us:
 * @info: a #CoglFrameInfo object
 *
 * Gets the presentation time for the frame. This is the time at which
 * the frame became visible to the user.
 *
 * The presentation time measured in microseconds, is based on
 * CLOCK_MONOTONIC.
 *
 * Some buggy Mesa drivers up to 9.0.1 may
 * incorrectly report non-monotonic timestamps.
 *
 * Return value: the presentation time for the frame
 */
COGL_EXPORT
int64_t cogl_frame_info_get_presentation_time_us (CoglFrameInfo *info);

/**
 * cogl_frame_info_get_refresh_rate:
 * @info: a #CoglFrameInfo object
 *
 * Gets the refresh rate in Hertz for the output that the frame was on
 * at the time the frame was presented.
 *
 * Some platforms can't associate a #CoglOutput with a
 * #CoglFrameInfo object but are able to report a refresh rate via
 * this api. Therefore if you need this information then this api is
 * more reliable than using cogl_frame_info_get_output() followed by
 * cogl_output_get_refresh_rate().
 *
 * Return value: the refresh rate in Hertz
 */
COGL_EXPORT
float cogl_frame_info_get_refresh_rate (CoglFrameInfo *info);

/**
 * cogl_frame_info_get_global_frame_counter: (skip)
 */
COGL_EXPORT
int64_t cogl_frame_info_get_global_frame_counter (CoglFrameInfo *info);

COGL_EXPORT
gboolean cogl_frame_info_get_is_symbolic (CoglFrameInfo *info);

COGL_EXPORT
gboolean cogl_frame_info_is_hw_clock (CoglFrameInfo *info);

COGL_EXPORT
gboolean cogl_frame_info_is_zero_copy (CoglFrameInfo *info);

COGL_EXPORT
gboolean cogl_frame_info_is_vsync (CoglFrameInfo *info);

COGL_EXPORT
unsigned int cogl_frame_info_get_sequence (CoglFrameInfo *info);

COGL_EXPORT
int64_t cogl_frame_info_get_rendering_duration_ns (CoglFrameInfo *info);

COGL_EXPORT
int64_t cogl_frame_info_get_time_before_buffer_swap_us (CoglFrameInfo *info);

G_END_DECLS
