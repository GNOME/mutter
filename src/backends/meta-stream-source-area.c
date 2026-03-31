/*
 * Copyright (C) 2020-2026 Red Hat Inc.
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

#include "config.h"

#include "backends/meta-stream-source-area.h"

#include <spa/buffer/meta.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-stream-area.h"
#include "backends/meta-stage-private.h"
#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"
#include "core/boxes-private.h"

struct _MetaStreamSourceArea
{
  MetaStreamSource parent;

  gboolean cursor_bitmap_invalid;
  gboolean hw_cursor_inhibited;

  struct {
    gboolean set;
    int x;
    int y;
  } last_cursor_matadata;

  GList *watches;

  gulong position_invalidated_handler_id;
  gulong cursor_changed_handler_id;
  gulong prepare_frame_handler_id;

  guint maybe_record_idle_id;
};

static void hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaStreamSourceArea,
                         meta_stream_source_area,
                         META_TYPE_STREAM_SOURCE,
                         G_IMPLEMENT_INTERFACE (META_TYPE_HW_CURSOR_INHIBITOR,
                                                hw_cursor_inhibitor_iface_init))

static MetaBackend *
get_backend (MetaStreamSourceArea *source_area)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_area);
  MetaStream *stream = meta_stream_source_get_stream (source);

  return meta_stream_get_backend (stream);
}

static ClutterStage *
get_stage (MetaStreamSourceArea *source_area)
{
  MetaBackend *backend = get_backend (source_area);

  return CLUTTER_STAGE (meta_backend_get_stage (backend));
}

static gboolean
meta_stream_source_area_get_specs (MetaStreamSource *source,
                                   int              *width,
                                   int              *height,
                                   float            *frame_rate)
{
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamArea *area_stream = META_STREAM_AREA (stream);
  MtkRectangle area;
  float scale;

  meta_stream_area_get_area (area_stream, &area);
  scale = meta_stream_area_get_scale (area_stream);

  *width = (int) roundf (area.width * scale);
  *height = (int) roundf (area.height * scale);
  *frame_rate = 60.0;

  return TRUE;
}

static gboolean
is_cursor_in_stream (MetaStreamSourceArea *source_area)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_area);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamArea *area_stream = META_STREAM_AREA (stream);
  MetaBackend *backend = get_backend (source_area);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MtkRectangle area;
  graphene_rect_t area_rect;
  ClutterCursor *cursor;

  meta_stream_area_get_area (area_stream, &area);
  area_rect = mtk_rectangle_to_graphene_rect (&area);

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (cursor)
    {
      graphene_rect_t cursor_rect;

      cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                         cursor);
      return graphene_rect_intersection (&cursor_rect, &area_rect, NULL);
    }
  else
    {
      MetaCursorTracker *cursor_tracker =
        meta_backend_get_cursor_tracker (backend);
      graphene_point_t cursor_position;

      meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
      return graphene_rect_contains_point (&area_rect, &cursor_position);
    }
}

static gboolean
is_redraw_queued (MetaStreamSourceArea *source_area)
{
  ClutterStage *stage = get_stage (source_area);
  GList *l;

  for (l = clutter_stage_peek_stage_views (stage); l; l = l->next)
    {
      ClutterStageView *view = l->data;

      if (clutter_stage_is_redraw_queued_on_view (stage, view))
        return TRUE;
    }

  return FALSE;
}

static void
sync_cursor_state (MetaStreamSourceArea *source_area)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_area);
  MetaStreamPaintPhase paint_phase;
  MetaStreamRecordFlag flags;

  if (is_redraw_queued (source_area))
    return;

  flags = META_STREAM_RECORD_FLAG_CURSOR_ONLY;
  paint_phase = META_STREAM_PAINT_PHASE_DETACHED;
  meta_stream_source_maybe_record_frame (source, flags,
                                         paint_phase,
                                         NULL);
}

static void
pointer_position_invalidated (MetaCursorTracker    *cursor_tracker,
                              MetaStreamSourceArea *source_area)
{
  ClutterStage *stage = get_stage (source_area);

  clutter_stage_schedule_update (stage);
}

static void
cursor_changed (MetaCursorTracker   *cursor_tracker,
                MetaStreamSourceArea *source_area)
{
  source_area->cursor_bitmap_invalid = TRUE;
  sync_cursor_state (source_area);
}

static void
on_prepare_frame (ClutterStage         *stage,
                  ClutterStageView     *stage_view,
                  ClutterFrame         *frame,
                  MetaStreamSourceArea *source_area)
{
  sync_cursor_state (source_area);
}

static void
inhibit_hw_cursor (MetaStreamSourceArea *source_area)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (!source_area->hw_cursor_inhibited);

  backend = get_backend (source_area);
  inhibitor = META_HW_CURSOR_INHIBITOR (source_area);
  meta_backend_add_hw_cursor_inhibitor (backend, inhibitor);

  source_area->hw_cursor_inhibited = TRUE;
}

static void
uninhibit_hw_cursor (MetaStreamSourceArea *source_area)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (source_area->hw_cursor_inhibited);

  backend = get_backend (source_area);
  inhibitor = META_HW_CURSOR_INHIBITOR (source_area);
  meta_backend_remove_hw_cursor_inhibitor (backend, inhibitor);

  source_area->hw_cursor_inhibited = FALSE;
}

static void
maybe_record_frame_on_idle (gpointer user_data)
{
  MetaStreamSourceArea *source_area =
    META_STREAM_SOURCE_AREA (user_data);
  MetaStreamSource *source = META_STREAM_SOURCE (source_area);
  MetaStreamPaintPhase paint_phase;
  MetaStreamRecordFlag flags;

  source_area->maybe_record_idle_id = 0;

  flags = META_STREAM_RECORD_FLAG_NONE;
  paint_phase = META_STREAM_PAINT_PHASE_DETACHED;
  meta_stream_source_maybe_record_frame (source, flags,
                                         paint_phase,
                                         NULL);
}

static void
before_stage_painted (MetaStage        *stage,
                      ClutterStageView *view,
                      const MtkRegion  *redraw_clip,
                      ClutterFrame     *frame,
                      gpointer          user_data)
{
  MetaStreamSourceArea *source_area =
    META_STREAM_SOURCE_AREA (user_data);
  MetaStreamSource *source = META_STREAM_SOURCE (source_area);

  if (source_area->maybe_record_idle_id)
    return;

  if (!clutter_stage_view_peek_scanout (view))
    return;

  source_area->maybe_record_idle_id =
    g_idle_add_once (maybe_record_frame_on_idle, source);
}

static void
stage_painted (MetaStage        *stage,
               ClutterStageView *view,
               const MtkRegion  *redraw_clip,
               ClutterFrame     *frame,
               gpointer          user_data)
{
  MetaStreamSourceArea *source_area =
    META_STREAM_SOURCE_AREA (user_data);
  MetaStreamSource *source = META_STREAM_SOURCE (source_area);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamArea *area_stream = META_STREAM_AREA (stream);
  MtkRectangle area;

  if (source_area->maybe_record_idle_id)
    return;

  meta_stream_area_get_area (area_stream, &area);

  if (redraw_clip)
    {
      switch (mtk_region_contains_rectangle (redraw_clip, &area))
        {
        case MTK_REGION_OVERLAP_IN:
        case MTK_REGION_OVERLAP_PART:
          break;
        case MTK_REGION_OVERLAP_OUT:
          return;
        }
    }

  source_area->maybe_record_idle_id =
    g_idle_add_once (maybe_record_frame_on_idle, source);
}

static void
add_view_painted_watches (MetaStreamSourceArea *source_area)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_area);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamArea *area_stream = META_STREAM_AREA (stream);
  MetaBackend *backend = get_backend (source_area);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage;
  MetaStage *meta_stage;
  MtkRectangle area;
  GList *l;

  stage = get_stage (source_area);
  meta_stage = META_STAGE (stage);
  meta_stream_area_get_area (area_stream, &area);

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      MetaRendererView *view = l->data;
      MtkRectangle view_layout;

      clutter_stage_view_get_layout (CLUTTER_STAGE_VIEW (view), &view_layout);
      if (mtk_rectangle_overlap (&area, &view_layout))
        {
          MetaStageWatch *watch;

          watch = meta_stage_watch_view (meta_stage,
                                         CLUTTER_STAGE_VIEW (view),
                                         META_STAGE_WATCH_BEFORE_PAINT,
                                         before_stage_painted,
                                         source_area);

          source_area->watches = g_list_prepend (source_area->watches, watch);

          watch = meta_stage_watch_view (meta_stage,
                                         CLUTTER_STAGE_VIEW (view),
                                         META_STAGE_WATCH_AFTER_ACTOR_PAINT,
                                         stage_painted,
                                         source_area);

          source_area->watches = g_list_prepend (source_area->watches, watch);
        }
    }
}

static void
on_monitors_changed (MetaMonitorManager   *monitor_manager,
                     MetaStreamSourceArea *source_area)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_area);
  MetaStage *stage = META_STAGE (get_stage (source_area));
  GList *l;

  if (!meta_stream_source_is_enabled (source))
    return;

  for (l = source_area->watches; l; l = l->next)
    meta_stage_remove_watch (stage, l->data);
  g_clear_pointer (&source_area->watches, g_list_free);

  add_view_painted_watches (source_area);
}

static void
meta_stream_source_area_enable (MetaStreamSource *source)
{
  MetaStreamSourceArea *source_area =
    META_STREAM_SOURCE_AREA (source);
  MetaBackend *backend = get_backend (source_area);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage;
  MetaStream *stream;

  stream = meta_stream_source_get_stream (source);
  stage = get_stage (source_area);

  switch (meta_stream_get_cursor_mode (stream))
    {
    case META_STREAM_CURSOR_MODE_METADATA:
      source_area->position_invalidated_handler_id =
        g_signal_connect_after (cursor_tracker, "position-invalidated",
                                G_CALLBACK (pointer_position_invalidated),
                                source_area);
      source_area->cursor_changed_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-changed",
                                G_CALLBACK (cursor_changed),
                                source_area);
      source_area->prepare_frame_handler_id =
        g_signal_connect_after (stage, "prepare-frame",
                                G_CALLBACK (on_prepare_frame),
                                source_area);
      G_GNUC_FALLTHROUGH;
    case META_STREAM_CURSOR_MODE_HIDDEN:
      add_view_painted_watches (source_area);
      break;
    case META_STREAM_CURSOR_MODE_EMBEDDED:
      inhibit_hw_cursor (source_area);
      add_view_painted_watches (source_area);
      break;
    }

  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           source_area, 0);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
}

static void
meta_stream_source_area_disable (MetaStreamSource *source)
{
  MetaStreamSourceArea *source_area =
    META_STREAM_SOURCE_AREA (source);
  MetaBackend *backend = get_backend (source_area);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage;
  MetaStage *meta_stage;
  GList *l;

  stage = get_stage (source_area);
  meta_stage = META_STAGE (stage);

  for (l = source_area->watches; l; l = l->next)
    {
      MetaStageWatch *watch = l->data;

      meta_stage_remove_watch (meta_stage, watch);
    }
  g_clear_pointer (&source_area->watches, g_list_free);

  if (source_area->hw_cursor_inhibited)
    uninhibit_hw_cursor (source_area);

  g_clear_signal_handler (&source_area->position_invalidated_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&source_area->cursor_changed_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&source_area->prepare_frame_handler_id,
                          stage);

  g_clear_handle_id (&source_area->maybe_record_idle_id, g_source_remove);
}

static gboolean
meta_stream_source_area_record_to_buffer (MetaStreamSource      *source,
                                          MetaStreamRecordFlag   flags,
                                          MetaStreamPaintPhase   paint_phase,
                                          int                    width,
                                          int                    height,
                                          int                    stride,
                                          uint8_t               *data,
                                          MtkRegion             *damage,
                                          GError               **error)
{
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamArea *area_stream = META_STREAM_AREA (stream);
  MtkRectangle area;
  float scale;

  meta_stream_area_get_area (area_stream, &area);
  scale = meta_stream_area_get_scale (area_stream);

  return meta_stream_source_paint_to_buffer (source,
                                             NULL, NULL,
                                             &area, scale,
                                             width, height, stride,
                                             data,
                                             COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                             damage,
                                             error);

}

static gboolean
meta_stream_source_area_record_to_framebuffer (MetaStreamSource      *source,
                                               MetaStreamPaintPhase   paint_phase,
                                               CoglFramebuffer       *framebuffer,
                                               MtkRegion             *damage,
                                               GError               **error)
{
  MetaStreamSourceArea *source_area =
    META_STREAM_SOURCE_AREA (source);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamArea *area_stream = META_STREAM_AREA (stream);
  MetaBackend *backend = get_backend (source_area);
  ClutterStage *stage;
  MtkRectangle area;
  float scale;
  ClutterPaintFlag paint_flags = CLUTTER_PAINT_FLAG_CLEAR;

  stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  meta_stream_area_get_area (area_stream, &area);
  scale = meta_stream_area_get_scale (area_stream);

  switch (meta_stream_get_cursor_mode (stream))
    {
    case META_STREAM_CURSOR_MODE_METADATA:
    case META_STREAM_CURSOR_MODE_HIDDEN:
      paint_flags |= CLUTTER_PAINT_FLAG_NO_CURSORS;
      break;
    case META_STREAM_CURSOR_MODE_EMBEDDED:
      paint_flags |= CLUTTER_PAINT_FLAG_FORCE_CURSORS;
      break;
    }
  clutter_stage_paint_to_framebuffer_clipped (stage, framebuffer,
                                              &area, scale,
                                              NULL,
                                              damage,
                                              paint_flags);

  return TRUE;
}

static void
meta_stream_source_area_queue_follow_up (MetaStreamSource     *source,
                                         MetaStreamRecordFlag  flags)
{
  MetaStreamSourceArea *source_area =
    META_STREAM_SOURCE_AREA (source);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamArea *area_stream = META_STREAM_AREA (stream);
  ClutterStage *stage = get_stage (source_area);
  MtkRectangle area;
  GList *l;

  if (flags & META_STREAM_RECORD_FLAG_CURSOR_ONLY)
    {
      clutter_stage_schedule_update (stage);
      return;
    }

  meta_stream_area_get_area (area_stream, &area);

  for (l = clutter_stage_peek_stage_views (stage); l; l = l->next)
    {
      MetaRendererView *view = l->data;
      MtkRectangle view_layout;
      MtkRectangle damage;

      clutter_stage_view_get_layout (CLUTTER_STAGE_VIEW (view), &view_layout);

      if (!mtk_rectangle_intersect (&view_layout, &area, &damage))
        continue;

      damage.width = 1;
      damage.height = 1;

      clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage), &damage);
    }
}

static gboolean
should_cursor_metadata_be_set (MetaStreamSourceArea *source_area)
{
  MetaBackend *backend = get_backend (source_area);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);

  return (meta_cursor_tracker_get_pointer_visible (cursor_tracker) &&
          is_cursor_in_stream (source_area));
}

static void
get_cursor_position (MetaStreamSourceArea *source_area,
                     int                  *out_x,
                     int                  *out_y)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_area);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamArea *area_stream = META_STREAM_AREA (stream);
  MetaBackend *backend = get_backend (source_area);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  MtkRectangle area;
  float scale;
  graphene_point_t cursor_position;

  meta_stream_area_get_area (area_stream, &area);
  scale = meta_stream_area_get_scale (area_stream);

  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
  cursor_position.x -= area.x;
  cursor_position.y -= area.y;
  cursor_position.x *= scale;
  cursor_position.y *= scale;

  *out_x = (int) roundf (cursor_position.x);
  *out_y = (int) roundf (cursor_position.y);
}

static gboolean
meta_stream_source_area_is_cursor_metadata_valid (MetaStreamSource *source)
{
  MetaStreamSourceArea *source_area =
    META_STREAM_SOURCE_AREA (source);

  if (should_cursor_metadata_be_set (source_area))
    {
      int x, y;

      if (!source_area->last_cursor_matadata.set)
        return FALSE;

      if (source_area->cursor_bitmap_invalid)
        return FALSE;

      get_cursor_position (source_area, &x, &y);

      return (source_area->last_cursor_matadata.x == x &&
              source_area->last_cursor_matadata.y == y);
    }
  else
    {
      return !source_area->last_cursor_matadata.set;
    }
}

static void
meta_stream_source_area_set_cursor_metadata (MetaStreamSource       *source,
                                             struct spa_meta_cursor *spa_meta_cursor)
{
  MetaStreamSourceArea *source_area =
    META_STREAM_SOURCE_AREA (source);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamArea *area_stream = META_STREAM_AREA (stream);
  MetaBackend *backend = get_backend (source_area);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  ClutterCursor *cursor;
  int x, y;

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);

  if (!should_cursor_metadata_be_set (source_area))
    {
      source_area->last_cursor_matadata.set = FALSE;
      meta_stream_source_unset_cursor_metadata (source, spa_meta_cursor);
      return;
    }

  get_cursor_position (source_area, &x, &y);

  source_area->last_cursor_matadata.set = TRUE;
  source_area->last_cursor_matadata.x = x;
  source_area->last_cursor_matadata.y = y;

  if (source_area->cursor_bitmap_invalid)
    {
      if (cursor)
        {
          float view_scale;

          view_scale = meta_stream_area_get_scale (area_stream);

          meta_stream_source_set_cursor_sprite_metadata (source,
                                                         spa_meta_cursor,
                                                         cursor,
                                                         x, y,
                                                         view_scale);
        }
      else
        {
          meta_stream_source_set_empty_cursor_sprite_metadata (source,
                                                               spa_meta_cursor,
                                                               x, y);
        }

      source_area->cursor_bitmap_invalid = FALSE;
    }
  else
    {
      meta_stream_source_set_cursor_position_metadata (source,
                                                       spa_meta_cursor,
                                                       x, y);
    }
}

static gboolean
meta_stream_source_area_is_cursor_inhibited (MetaHwCursorInhibitor *inhibitor)
{
  MetaStreamSourceArea *source_area = META_STREAM_SOURCE_AREA (inhibitor);

  return is_cursor_in_stream (source_area);
}

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface)
{
  iface->is_cursor_inhibited =
    meta_stream_source_area_is_cursor_inhibited;
}

static void
meta_stream_source_area_class_init (MetaStreamSourceAreaClass *klass)
{
  MetaStreamSourceClass *source_class =
    META_STREAM_SOURCE_CLASS (klass);

  source_class->get_specs = meta_stream_source_area_get_specs;
  source_class->enable = meta_stream_source_area_enable;
  source_class->disable = meta_stream_source_area_disable;
  source_class->record_to_buffer =
    meta_stream_source_area_record_to_buffer;
  source_class->record_to_framebuffer =
    meta_stream_source_area_record_to_framebuffer;
  source_class->queue_follow_up =
    meta_stream_source_area_queue_follow_up;
  source_class->is_cursor_metadata_valid =
    meta_stream_source_area_is_cursor_metadata_valid;
  source_class->set_cursor_metadata =
    meta_stream_source_area_set_cursor_metadata;
}

static void
meta_stream_source_area_init (MetaStreamSourceArea *source_area)
{
  source_area->cursor_bitmap_invalid = TRUE;
}

MetaStreamSourceArea *
meta_stream_source_area_new (MetaStreamArea  *area_stream,
                             GError         **error)
{
  MtkRectangle area;

  meta_stream_area_get_area (area_stream, &area);

  return g_initable_new (META_TYPE_STREAM_SOURCE_AREA, NULL, error,
                         "stream", area_stream,
                         "layout", &area,
                         NULL);
}
