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

#include "config.h"

#include "cogl/cogl-frame-info-private.h"
#include "cogl/cogl-context-private.h"

G_DEFINE_TYPE (CoglFrameInfo, cogl_frame_info, G_TYPE_OBJECT);

static void
cogl_frame_info_dispose (GObject *object)
{
  CoglFrameInfo *info = COGL_FRAME_INFO (object);

  if (info->timestamp_query)
    cogl_context_free_timestamp_query (info->context, info->timestamp_query);

  G_OBJECT_CLASS (cogl_frame_info_parent_class)->dispose (object);
}

static void
cogl_frame_info_init (CoglFrameInfo *info)
{
}

static void
cogl_frame_info_class_init (CoglFrameInfoClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_frame_info_dispose;
}

CoglFrameInfo *
cogl_frame_info_new (CoglContext *context,
                     int64_t      global_frame_counter)
{
  CoglFrameInfo *info;

  info = g_object_new (COGL_TYPE_FRAME_INFO, NULL);
  info->context = context;
  info->global_frame_counter = global_frame_counter;

  return info;
}

int64_t
cogl_frame_info_get_frame_counter (CoglFrameInfo *info)
{
  return info->frame_counter;
}

int64_t
cogl_frame_info_get_presentation_time_us (CoglFrameInfo *info)
{
  g_warn_if_fail (!(info->flags & COGL_FRAME_INFO_FLAG_SYMBOLIC));

  return info->presentation_time_us;
}

float
cogl_frame_info_get_refresh_rate (CoglFrameInfo *info)
{
  g_warn_if_fail (!(info->flags & COGL_FRAME_INFO_FLAG_SYMBOLIC));

  return info->refresh_rate;
}

int64_t
cogl_frame_info_get_global_frame_counter (CoglFrameInfo *info)
{
  return info->global_frame_counter;
}

gboolean
cogl_frame_info_get_is_symbolic (CoglFrameInfo *info)
{
  return !!(info->flags & COGL_FRAME_INFO_FLAG_SYMBOLIC);
}

gboolean
cogl_frame_info_is_hw_clock (CoglFrameInfo *info)
{
  return !!(info->flags & COGL_FRAME_INFO_FLAG_HW_CLOCK);
}

gboolean
cogl_frame_info_is_zero_copy (CoglFrameInfo *info)
{
  return !!(info->flags & COGL_FRAME_INFO_FLAG_ZERO_COPY);
}

gboolean
cogl_frame_info_is_vsync (CoglFrameInfo *info)
{
  return !!(info->flags & COGL_FRAME_INFO_FLAG_VSYNC);
}

unsigned int
cogl_frame_info_get_sequence (CoglFrameInfo *info)
{
  g_warn_if_fail (!(info->flags & COGL_FRAME_INFO_FLAG_SYMBOLIC));

  return info->sequence;
}

gboolean
cogl_frame_info_has_valid_gpu_rendering_duration (CoglFrameInfo *info)
{
  return info->has_valid_gpu_rendering_duration;
}

int64_t
cogl_frame_info_get_rendering_duration_ns (CoglFrameInfo *info)
{
  int64_t gpu_time_rendering_done_ns;

  if (!info->timestamp_query ||
      info->gpu_time_before_buffer_swap_ns == 0)
    return 0;

  gpu_time_rendering_done_ns =
    cogl_context_timestamp_query_get_time_ns (info->context,
                                              info->timestamp_query);

  return gpu_time_rendering_done_ns - info->gpu_time_before_buffer_swap_ns;
}

int64_t
cogl_frame_info_get_time_before_buffer_swap_us (CoglFrameInfo *info)
{
  return info->cpu_time_before_buffer_swap_us;
}

void
cogl_frame_info_set_target_presentation_time (CoglFrameInfo *info,
                                              int64_t        presentation_time_us)
{
  info->has_target_presentation_time = TRUE;
  info->target_presentation_time_us = presentation_time_us;
}
