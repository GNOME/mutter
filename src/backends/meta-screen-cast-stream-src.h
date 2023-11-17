/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015-2017 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <glib-object.h>
#include <spa/param/video/format-utils.h>
#include <spa/buffer/meta.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-cursor.h"
#include "backends/meta-renderer.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "meta/boxes.h"

typedef struct _MetaScreenCastStream MetaScreenCastStream;

typedef enum _MetaScreenCastRecordFlag
{
  META_SCREEN_CAST_RECORD_FLAG_NONE = 0,
  META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY = 1 << 0,
} MetaScreenCastRecordFlag;

typedef enum _MetaScreenCastRecordResult
{
  META_SCREEN_CAST_RECORD_RESULT_RECORDED_NOTHING = 0,
  META_SCREEN_CAST_RECORD_RESULT_RECORDED_FRAME = 1 << 0,
  META_SCREEN_CAST_RECORD_RESULT_RECORDED_CURSOR = 1 << 1,
} MetaScreenCastRecordResult;

typedef enum _MetaScreenCastPaintPhase
{
  META_SCREEN_CAST_PAINT_PHASE_DETACHED,
  META_SCREEN_CAST_PAINT_PHASE_PRE_PAINT,
  META_SCREEN_CAST_PAINT_PHASE_PRE_SWAP_BUFFER,
} MetaScreenCastPaintPhase;

#define META_TYPE_SCREEN_CAST_STREAM_SRC (meta_screen_cast_stream_src_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaScreenCastStreamSrc,
                          meta_screen_cast_stream_src,
                          META, SCREEN_CAST_STREAM_SRC,
                          GObject)

struct _MetaScreenCastStreamSrcClass
{
  GObjectClass parent_class;

  gboolean (* get_specs) (MetaScreenCastStreamSrc *src,
                          int                     *width,
                          int                     *height,
                          float                   *frame_rate);
  void (* enable) (MetaScreenCastStreamSrc *src);
  void (* disable) (MetaScreenCastStreamSrc *src);
  gboolean (* record_to_buffer) (MetaScreenCastStreamSrc  *src,
                                 MetaScreenCastPaintPhase  paint_phase,
                                 int                       width,
                                 int                       height,
                                 int                       stride,
                                 uint8_t                  *data,
                                 GError                  **error);
  gboolean (* record_to_framebuffer) (MetaScreenCastStreamSrc   *src,
                                      MetaScreenCastPaintPhase   paint_phase,
                                      CoglFramebuffer           *framebuffer,
                                      GError                   **error);
  void (* record_follow_up) (MetaScreenCastStreamSrc *src);

  gboolean (* get_videocrop) (MetaScreenCastStreamSrc *src,
                              MtkRectangle            *crop_rect);
  void (* set_cursor_metadata) (MetaScreenCastStreamSrc *src,
                                struct spa_meta_cursor  *spa_meta_cursor);

  void (* notify_params_updated) (MetaScreenCastStreamSrc   *src,
                                  struct spa_video_info_raw *video_format);

  CoglPixelFormat (* get_preferred_format) (MetaScreenCastStreamSrc *src);
};

void meta_screen_cast_stream_src_close (MetaScreenCastStreamSrc *src);

gboolean meta_screen_cast_stream_src_is_enabled (MetaScreenCastStreamSrc *src);

MetaScreenCastRecordResult meta_screen_cast_stream_src_maybe_record_frame (MetaScreenCastStreamSrc  *src,
                                                                           MetaScreenCastRecordFlag  flags,
                                                                           MetaScreenCastPaintPhase  paint_phase,
                                                                           const MtkRegion          *redraw_clip);

MetaScreenCastRecordResult meta_screen_cast_stream_src_maybe_record_frame_with_timestamp (MetaScreenCastStreamSrc  *src,
                                                                                          MetaScreenCastRecordFlag  flags,
                                                                                          MetaScreenCastPaintPhase  paint_phase,
                                                                                          const MtkRegion          *redraw_clip,
                                                                                          int64_t                   frame_timestamp_us);

gboolean meta_screen_cast_stream_src_pending_follow_up_frame (MetaScreenCastStreamSrc *src);

MetaScreenCastStream * meta_screen_cast_stream_src_get_stream (MetaScreenCastStreamSrc *src);

gboolean meta_screen_cast_stream_src_draw_cursor_into (MetaScreenCastStreamSrc  *src,
                                                       CoglTexture              *cursor_texture,
                                                       float                     scale,
                                                       MetaMonitorTransform      transform,
                                                       uint8_t                  *data,
                                                       GError                  **error);

void meta_screen_cast_stream_src_unset_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                        struct spa_meta_cursor  *spa_meta_cursor);

void meta_screen_cast_stream_src_set_cursor_position_metadata (MetaScreenCastStreamSrc *src,
                                                               struct spa_meta_cursor  *spa_meta_cursor,
                                                               int                      x,
                                                               int                      y);

void meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (MetaScreenCastStreamSrc *src,
                                                                   struct spa_meta_cursor  *spa_meta_cursor,
                                                                   int                      x,
                                                                   int                      y);

void meta_screen_cast_stream_src_set_cursor_sprite_metadata (MetaScreenCastStreamSrc *src,
                                                             struct spa_meta_cursor  *spa_meta_cursor,
                                                             MetaCursorSprite        *cursor_sprite,
                                                             int                      x,
                                                             int                      y,
                                                             float                    scale,
                                                             MetaMonitorTransform     transform);

gboolean meta_screen_cast_stream_src_uses_dma_bufs (MetaScreenCastStreamSrc *src);

CoglPixelFormat
meta_screen_cast_stream_src_get_preferred_format (MetaScreenCastStreamSrc *src);
