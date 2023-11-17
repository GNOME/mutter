/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat Inc.
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

#include "backends/meta-screen-cast-monitor-stream-src.h"

#include <spa/buffer/meta.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-screen-cast-monitor-stream.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-stage-private.h"
#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"
#include "core/boxes-private.h"

struct _MetaScreenCastMonitorStreamSrc
{
  MetaScreenCastStreamSrc parent;

  gboolean cursor_bitmap_invalid;
  gboolean hw_cursor_inhibited;

  GList *watches;

  gulong position_invalidated_handler_id;
  gulong cursor_changed_handler_id;
  gulong stage_prepare_frame_handler_id;

  guint maybe_record_idle_id;
};

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastMonitorStreamSrc,
                         meta_screen_cast_monitor_stream_src,
                         META_TYPE_SCREEN_CAST_STREAM_SRC,
                         G_IMPLEMENT_INTERFACE (META_TYPE_HW_CURSOR_INHIBITOR,
                                                hw_cursor_inhibitor_iface_init))

static MetaBackend *
get_backend (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastSession *session = meta_screen_cast_stream_get_session (stream);
  MetaScreenCast *screen_cast =
    meta_screen_cast_session_get_screen_cast (session);

  return meta_screen_cast_get_backend (screen_cast);
}

static ClutterStage *
get_stage (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaScreenCastStreamSrc *src;
  MetaScreenCastStream *stream;
  MetaScreenCastMonitorStream *monitor_stream;

  src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  stream = meta_screen_cast_stream_src_get_stream (src);
  monitor_stream = META_SCREEN_CAST_MONITOR_STREAM (stream);

  return meta_screen_cast_monitor_stream_get_stage (monitor_stream);
}

static MetaMonitor *
get_monitor (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaScreenCastStreamSrc *src;
  MetaScreenCastStream *stream;
  MetaScreenCastMonitorStream *monitor_stream;

  src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  stream = meta_screen_cast_stream_src_get_stream (src);
  monitor_stream = META_SCREEN_CAST_MONITOR_STREAM (stream);

  return meta_screen_cast_monitor_stream_get_monitor (monitor_stream);
}

static gboolean
meta_screen_cast_monitor_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                               int                     *width,
                                               int                     *height,
                                               float                   *frame_rate)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  float scale;
  MetaMonitorMode *mode;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  mode = meta_monitor_get_current_mode (monitor);

  if (meta_backend_is_stage_views_scaled (backend))
    scale = logical_monitor->scale;
  else
    scale = 1.0;

  *width = (int) roundf (logical_monitor->rect.width * scale);
  *height = (int) roundf (logical_monitor->rect.height * scale);
  *frame_rate = meta_monitor_mode_get_refresh_rate (mode);

  return TRUE;
}

static gboolean
maybe_record_frame_on_idle (gpointer user_data)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (user_data);
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  MetaScreenCastPaintPhase paint_phase;
  MetaScreenCastRecordFlag flags;

  monitor_src->maybe_record_idle_id = 0;

  flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
  paint_phase = META_SCREEN_CAST_PAINT_PHASE_DETACHED;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags, paint_phase, NULL);

  return G_SOURCE_REMOVE;
}

static void
stage_painted (MetaStage        *stage,
               ClutterStageView *view,
               const MtkRegion  *redraw_clip,
               ClutterFrame     *frame,
               gpointer          user_data)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (user_data);
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  MetaScreenCastRecordResult record_result =
    META_SCREEN_CAST_RECORD_RESULT_RECORDED_NOTHING;
  int64_t presentation_time_us;

  if (monitor_src->maybe_record_idle_id)
    return;

  if (!clutter_frame_get_target_presentation_time (frame, &presentation_time_us))
    presentation_time_us = g_get_monotonic_time ();

  if (meta_screen_cast_stream_src_uses_dma_bufs (src))
    {
      MetaScreenCastRecordFlag flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
      MetaScreenCastPaintPhase paint_phase =
        META_SCREEN_CAST_PAINT_PHASE_PRE_SWAP_BUFFER;

      record_result =
        meta_screen_cast_stream_src_maybe_record_frame_with_timestamp (src,
                                                                       flags,
                                                                       paint_phase,
                                                                       NULL,
                                                                       presentation_time_us);
    }

  if (!(record_result & META_SCREEN_CAST_RECORD_RESULT_RECORDED_FRAME))
    {
      monitor_src->maybe_record_idle_id = g_idle_add (maybe_record_frame_on_idle,
                                                      src);
      g_source_set_name_by_id (monitor_src->maybe_record_idle_id,
                               "[mutter] maybe_record_frame_on_idle [monitor-src]");
    }
}

static void
before_stage_painted (MetaStage        *stage,
                      ClutterStageView *view,
                      const MtkRegion  *redraw_clip,
                      ClutterFrame     *frame,
                      gpointer          user_data)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (user_data);
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  MetaScreenCastPaintPhase paint_phase;
  MetaScreenCastRecordFlag flags;
  int64_t presentation_time_us;

  if (monitor_src->maybe_record_idle_id)
    return;

  if (!meta_screen_cast_stream_src_uses_dma_bufs (src))
    return;

  if (!clutter_stage_view_peek_scanout (view))
    return;

  if (!clutter_frame_get_target_presentation_time (frame, &presentation_time_us))
    presentation_time_us = g_get_monotonic_time ();

  flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
  paint_phase = META_SCREEN_CAST_PAINT_PHASE_PRE_PAINT;
  meta_screen_cast_stream_src_maybe_record_frame_with_timestamp (src,
                                                                 flags,
                                                                 paint_phase,
                                                                 NULL,
                                                                 presentation_time_us);
}

static gboolean
is_cursor_in_stream (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaBackend *backend = get_backend (monitor_src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;
  graphene_rect_t logical_monitor_rect;
  MetaCursorSprite *cursor_sprite;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);
  logical_monitor_rect =
    mtk_rectangle_to_graphene_rect (&logical_monitor_layout);

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (cursor_sprite)
    {
      graphene_rect_t cursor_rect;

      cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                         cursor_sprite);
      return graphene_rect_intersection (&cursor_rect,
                                         &logical_monitor_rect,
                                         NULL);
    }
  else
    {
      MetaCursorTracker *cursor_tracker =
        meta_backend_get_cursor_tracker (backend);
      graphene_point_t cursor_position;

      meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
      return graphene_rect_contains_point (&logical_monitor_rect,
                                           &cursor_position);
    }
}

static gboolean
is_redraw_queued (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaBackend *backend = get_backend (monitor_src);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage = get_stage (monitor_src);
  MetaMonitor *monitor = get_monitor (monitor_src);
  g_autoptr (GList) views = NULL;
  GList *l;

  views = meta_renderer_get_views_for_monitor (renderer, monitor);
  for (l = views; l; l = l->next)
    {
      MetaRendererView *view = l->data;

      if (clutter_stage_is_redraw_queued_on_view (stage, CLUTTER_STAGE_VIEW (view)))
        return TRUE;
    }

  return FALSE;
}

static void
sync_cursor_state (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  MetaScreenCastPaintPhase paint_phase;
  MetaScreenCastRecordFlag flags;

  if (is_redraw_queued (monitor_src))
    return;

  if (meta_screen_cast_stream_src_pending_follow_up_frame (src))
    return;

  flags = META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY;
  paint_phase = META_SCREEN_CAST_PAINT_PHASE_DETACHED;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags,
                                                  paint_phase,
                                                  NULL);
}

static void
pointer_position_invalidated (MetaCursorTracker              *cursor_tracker,
                              MetaScreenCastMonitorStreamSrc *monitor_src)
{
  ClutterStage *stage = get_stage (monitor_src);

  clutter_stage_schedule_update (stage);
}

static void
cursor_changed (MetaCursorTracker              *cursor_tracker,
                MetaScreenCastMonitorStreamSrc *monitor_src)
{
  monitor_src->cursor_bitmap_invalid = TRUE;
  sync_cursor_state (monitor_src);
}

static void
on_prepare_frame (ClutterStage                   *stage,
                  ClutterStageView               *stage_view,
                  ClutterFrame                   *frame,
                  MetaScreenCastMonitorStreamSrc *monitor_src)
{
  sync_cursor_state (monitor_src);
}

static void
inhibit_hw_cursor (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (!monitor_src->hw_cursor_inhibited);

  backend = get_backend (monitor_src);
  inhibitor = META_HW_CURSOR_INHIBITOR (monitor_src);
  meta_backend_add_hw_cursor_inhibitor (backend, inhibitor);

  monitor_src->hw_cursor_inhibited = TRUE;
}

static void
uninhibit_hw_cursor (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (monitor_src->hw_cursor_inhibited);

  backend = get_backend (monitor_src);
  inhibitor = META_HW_CURSOR_INHIBITOR (monitor_src);
  meta_backend_remove_hw_cursor_inhibitor (backend, inhibitor);

  monitor_src->hw_cursor_inhibited = FALSE;
}

static void
add_view_watches (MetaScreenCastMonitorStreamSrc *monitor_src,
                  MetaStageWatchPhase             watch_phase,
                  MetaStageWatchFunc              callback)
{
  MetaBackend *backend = get_backend (monitor_src);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage;
  MetaStage *meta_stage;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;
  GList *l;

  stage = get_stage (monitor_src);
  meta_stage = META_STAGE (stage);
  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      MetaRendererView *view = l->data;
      MtkRectangle view_layout;

      clutter_stage_view_get_layout (CLUTTER_STAGE_VIEW (view), &view_layout);
      if (mtk_rectangle_overlap (&logical_monitor_layout, &view_layout))
        {
          MetaStageWatch *watch;

          watch = meta_stage_watch_view (meta_stage,
                                         CLUTTER_STAGE_VIEW (view),
                                         watch_phase,
                                         callback,
                                         monitor_src);

          monitor_src->watches = g_list_prepend (monitor_src->watches, watch);
        }
    }
}

static void
reattach_watches (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  MetaScreenCastStream *stream;
  ClutterStage *stage;
  GList *l;

  stream = meta_screen_cast_stream_src_get_stream (src);
  stage = get_stage (monitor_src);

  for (l = monitor_src->watches; l; l = l->next)
    meta_stage_remove_watch (META_STAGE (stage), l->data);
  g_clear_pointer (&monitor_src->watches, g_list_free);

  add_view_watches (monitor_src,
                    META_STAGE_WATCH_BEFORE_PAINT,
                    before_stage_painted);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      add_view_watches (monitor_src,
                        META_STAGE_WATCH_AFTER_ACTOR_PAINT,
                        stage_painted);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      add_view_watches (monitor_src,
                        META_STAGE_WATCH_AFTER_PAINT,
                        stage_painted);
      break;
    }
}

static void
on_monitors_changed (MetaMonitorManager             *monitor_manager,
                     MetaScreenCastMonitorStreamSrc *monitor_src)
{
  reattach_watches (monitor_src);
}

static void
meta_screen_cast_monitor_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage = get_stage (monitor_src);
  MetaScreenCastStream *stream;

  stream = meta_screen_cast_stream_src_get_stream (src);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
      monitor_src->position_invalidated_handler_id =
        g_signal_connect_after (cursor_tracker, "position-invalidated",
                                G_CALLBACK (pointer_position_invalidated),
                                monitor_src);
      monitor_src->cursor_changed_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-changed",
                                G_CALLBACK (cursor_changed),
                                monitor_src);
      monitor_src->stage_prepare_frame_handler_id =
        g_signal_connect_after (stage, "prepare-frame",
                                G_CALLBACK (on_prepare_frame),
                                monitor_src);
      meta_cursor_tracker_track_position (cursor_tracker);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      inhibit_hw_cursor (monitor_src);
      meta_cursor_tracker_track_position (cursor_tracker);
      break;
    }

  reattach_watches (monitor_src);
  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           monitor_src, 0);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (get_stage (monitor_src)));
}

static void
meta_screen_cast_monitor_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage;
  MetaStage *meta_stage;
  GList *l;

  stage = get_stage (monitor_src);
  meta_stage = META_STAGE (stage);

  for (l = monitor_src->watches; l; l = l->next)
    {
      MetaStageWatch *watch = l->data;

      meta_stage_remove_watch (meta_stage, watch);
    }
  g_clear_pointer (&monitor_src->watches, g_list_free);

  if (monitor_src->hw_cursor_inhibited)
    uninhibit_hw_cursor (monitor_src);

  g_clear_signal_handler (&monitor_src->position_invalidated_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&monitor_src->cursor_changed_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&monitor_src->stage_prepare_frame_handler_id,
                          stage);

  g_clear_handle_id (&monitor_src->maybe_record_idle_id, g_source_remove);

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
meta_screen_cast_monitor_stream_src_record_to_buffer (MetaScreenCastStreamSrc   *src,
                                                      MetaScreenCastPaintPhase   paint_phase,
                                                      int                        width,
                                                      int                        height,
                                                      int                        stride,
                                                      uint8_t                   *data,
                                                      GError                   **error)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaBackend *backend = get_backend (monitor_src);
  ClutterStage *stage;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  float scale;
  ClutterPaintFlag paint_flags = CLUTTER_PAINT_FLAG_CLEAR;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  stage = get_stage (monitor_src);

  if (meta_backend_is_stage_views_scaled (backend))
    scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    scale = 1.0;

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

  if (!clutter_stage_paint_to_buffer (stage, &logical_monitor->rect, scale,
                                      data,
                                      stride,
                                      COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                      paint_flags,
                                      error))
    return FALSE;

  return TRUE;
}

static gboolean
meta_screen_cast_monitor_stream_src_record_to_framebuffer (MetaScreenCastStreamSrc   *src,
                                                           MetaScreenCastPaintPhase   paint_phase,
                                                           CoglFramebuffer           *framebuffer,
                                                           GError                   **error)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage = get_stage (monitor_src);
  g_autoptr (GError) local_error = NULL;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MetaRendererView *renderer_view;
  ClutterStageView *view;
  MtkRectangle logical_monitor_layout;
  MtkRectangle view_layout;
  MetaCrtc *crtc;
  gboolean do_stage_paint = TRUE;
  float view_scale;
  GList *outputs;
  int x, y;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  if (meta_backend_is_stage_views_scaled (backend))
    view_scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    view_scale = 1.0;

  if (paint_phase == META_SCREEN_CAST_PAINT_PHASE_DETACHED)
    goto stage_paint;

  outputs = meta_monitor_get_outputs (monitor);
  if (outputs->next)
    goto stage_paint;

  crtc = meta_output_get_assigned_crtc (outputs->data);
  renderer_view = meta_renderer_get_view_for_crtc (renderer, crtc);

  g_assert (renderer_view != NULL);

  view = CLUTTER_STAGE_VIEW (renderer_view);
  clutter_stage_view_get_layout (view, &view_layout);

  x = (int) roundf ((view_layout.x - logical_monitor_layout.x) * view_scale);
  y = (int) roundf ((view_layout.y - logical_monitor_layout.y) * view_scale);

  switch (paint_phase)
    {
    case META_SCREEN_CAST_PAINT_PHASE_PRE_PAINT:
      {
        CoglScanout *scanout = clutter_stage_view_peek_scanout (view);

        if (scanout)
          {
            cogl_scanout_blit_to_framebuffer (scanout,
                                              framebuffer,
                                              x, y,
                                              &local_error);
          }
      }
      break;

    case META_SCREEN_CAST_PAINT_PHASE_PRE_SWAP_BUFFER:
      {
        CoglFramebuffer *view_framebuffer =
          clutter_stage_view_get_framebuffer (view);

        cogl_blit_framebuffer (view_framebuffer,
                               framebuffer,
                               0, 0,
                               x, y,
                               cogl_framebuffer_get_width (view_framebuffer),
                               cogl_framebuffer_get_height (view_framebuffer),
                               &local_error);
      }
      break;

    case META_SCREEN_CAST_PAINT_PHASE_DETACHED:
      g_assert_not_reached ();
    }

  if (local_error)
    {
      g_warning ("Error blitting to screencast framebuffer: %s",
                 local_error->message);
    }

  do_stage_paint = local_error != NULL;

stage_paint:
  if (do_stage_paint)
    {
      ClutterPaintFlag paint_flags = CLUTTER_PAINT_FLAG_CLEAR;

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

      clutter_stage_paint_to_framebuffer (stage,
                                          framebuffer,
                                          &logical_monitor_layout,
                                          view_scale,
                                          paint_flags);
    }

  cogl_framebuffer_flush (framebuffer);

  return TRUE;
}

static void
meta_screen_cast_monitor_stream_record_follow_up (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage = get_stage (monitor_src);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;
  GList *l;

  g_clear_handle_id (&monitor_src->maybe_record_idle_id, g_source_remove);

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      MetaRendererView *view = l->data;
      MtkRectangle view_layout;
      MtkRectangle damage;

      clutter_stage_view_get_layout (CLUTTER_STAGE_VIEW (view), &view_layout);

      if (!mtk_rectangle_overlap (&logical_monitor_layout, &view_layout))
        continue;

      damage = (MtkRectangle) {
        .x = view_layout.x,
        .y = view_layout.y,
        .width = 1,
        .height = 1,
      };
      clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage), &damage);
    }
}

static void
meta_screen_cast_monitor_stream_src_set_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                         struct spa_meta_cursor  *spa_meta_cursor)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  MetaCursorSprite *cursor_sprite;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;
  graphene_rect_t logical_monitor_rect;
  float view_scale;
  graphene_point_t cursor_position;
  int x, y;

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);

  if (!meta_cursor_tracker_get_pointer_visible (cursor_tracker) ||
      !is_cursor_in_stream (monitor_src))
    {
      meta_screen_cast_stream_src_unset_cursor_metadata (src,
                                                         spa_meta_cursor);
      return;
    }

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);
  logical_monitor_rect =
    mtk_rectangle_to_graphene_rect (&logical_monitor_layout);

  if (meta_backend_is_stage_views_scaled (backend))
    view_scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    view_scale = 1.0;

  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
  cursor_position.x -= logical_monitor_rect.origin.x;
  cursor_position.y -= logical_monitor_rect.origin.y;
  cursor_position.x *= view_scale;
  cursor_position.y *= view_scale;

  x = (int) roundf (cursor_position.x);
  y = (int) roundf (cursor_position.y);

  if (monitor_src->cursor_bitmap_invalid)
    {
      if (cursor_sprite)
        {
          float cursor_scale;
          float scale;
          MetaMonitorTransform transform;

          cursor_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);
          scale = view_scale * cursor_scale;
          transform = meta_cursor_sprite_get_texture_transform (cursor_sprite);

          meta_screen_cast_stream_src_set_cursor_sprite_metadata (src,
                                                                  spa_meta_cursor,
                                                                  cursor_sprite,
                                                                  x, y,
                                                                  scale,
                                                                  transform);
        }
      else
        {
          meta_screen_cast_stream_src_set_empty_cursor_sprite_metadata (src,
                                                                        spa_meta_cursor,
                                                                        x, y);
        }

      monitor_src->cursor_bitmap_invalid = FALSE;
    }
  else
    {
      meta_screen_cast_stream_src_set_cursor_position_metadata (src,
                                                                spa_meta_cursor,
                                                                x, y);
    }
}

static gboolean
meta_screen_cast_monitor_stream_src_is_cursor_inhibited (MetaHwCursorInhibitor *inhibitor)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (inhibitor);

  return is_cursor_in_stream (monitor_src);
}

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface)
{
  iface->is_cursor_inhibited =
    meta_screen_cast_monitor_stream_src_is_cursor_inhibited;
}

MetaScreenCastMonitorStreamSrc *
meta_screen_cast_monitor_stream_src_new (MetaScreenCastMonitorStream  *monitor_stream,
                                         GError                      **error)
{
  return g_initable_new (META_TYPE_SCREEN_CAST_MONITOR_STREAM_SRC, NULL, error,
                         "stream", monitor_stream,
                         NULL);
}

static void
meta_screen_cast_monitor_stream_src_init (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  monitor_src->cursor_bitmap_invalid = TRUE;
}

static void
meta_screen_cast_monitor_stream_src_class_init (MetaScreenCastMonitorStreamSrcClass *klass)
{
  MetaScreenCastStreamSrcClass *src_class =
    META_SCREEN_CAST_STREAM_SRC_CLASS (klass);

  src_class->get_specs = meta_screen_cast_monitor_stream_src_get_specs;
  src_class->enable = meta_screen_cast_monitor_stream_src_enable;
  src_class->disable = meta_screen_cast_monitor_stream_src_disable;
  src_class->record_to_buffer =
    meta_screen_cast_monitor_stream_src_record_to_buffer;
  src_class->record_to_framebuffer =
    meta_screen_cast_monitor_stream_src_record_to_framebuffer;
  src_class->record_follow_up =
    meta_screen_cast_monitor_stream_record_follow_up;
  src_class->set_cursor_metadata =
    meta_screen_cast_monitor_stream_src_set_cursor_metadata;
}
