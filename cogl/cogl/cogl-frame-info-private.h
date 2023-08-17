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
 */

#pragma once

#include "cogl/cogl-frame-info.h"
#include "cogl/cogl-context.h"

typedef enum _CoglFrameInfoFlag
{
  COGL_FRAME_INFO_FLAG_NONE = 0,
  COGL_FRAME_INFO_FLAG_SYMBOLIC = 1 << 0,
  /* presentation_time timestamp was provided by the hardware */
  COGL_FRAME_INFO_FLAG_HW_CLOCK = 1 << 1,
  /*
   * The presentation of this frame was done zero-copy. This means the buffer
   * from the client was given to display hardware as is, without copying it.
   * Compositing with OpenGL counts as copying, even if textured directly from
   * the client buffer. Possible zero-copy cases include direct scanout of a
   * fullscreen surface and a surface on a hardware overlay.
   */
  COGL_FRAME_INFO_FLAG_ZERO_COPY = 1 << 2,
  /*
   * The presentation was synchronized to the "vertical retrace" by the display
   * hardware such that tearing does not happen. Relying on user space
   * scheduling is not acceptable for this flag. If presentation is done by a
   * copy to the active frontbuffer, then it must guarantee that tearing cannot
   * happen.
   */
  COGL_FRAME_INFO_FLAG_VSYNC = 1 << 3,
} CoglFrameInfoFlag;

struct _CoglFrameInfo
{
  GObject parent_instance;

  CoglContext *context;

  int64_t frame_counter;
  int64_t presentation_time_us; /* CLOCK_MONOTONIC */
  float refresh_rate;

  int64_t global_frame_counter;

  CoglFrameInfoFlag flags;

  unsigned int sequence;

  CoglTimestampQuery *timestamp_query;
  int64_t gpu_time_before_buffer_swap_ns;
  int64_t cpu_time_before_buffer_swap_us;

  gboolean has_target_presentation_time;
  int64_t target_presentation_time_us;
};

COGL_EXPORT
CoglFrameInfo *cogl_frame_info_new (CoglContext *context,
                                    int64_t      global_frame_counter);

COGL_EXPORT
void cogl_frame_info_set_target_presentation_time (CoglFrameInfo *info,
                                                   int64_t        presentation_time_us);
