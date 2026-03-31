/*
 * Copyright (C) 2015-2026 Red Hat Inc.
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

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-renderer.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "meta/boxes.h"

typedef enum _MetaStreamRecordFlag
{
  META_STREAM_RECORD_FLAG_NONE = 0,
  META_STREAM_RECORD_FLAG_CURSOR_ONLY = 1 << 0,
} MetaStreamRecordFlag;

typedef enum _MetaStreamRecordResult
{
  META_STREAM_RECORD_RESULT_RECORDED_NOTHING = 0,
  META_STREAM_RECORD_RESULT_RECORDED_FRAME = 1 << 0,
  META_STREAM_RECORD_RESULT_RECORDED_CURSOR = 1 << 1,
} MetaStreamRecordResult;

typedef enum _MetaStreamPaintPhase
{
  META_STREAM_PAINT_PHASE_DETACHED,
  META_STREAM_PAINT_PHASE_PRE_PAINT,
  META_STREAM_PAINT_PHASE_PRE_SWAP_BUFFER,
} MetaStreamPaintPhase;

typedef struct _MetaSpaDictEntry
{
  char *key;
  char *value;
} MetaSpaDictEntry;

typedef struct _MetaStreamFormat
{
  CoglPixelFormat format;
  ClutterColorState *color_state;
} MetaStreamFormat;

/* Declare some SPA types to avoid including the headers in too many places. */
struct spa_meta_cursor;
struct spa_video_info_raw;

#define META_TYPE_STREAM_SOURCE (meta_stream_source_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaStreamSource,
                          meta_stream_source,
                          META, STREAM_SOURCE,
                          GObject)

struct _MetaStreamSourceClass
{
  GObjectClass parent_class;

  gboolean (* get_specs) (MetaStreamSource *source,
                          int              *width,
                          int              *height,
                          float            *frame_rate);
  void (* enable) (MetaStreamSource *source);
  void (* disable) (MetaStreamSource *source);
  gboolean (* record_to_buffer) (MetaStreamSource      *source,
                                 MetaStreamRecordFlag   flags,
                                 MetaStreamPaintPhase   paint_phase,
                                 int                    width,
                                 int                    height,
                                 int                    stride,
                                 uint8_t               *data,
                                 MtkRegion             *damage,
                                 GError               **error);
  gboolean (* record_to_framebuffer) (MetaStreamSource      *source,
                                      MetaStreamPaintPhase   paint_phase,
                                      CoglFramebuffer       *framebuffer,
                                      MtkRegion             *damage,
                                      GError               **error);
  void (* queue_follow_up) (MetaStreamSource     *source,
                            MetaStreamRecordFlag  flags);

  gboolean (* get_videocrop) (MetaStreamSource *source,
                              MtkRectangle     *crop_rect);
  gboolean (* is_cursor_metadata_valid) (MetaStreamSource *source);
  void (* set_cursor_metadata) (MetaStreamSource       *source,
                                struct spa_meta_cursor *spa_meta_cursor);

  void (* notify_params_updated) (MetaStreamSource          *source,
                                  struct spa_video_info_raw *video_format);

  const MetaStreamFormat * (* get_formats) (MetaStreamSource *source);

  void (* dispatch) (MetaStreamSource *source);

  void (* append_tags) (MetaStreamSource *source,
                        GArray           *tags);
  void (* tag_changed) (MetaStreamSource *source,
                        const char       *key,
                        const char       *value);
};

void meta_stream_source_close (MetaStreamSource *source);

gboolean meta_stream_source_is_enabled (MetaStreamSource *source);

void meta_stream_source_accumulate_damage (MetaStreamSource     *source,
                                           MetaStreamRecordFlag  flags,
                                           const MtkRegion      *redraw_clip);

MetaStreamRecordResult meta_stream_source_maybe_record_frame (MetaStreamSource     *source,
                                                              MetaStreamRecordFlag  flags,
                                                              MetaStreamPaintPhase  paint_phase,
                                                              const MtkRegion      *redraw_clip);

MetaStreamRecordResult meta_stream_source_maybe_record_frame_with_timestamp (MetaStreamSource     *source,
                                                                             MetaStreamRecordFlag  flags,
                                                                             MetaStreamPaintPhase  paint_phase,
                                                                             const MtkRegion      *redraw_clip,
                                                                             int64_t               frame_timestamp_us);

MetaStreamRecordResult meta_stream_source_record_frame (MetaStreamSource     *source,
                                                        MetaStreamRecordFlag  flags,
                                                        MetaStreamPaintPhase  paint_phase,
                                                        const MtkRegion      *redraw_clip);

MetaStreamRecordResult meta_stream_source_record_frame_with_timestamp (MetaStreamSource     *source,
                                                                       MetaStreamRecordFlag  flags,
                                                                       MetaStreamPaintPhase  paint_phase,
                                                                       const MtkRegion      *redraw_clip,
                                                                       int64_t               frame_timestamp_us);

gboolean meta_stream_source_is_driving (MetaStreamSource *source);

void meta_stream_source_request_process (MetaStreamSource *source);

MetaStream * meta_stream_source_get_stream (MetaStreamSource *source);

gboolean meta_stream_source_paint_to_buffer (MetaStreamSource   *source,
                                             ClutterColorState  *color_state,
                                             CoglFramebuffer    *framebuffer,
                                             MtkRectangle       *area,
                                             float               scale,
                                             int                 width,
                                             int                 height,
                                             int                 stride,
                                             uint8_t            *data,
                                             CoglPixelFormat     format,
                                             MtkRegion          *damage,
                                             GError            **error);

gboolean meta_stream_source_draw_cursor_into (MetaStreamSource         *source,
                                              CoglTexture              *cursor_texture,
                                              int                       width,
                                              int                       height,
                                              const graphene_matrix_t  *matrix,
                                              uint8_t                  *data,
                                              GError                  **error);

void meta_stream_source_unset_cursor_metadata (MetaStreamSource       *source,
                                               struct spa_meta_cursor *spa_meta_cursor);

void meta_stream_source_set_cursor_position_metadata (MetaStreamSource       *source,
                                                      struct spa_meta_cursor *spa_meta_cursor,
                                                      int                     x,
                                                      int                     y);

void meta_stream_source_set_empty_cursor_sprite_metadata (MetaStreamSource       *source,
                                                          struct spa_meta_cursor *spa_meta_cursor,
                                                          int                     x,
                                                          int                     y);

void meta_stream_source_set_cursor_sprite_metadata (MetaStreamSource       *source,
                                                    struct spa_meta_cursor *spa_meta_cursor,
                                                    ClutterCursor          *cursor,
                                                    int                     x,
                                                    int                     y,
                                                    float                   view_scale);

gboolean meta_stream_source_uses_dma_bufs (MetaStreamSource *source);

const MetaStreamFormat * meta_stream_source_get_formats (MetaStreamSource *source);

void meta_stream_source_queue_empty_buffer (MetaStreamSource *source);

void meta_stream_source_renegotiate (MetaStreamSource *source);

ClutterColorState * meta_stream_source_get_color_state (MetaStreamSource *source);

uint32_t meta_stream_source_get_node_id (MetaStreamSource *source);
