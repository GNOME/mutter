/*
 * Copyright (C) 2020 Red Hat Inc.
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

/* Till https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/4065 is fixed */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-conversion"

#include "config.h"

#include "backends/meta-screen-cast-area-stream-src.h"

#include <spa/buffer/meta.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-dbus-session-manager.h"
#include "backends/meta-screen-cast-area-stream.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-stage-private.h"
#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"
#include "core/boxes-private.h"

struct _MetaScreenCastAreaStreamSrc
{
  MetaScreenCastStreamSrc parent;

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

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastAreaStreamSrc,
                         meta_screen_cast_area_stream_src,
                         META_TYPE_SCREEN_CAST_STREAM_SRC,
                         G_IMPLEMENT_INTERFACE (META_TYPE_HW_CURSOR_INHIBITOR,
                                                hw_cursor_inhibitor_iface_init))

static ClutterStage *
get_stage (MetaScreenCastAreaStreamSrc *area_src)
{
  MetaScreenCastStreamSrc *src;
  MetaScreenCastStream *stream;
  MetaScreenCastAreaStream *area_stream;

  src = META_SCREEN_CAST_STREAM_SRC (area_src);
  stream = meta_screen_cast_stream_src_get_stream (src);
  area_stream = META_SCREEN_CAST_AREA_STREAM (stream);

  return meta_screen_cast_area_stream_get_stage (area_stream);
}

static MetaBackend *
get_backend (MetaScreenCastAreaStreamSrc *area_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (area_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastSession *session = meta_screen_cast_stream_get_session (stream);
  MetaScreenCast *screen_cast =
    meta_screen_cast_session_get_screen_cast (session);

  return meta_screen_cast_get_backend (screen_cast);
}

static gboolean
meta_screen_cast_area_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                            int                     *width,
                                            int                     *height,
                                            float                   *frame_rate)
{
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastAreaStream *area_stream = META_SCREEN_CAST_AREA_STREAM (stream);
  MtkRectangle *area;
  float scale;

  area = meta_screen_cast_area_stream_get_area (area_stream);
  scale = meta_screen_cast_area_stream_get_scale (area_stream);

  *width = (int) roundf (area->width * scale);
  *height = (int) roundf (area->height * scale);
  *frame_rate = 60.0;

  return TRUE;
}

static gboolean
is_cursor_in_stream (MetaScreenCastAreaStreamSrc *area_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (area_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastAreaStream *area_stream = META_SCREEN_CAST_AREA_STREAM (stream);
  MetaBackend *backend = get_backend (area_src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MtkRectangle *area;
  graphene_rect_t area_rect;
  MetaCursorSprite *cursor_sprite;

  area = meta_screen_cast_area_stream_get_area (area_stream);
  area_rect = mtk_rectangle_to_graphene_rect (area);

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (cursor_sprite)
    {
      graphene_rect_t cursor_rect;

      cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                         cursor_sprite);
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
is_redraw_queued (MetaScreenCastAreaStreamSrc *area_src)
{
  ClutterStage *stage = get_stage (area_src);
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
sync_cursor_state (MetaScreenCastAreaStreamSrc *area_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (area_src);
  MetaScreenCastPaintPhase paint_phase;
  MetaScreenCastRecordFlag flags;

  if (is_redraw_queued (area_src))
    return;

  flags = META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY;
  paint_phase = META_SCREEN_CAST_PAINT_PHASE_DETACHED;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags,
                                                  paint_phase,
                                                  NULL);
}

static void
pointer_position_invalidated (MetaCursorTracker           *cursor_tracker,
                              MetaScreenCastAreaStreamSrc *area_src)
{
  ClutterStage *stage = get_stage (area_src);

  clutter_stage_schedule_update (stage);
}

static void
cursor_changed (MetaCursorTracker           *cursor_tracker,
                MetaScreenCastAreaStreamSrc *area_src)
{
  area_src->cursor_bitmap_invalid = TRUE;
  sync_cursor_state (area_src);
}

static void
on_prepare_frame (ClutterStage                *stage,
                  ClutterStageView            *stage_view,
                  ClutterFrame                *frame,
                  MetaScreenCastAreaStreamSrc *area_src)
{
  sync_cursor_state (area_src);
}

static void
inhibit_hw_cursor (MetaScreenCastAreaStreamSrc *area_src)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (!area_src->hw_cursor_inhibited);

  backend = get_backend (area_src);
  inhibitor = META_HW_CURSOR_INHIBITOR (area_src);
  meta_backend_add_hw_cursor_inhibitor (backend, inhibitor);

  area_src->hw_cursor_inhibited = TRUE;
}

static void
uninhibit_hw_cursor (MetaScreenCastAreaStreamSrc *area_src)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (area_src->hw_cursor_inhibited);

  backend = get_backend (area_src);
  inhibitor = META_HW_CURSOR_INHIBITOR (area_src);
  meta_backend_remove_hw_cursor_inhibitor (backend, inhibitor);

  area_src->hw_cursor_inhibited = FALSE;
}

static gboolean
maybe_record_frame_on_idle (gpointer user_data)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (user_data);
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (area_src);
  MetaScreenCastPaintPhase paint_phase;
  MetaScreenCastRecordFlag flags;

  area_src->maybe_record_idle_id = 0;

  flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
  paint_phase = META_SCREEN_CAST_PAINT_PHASE_DETACHED;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags,
                                                  paint_phase,
                                                  NULL);

  return G_SOURCE_REMOVE;
}

static void
before_stage_painted (MetaStage        *stage,
                      ClutterStageView *view,
                      const MtkRegion  *redraw_clip,
                      ClutterFrame     *frame,
                      gpointer          user_data)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (user_data);
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (area_src);

  if (area_src->maybe_record_idle_id)
    return;

  if (!clutter_stage_view_peek_scanout (view))
    return;

  area_src->maybe_record_idle_id = g_idle_add (maybe_record_frame_on_idle, src);
}

static void
stage_painted (MetaStage        *stage,
               ClutterStageView *view,
               const MtkRegion  *redraw_clip,
               ClutterFrame     *frame,
               gpointer          user_data)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (user_data);
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (area_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastAreaStream *area_stream = META_SCREEN_CAST_AREA_STREAM (stream);
  MtkRectangle *area;

  if (area_src->maybe_record_idle_id)
    return;

  area = meta_screen_cast_area_stream_get_area (area_stream);

  if (redraw_clip)
    {
      switch (mtk_region_contains_rectangle (redraw_clip, area))
        {
        case MTK_REGION_OVERLAP_IN:
        case MTK_REGION_OVERLAP_PART:
          break;
        case MTK_REGION_OVERLAP_OUT:
          return;
        }
    }

  area_src->maybe_record_idle_id = g_idle_add (maybe_record_frame_on_idle, src);
}

static void
add_view_painted_watches (MetaScreenCastAreaStreamSrc *area_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (area_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastAreaStream *area_stream = META_SCREEN_CAST_AREA_STREAM (stream);
  MetaBackend *backend = get_backend (area_src);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage;
  MetaStage *meta_stage;
  MtkRectangle *area;
  GList *l;

  stage = get_stage (area_src);
  meta_stage = META_STAGE (stage);
  area = meta_screen_cast_area_stream_get_area (area_stream);

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      MetaRendererView *view = l->data;
      MtkRectangle view_layout;

      clutter_stage_view_get_layout (CLUTTER_STAGE_VIEW (view), &view_layout);
      if (mtk_rectangle_overlap (area, &view_layout))
        {
          MetaStageWatch *watch;

          watch = meta_stage_watch_view (meta_stage,
                                         CLUTTER_STAGE_VIEW (view),
                                         META_STAGE_WATCH_BEFORE_PAINT,
                                         before_stage_painted,
                                         area_src);

          area_src->watches = g_list_prepend (area_src->watches, watch);

          watch = meta_stage_watch_view (meta_stage,
                                         CLUTTER_STAGE_VIEW (view),
                                         META_STAGE_WATCH_AFTER_ACTOR_PAINT,
                                         stage_painted,
                                         area_src);

          area_src->watches = g_list_prepend (area_src->watches, watch);
        }
    }
}

static void
on_monitors_changed (MetaMonitorManager          *monitor_manager,
                     MetaScreenCastAreaStreamSrc *area_src)
{
  MetaStage *stage = META_STAGE (get_stage (area_src));
  GList *l;

  for (l = area_src->watches; l; l = l->next)
    meta_stage_remove_watch (stage, l->data);
  g_clear_pointer (&area_src->watches, g_list_free);

  add_view_painted_watches (area_src);
}

static void
meta_screen_cast_area_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (src);
  MetaBackend *backend = get_backend (area_src);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage;
  MetaScreenCastStream *stream;

  stream = meta_screen_cast_stream_src_get_stream (src);
  stage = get_stage (area_src);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
      area_src->position_invalidated_handler_id =
        g_signal_connect_after (cursor_tracker, "position-invalidated",
                                G_CALLBACK (pointer_position_invalidated),
                                area_src);
      area_src->cursor_changed_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-changed",
                                G_CALLBACK (cursor_changed),
                                area_src);
      area_src->prepare_frame_handler_id =
        g_signal_connect_after (stage, "prepare-frame",
                                G_CALLBACK (on_prepare_frame),
                                area_src);
      meta_cursor_tracker_track_position (cursor_tracker);
      G_GNUC_FALLTHROUGH;
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      add_view_painted_watches (area_src);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      inhibit_hw_cursor (area_src);
      meta_cursor_tracker_track_position (cursor_tracker);
      add_view_painted_watches (area_src);
      break;
    }

  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           area_src, 0);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
}

static void
meta_screen_cast_area_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaBackend *backend = get_backend (area_src);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage;
  MetaStage *meta_stage;
  GList *l;

  stage = get_stage (area_src);
  meta_stage = META_STAGE (stage);

  for (l = area_src->watches; l; l = l->next)
    {
      MetaStageWatch *watch = l->data;

      meta_stage_remove_watch (meta_stage, watch);
    }
  g_clear_pointer (&area_src->watches, g_list_free);

  if (area_src->hw_cursor_inhibited)
    uninhibit_hw_cursor (area_src);

  g_clear_signal_handler (&area_src->position_invalidated_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&area_src->cursor_changed_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&area_src->prepare_frame_handler_id,
                          stage);

  g_clear_handle_id (&area_src->maybe_record_idle_id, g_source_remove);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      meta_cursor_tracker_untrack_position (cursor_tracker);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      break;
    }
}

static gboolean
meta_screen_cast_area_stream_src_record_to_buffer (MetaScreenCastStreamSrc   *src,
                                                   MetaScreenCastPaintPhase   paint_phase,
                                                   int                        width,
                                                   int                        height,
                                                   int                        stride,
                                                   uint8_t                   *data,
                                                   GError                   **error)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastAreaStream *area_stream = META_SCREEN_CAST_AREA_STREAM (stream);
  ClutterStage *stage;
  MtkRectangle *area;
  float scale;
  ClutterPaintFlag paint_flags = CLUTTER_PAINT_FLAG_CLEAR;

  stage = get_stage (area_src);
  area = meta_screen_cast_area_stream_get_area (area_stream);
  scale = meta_screen_cast_area_stream_get_scale (area_stream);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      paint_flags |= CLUTTER_PAINT_FLAG_NO_CURSORS;
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      paint_flags |= CLUTTER_PAINT_FLAG_FORCE_CURSORS;
      break;
    }

  if (!clutter_stage_paint_to_buffer (stage, area, scale,
                                      data,
                                      stride,
                                      COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                      paint_flags,
                                      error))
    return FALSE;

  return TRUE;
}

static gboolean
meta_screen_cast_area_stream_src_record_to_framebuffer (MetaScreenCastStreamSrc   *src,
                                                        MetaScreenCastPaintPhase   paint_phase,
                                                        CoglFramebuffer           *framebuffer,
                                                        GError                   **error)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastAreaStream *area_stream = META_SCREEN_CAST_AREA_STREAM (stream);
  MetaBackend *backend = get_backend (area_src);
  ClutterStage *stage;
  MtkRectangle *area;
  float scale;
  ClutterPaintFlag paint_flags = CLUTTER_PAINT_FLAG_CLEAR;

  stage = CLUTTER_STAGE (meta_backend_get_stage (backend));
  area = meta_screen_cast_area_stream_get_area (area_stream);
  scale = meta_screen_cast_area_stream_get_scale (area_stream);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      paint_flags |= CLUTTER_PAINT_FLAG_NO_CURSORS;
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      paint_flags |= CLUTTER_PAINT_FLAG_FORCE_CURSORS;
      break;
    }
  clutter_stage_paint_to_framebuffer (stage, framebuffer,
                                      area, scale,
                                      paint_flags);

  cogl_framebuffer_flush (framebuffer);

  return TRUE;
}

static void
meta_screen_cast_area_stream_record_follow_up (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (src);
  MetaScreenCastPaintPhase paint_phase;
  MetaScreenCastRecordFlag flags;

  g_clear_handle_id (&area_src->maybe_record_idle_id, g_source_remove);

  flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
  paint_phase = META_SCREEN_CAST_PAINT_PHASE_DETACHED;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags,
                                                  paint_phase,
                                                  NULL);
}

static gboolean
should_cursor_metadata_be_set (MetaScreenCastAreaStreamSrc *area_src)
{
  MetaBackend *backend = get_backend (area_src);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);

  return (meta_cursor_tracker_get_pointer_visible (cursor_tracker) &&
          is_cursor_in_stream (area_src));
}

static void
get_cursor_position (MetaScreenCastAreaStreamSrc *area_src,
                     int                         *out_x,
                     int                         *out_y)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (area_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastAreaStream *area_stream = META_SCREEN_CAST_AREA_STREAM (stream);
  MetaBackend *backend = get_backend (area_src);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  MtkRectangle *area;
  float scale;
  graphene_point_t cursor_position;

  area = meta_screen_cast_area_stream_get_area (area_stream);
  scale = meta_screen_cast_area_stream_get_scale (area_stream);

  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
  cursor_position.x -= area->x;
  cursor_position.y -= area->y;
  cursor_position.x *= scale;
  cursor_position.y *= scale;

  *out_x = (int) roundf (cursor_position.x);
  *out_y = (int) roundf (cursor_position.y);
}

static gboolean
meta_screen_cast_area_stream_src_is_cursor_metadata_valid (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (src);

  if (should_cursor_metadata_be_set (area_src))
    {
      int x, y;

      if (!area_src->last_cursor_matadata.set)
        return FALSE;

      if (area_src->cursor_bitmap_invalid)
        return FALSE;

      get_cursor_position (area_src, &x, &y);

      return (area_src->last_cursor_matadata.x == x &&
              area_src->last_cursor_matadata.y == y);
    }
  else
    {
      return !area_src->last_cursor_matadata.set;
    }
}

static void
meta_screen_cast_area_stream_src_set_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                      struct spa_meta_cursor  *spa_meta_cursor)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastAreaStream *area_stream = META_SCREEN_CAST_AREA_STREAM (stream);
  MetaBackend *backend = get_backend (area_src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorSprite *cursor_sprite;
  int x, y;

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);

  if (!should_cursor_metadata_be_set (area_src))
    {
      area_src->last_cursor_matadata.set = FALSE;
      meta_screen_cast_stream_src_unset_cursor_metadata (src,
                                                         spa_meta_cursor);
      return;
    }

  get_cursor_position (area_src, &x, &y);

  area_src->last_cursor_matadata.set = TRUE;
  area_src->last_cursor_matadata.x = x;
  area_src->last_cursor_matadata.y = y;

  if (area_src->cursor_bitmap_invalid)
    {
      if (cursor_sprite)
        {
          float view_scale;

          view_scale = meta_screen_cast_area_stream_get_scale (area_stream);

          meta_screen_cast_stream_src_set_cursor_sprite_metadata (src,
                                                                  spa_meta_cursor,
                                                                  cursor_sprite,
                                                                  x, y,
                                                                  view_scale);
        }
      else
        {
          meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (src,
                                                                        spa_meta_cursor,
                                                                        x, y);
        }

      area_src->cursor_bitmap_invalid = FALSE;
    }
  else
    {
      meta_screen_cast_stream_src_set_cursor_position_metadata (src,
                                                                spa_meta_cursor,
                                                                x, y);
    }
}

static gboolean
meta_screen_cast_area_stream_src_is_cursor_inhibited (MetaHwCursorInhibitor *inhibitor)
{
  MetaScreenCastAreaStreamSrc *area_src =
    META_SCREEN_CAST_AREA_STREAM_SRC (inhibitor);

  return is_cursor_in_stream (area_src);
}

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface)
{
  iface->is_cursor_inhibited =
    meta_screen_cast_area_stream_src_is_cursor_inhibited;
}

MetaScreenCastAreaStreamSrc *
meta_screen_cast_area_stream_src_new (MetaScreenCastAreaStream  *area_stream,
                                      GError                   **error)
{
  return g_initable_new (META_TYPE_SCREEN_CAST_AREA_STREAM_SRC, NULL, error,
                         "stream", area_stream,
                         NULL);
}

static void
meta_screen_cast_area_stream_src_init (MetaScreenCastAreaStreamSrc *area_src)
{
  area_src->cursor_bitmap_invalid = TRUE;
}

static void
meta_screen_cast_area_stream_src_class_init (MetaScreenCastAreaStreamSrcClass *klass)
{
  MetaScreenCastStreamSrcClass *src_class =
    META_SCREEN_CAST_STREAM_SRC_CLASS (klass);

  src_class->get_specs = meta_screen_cast_area_stream_src_get_specs;
  src_class->enable = meta_screen_cast_area_stream_src_enable;
  src_class->disable = meta_screen_cast_area_stream_src_disable;
  src_class->record_to_buffer =
    meta_screen_cast_area_stream_src_record_to_buffer;
  src_class->record_to_framebuffer =
    meta_screen_cast_area_stream_src_record_to_framebuffer;
  src_class->record_follow_up =
    meta_screen_cast_area_stream_record_follow_up;
  src_class->is_cursor_metadata_valid =
    meta_screen_cast_area_stream_src_is_cursor_metadata_valid;
  src_class->set_cursor_metadata =
    meta_screen_cast_area_stream_src_set_cursor_metadata;
}

#pragma GCC diagnostic pop
