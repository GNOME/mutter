/*
 * Copyright (C) 2017-2026 Red Hat Inc.
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

#include "backends/meta-stream-source-monitor.h"

#include <spa/buffer/meta.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-color-device.h"
#include "backends/meta-color-manager.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-private.h"
#include "backends/meta-stream.h"
#include "backends/meta-stage-private.h"
#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"
#include "core/boxes-private.h"
#include "core/meta-debug-control-private.h"

struct _MetaStreamSourceMonitor
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

  GArray *formats;

  gulong position_invalidated_handler_id;
  gulong cursor_changed_handler_id;
  gulong stage_prepare_frame_handler_id;

  guint maybe_record_idle_id;

  CoglPipeline *blending_pipeline;
  CoglFramebuffer *blending_framebuffer;
};

static void hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaStreamSourceMonitor,
                         meta_stream_source_monitor,
                         META_TYPE_STREAM_SOURCE,
                         G_IMPLEMENT_INTERFACE (META_TYPE_HW_CURSOR_INHIBITOR,
                                                hw_cursor_inhibitor_iface_init))

static MetaBackend *
get_backend (MetaStreamSourceMonitor *source_monitor)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_monitor);
  MetaStream *stream = meta_stream_source_get_stream (source);

  return meta_stream_get_backend (stream);
}

static ClutterStage *
get_stage (MetaStreamSourceMonitor *source_monitor)
{
  MetaBackend *backend = get_backend (source_monitor);

  return CLUTTER_STAGE (meta_backend_get_stage (backend));
}

static MetaMonitor *
get_monitor (MetaStreamSourceMonitor *source_monitor)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_monitor);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamMonitor *stream_monitor = META_STREAM_MONITOR (stream);

  return meta_stream_monitor_get_monitor (stream_monitor);
}

static gboolean
meta_stream_source_monitor_get_specs (MetaStreamSource *source,
                                      int              *width,
                                      int              *height,
                                      float            *frame_rate)
{
  MetaStreamSourceMonitor *source_monitor = META_STREAM_SOURCE_MONITOR (source);
  MetaBackend *backend = get_backend (source_monitor);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  float scale;
  MetaMonitorMode *mode;

  monitor = get_monitor (source_monitor);
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

static void
maybe_record_frame_on_idle (gpointer user_data)
{
  MetaStreamSourceMonitor *source_monitor =
    META_STREAM_SOURCE_MONITOR (user_data);
  MetaStreamSource *source = META_STREAM_SOURCE (source_monitor);
  MetaStreamPaintPhase paint_phase;
  MetaStreamRecordFlag flags;
  MtkRectangle empty_rect;
  g_autoptr (MtkRegion) empty_region = NULL;

  source_monitor->maybe_record_idle_id = 0;

  flags = META_STREAM_RECORD_FLAG_NONE;
  paint_phase = META_STREAM_PAINT_PHASE_DETACHED;
  empty_rect.x = empty_rect.y = 0;
  empty_rect.width = empty_rect.height = 0;
  empty_region = mtk_region_create_rectangle (&empty_rect);
  meta_stream_source_maybe_record_frame (source, flags, paint_phase,
                                                  empty_region);
}

static void
stage_painted (MetaStage        *stage,
               ClutterStageView *view,
               const MtkRegion  *redraw_clip,
               ClutterFrame     *frame,
               gpointer          user_data)
{
  MetaStreamSourceMonitor *source_monitor =
    META_STREAM_SOURCE_MONITOR (user_data);
  MetaStreamSource *source = META_STREAM_SOURCE (source_monitor);
  MetaStreamRecordFlag flags = META_STREAM_RECORD_FLAG_NONE;
  MetaStreamRecordResult record_result =
    META_STREAM_RECORD_RESULT_RECORDED_NOTHING;
  int64_t presentation_time_us;

  if (source_monitor->maybe_record_idle_id)
    return;

  if (!clutter_frame_get_expected_presentation_time (frame,
                                                     &presentation_time_us))
    presentation_time_us = g_get_monotonic_time ();

  if (meta_stream_source_uses_dma_bufs (source))
    {
      MetaStreamPaintPhase paint_phase =
        META_STREAM_PAINT_PHASE_PRE_SWAP_BUFFER;

      record_result =
        meta_stream_source_maybe_record_frame_with_timestamp (source,
                                                              flags,
                                                              paint_phase,
                                                              redraw_clip,
                                                              presentation_time_us);
    }

  if (!(record_result & META_STREAM_RECORD_RESULT_RECORDED_FRAME))
    {
      meta_stream_source_accumulate_damage (source,
                                            flags,
                                            redraw_clip);
      source_monitor->maybe_record_idle_id = g_idle_add_once (maybe_record_frame_on_idle,
                                                           source);
      g_source_set_name_by_id (source_monitor->maybe_record_idle_id,
                               "[mutter] maybe_record_frame_on_idle [monitor-source]");
    }
}

static void
before_stage_painted (MetaStage        *stage,
                      ClutterStageView *view,
                      const MtkRegion  *redraw_clip,
                      ClutterFrame     *frame,
                      gpointer          user_data)
{
  MetaStreamSourceMonitor *source_monitor =
    META_STREAM_SOURCE_MONITOR (user_data);
  MetaStreamSource *source = META_STREAM_SOURCE (source_monitor);
  MetaStreamPaintPhase paint_phase;
  MetaStreamRecordFlag flags;
  int64_t presentation_time_us;

  if (source_monitor->maybe_record_idle_id)
    return;

  if (!meta_stream_source_uses_dma_bufs (source))
    return;

  if (!clutter_stage_view_peek_scanout (view))
    return;

  if (!clutter_frame_get_expected_presentation_time (frame,
                                                     &presentation_time_us))
    presentation_time_us = g_get_monotonic_time ();

  flags = META_STREAM_RECORD_FLAG_NONE;
  paint_phase = META_STREAM_PAINT_PHASE_PRE_PAINT;
  meta_stream_source_maybe_record_frame_with_timestamp (source,
                                                        flags,
                                                        paint_phase,
                                                        redraw_clip,
                                                        presentation_time_us);
}

static gboolean
is_cursor_in_stream (MetaStreamSourceMonitor *source_monitor)
{
  MetaBackend *backend = get_backend (source_monitor);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;
  graphene_rect_t logical_monitor_rect;
  ClutterCursor *cursor;

  monitor = get_monitor (source_monitor);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);
  logical_monitor_rect =
    mtk_rectangle_to_graphene_rect (&logical_monitor_layout);

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (cursor)
    {
      graphene_rect_t cursor_rect;

      cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                         cursor);
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
is_redraw_queued (MetaStreamSourceMonitor *source_monitor)
{
  MetaBackend *backend = get_backend (source_monitor);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage = get_stage (source_monitor);
  MetaMonitor *monitor = get_monitor (source_monitor);
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
sync_cursor_state (MetaStreamSourceMonitor *source_monitor)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_monitor);
  MetaStreamPaintPhase paint_phase;
  MetaStreamRecordFlag flags;

  if (is_redraw_queued (source_monitor))
    return;

  flags = META_STREAM_RECORD_FLAG_CURSOR_ONLY;
  paint_phase = META_STREAM_PAINT_PHASE_DETACHED;
  meta_stream_source_maybe_record_frame (source, flags,
                                         paint_phase,
                                         NULL);
}

static void
pointer_position_invalidated (MetaCursorTracker       *cursor_tracker,
                              MetaStreamSourceMonitor *source_monitor)
{
  ClutterStage *stage = get_stage (source_monitor);

  clutter_stage_schedule_update (stage);
}

static void
cursor_changed (MetaCursorTracker       *cursor_tracker,
                MetaStreamSourceMonitor *source_monitor)
{
  source_monitor->cursor_bitmap_invalid = TRUE;
  sync_cursor_state (source_monitor);
}

static void
on_prepare_frame (ClutterStage            *stage,
                  ClutterStageView        *stage_view,
                  ClutterFrame            *frame,
                  MetaStreamSourceMonitor *source_monitor)
{
  sync_cursor_state (source_monitor);
}

static void
inhibit_hw_cursor (MetaStreamSourceMonitor *source_monitor)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (!source_monitor->hw_cursor_inhibited);

  backend = get_backend (source_monitor);
  inhibitor = META_HW_CURSOR_INHIBITOR (source_monitor);
  meta_backend_add_hw_cursor_inhibitor (backend, inhibitor);

  source_monitor->hw_cursor_inhibited = TRUE;
}

static void
uninhibit_hw_cursor (MetaStreamSourceMonitor *source_monitor)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (source_monitor->hw_cursor_inhibited);

  backend = get_backend (source_monitor);
  inhibitor = META_HW_CURSOR_INHIBITOR (source_monitor);
  meta_backend_remove_hw_cursor_inhibitor (backend, inhibitor);

  source_monitor->hw_cursor_inhibited = FALSE;
}

static void
add_view_watches (MetaStreamSourceMonitor *source_monitor,
                  MetaStageWatchPhase      watch_phase,
                  MetaStageWatchFunc       callback)
{
  MetaBackend *backend = get_backend (source_monitor);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage;
  MetaStage *meta_stage;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;
  GList *l;

  stage = get_stage (source_monitor);
  meta_stage = META_STAGE (stage);
  monitor = get_monitor (source_monitor);
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
                                         source_monitor);

          source_monitor->watches = g_list_prepend (source_monitor->watches, watch);
        }
    }
}

static void
maybe_reattach_watches (MetaStreamSourceMonitor *source_monitor)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_monitor);
  MetaStream *stream;
  ClutterStage *stage;
  GList *l;

  if (!meta_stream_source_is_enabled (source))
    return;

  stream = meta_stream_source_get_stream (source);
  stage = get_stage (source_monitor);

  for (l = source_monitor->watches; l; l = l->next)
    meta_stage_remove_watch (META_STAGE (stage), l->data);
  g_clear_pointer (&source_monitor->watches, g_list_free);

  add_view_watches (source_monitor,
                    META_STAGE_WATCH_BEFORE_PAINT,
                    before_stage_painted);

  switch (meta_stream_get_cursor_mode (stream))
    {
    case META_STREAM_CURSOR_MODE_METADATA:
    case META_STREAM_CURSOR_MODE_HIDDEN:
      add_view_watches (source_monitor,
                        META_STAGE_WATCH_AFTER_ACTOR_PAINT,
                        stage_painted);
      break;
    case META_STREAM_CURSOR_MODE_EMBEDDED:
      add_view_watches (source_monitor,
                        META_STAGE_WATCH_AFTER_PAINT,
                        stage_painted);
      break;
    }
}

static void
on_monitors_changed (MetaMonitorManager      *monitor_manager,
                     MetaStreamSourceMonitor *source_monitor)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_monitor);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStreamMonitor *stream_monitor = META_STREAM_MONITOR (stream);
  MetaMonitor *monitor = meta_stream_monitor_get_monitor (stream_monitor);
  MetaLogicalMonitor *logical_monitor =
    meta_monitor_get_logical_monitor (monitor);
  MtkRectangle layout = meta_logical_monitor_get_layout (logical_monitor);

  g_object_set (G_OBJECT (source), "layout", &layout, NULL);
  maybe_reattach_watches (source_monitor);
}

static void
meta_stream_source_monitor_enable (MetaStreamSource *source)
{
  MetaStreamSourceMonitor *source_monitor = META_STREAM_SOURCE_MONITOR (source);
  MetaBackend *backend = get_backend (source_monitor);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage = get_stage (source_monitor);
  MetaStream *stream;

  stream = meta_stream_source_get_stream (source);

  switch (meta_stream_get_cursor_mode (stream))
    {
    case META_STREAM_CURSOR_MODE_METADATA:
      source_monitor->position_invalidated_handler_id =
        g_signal_connect_after (cursor_tracker, "position-invalidated",
                                G_CALLBACK (pointer_position_invalidated),
                                source_monitor);
      source_monitor->cursor_changed_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-changed",
                                G_CALLBACK (cursor_changed),
                                source_monitor);
      source_monitor->stage_prepare_frame_handler_id =
        g_signal_connect_after (stage, "prepare-frame",
                                G_CALLBACK (on_prepare_frame),
                                source_monitor);
      break;
    case META_STREAM_CURSOR_MODE_HIDDEN:
      break;
    case META_STREAM_CURSOR_MODE_EMBEDDED:
      inhibit_hw_cursor (source_monitor);
      break;
    }

  maybe_reattach_watches (source_monitor);
  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           source_monitor, 0);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (get_stage (source_monitor)));
}

static void
meta_stream_source_monitor_disable (MetaStreamSource *source)
{
  MetaStreamSourceMonitor *source_monitor = META_STREAM_SOURCE_MONITOR (source);
  MetaBackend *backend = get_backend (source_monitor);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage;
  MetaStage *meta_stage;
  GList *l;

  stage = get_stage (source_monitor);
  meta_stage = META_STAGE (stage);

  for (l = source_monitor->watches; l; l = l->next)
    {
      MetaStageWatch *watch = l->data;

      meta_stage_remove_watch (meta_stage, watch);
    }
  g_clear_pointer (&source_monitor->watches, g_list_free);

  if (source_monitor->hw_cursor_inhibited)
    uninhibit_hw_cursor (source_monitor);

  g_clear_signal_handler (&source_monitor->position_invalidated_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&source_monitor->cursor_changed_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&source_monitor->stage_prepare_frame_handler_id,
                          stage);

  g_clear_handle_id (&source_monitor->maybe_record_idle_id, g_source_remove);
}

static gboolean
meta_stream_source_monitor_record_to_buffer (MetaStreamSource      *source,
                                             MetaStreamRecordFlag   flags,
                                             MetaStreamPaintPhase   paint_phase,
                                             int                    width,
                                             int                    height,
                                             int                    stride,
                                             uint8_t               *data,
                                             MtkRegion             *damage,
                                             GError               **error)
{
  MetaStreamSourceMonitor *source_monitor = META_STREAM_SOURCE_MONITOR (source);
  MetaBackend *backend = get_backend (source_monitor);
  CoglFramebuffer *framebuffer = NULL;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  GList *outputs;
  float scale;
  ClutterColorState *color_state;

  monitor = get_monitor (source_monitor);
  outputs = meta_monitor_get_outputs (monitor);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);

  if (meta_backend_is_stage_views_scaled (backend))
    scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    scale = 1.0;

  if (!outputs->next &&
      (paint_phase != META_STREAM_PAINT_PHASE_DETACHED ||
       flags == META_STREAM_RECORD_FLAG_CURSOR_ONLY))
    {
      MetaRenderer *renderer = meta_backend_get_renderer (backend);
      MetaRendererView *renderer_view;
      ClutterStageView *stage_view;
      MetaCrtc *crtc;

      crtc = meta_output_get_assigned_crtc (outputs->data);
      renderer_view = meta_renderer_get_view_for_crtc (renderer, crtc);
      stage_view = CLUTTER_STAGE_VIEW (renderer_view);
      framebuffer = clutter_stage_view_get_framebuffer (stage_view);
    }

  color_state = meta_stream_source_get_color_state (source);

  return meta_stream_source_paint_to_buffer (source,
                                             color_state,
                                             framebuffer,
                                             &logical_monitor->rect, scale,
                                             width, height, stride, data,
                                             COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                             damage,
                                             error);
}

static gboolean
ensure_blending_pipeline (MetaStreamSourceMonitor  *source_monitor,
                          CoglFramebuffer          *target_framebuffer,
                          ClutterColorState        *blending_color_state,
                          ClutterColorState        *target_color_state,
                          CoglPipeline            **blending_pipeline,
                          CoglFramebuffer         **blending_framebuffer,
                          GError                  **error)
{
  MetaBackend *backend = get_backend (source_monitor);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context = clutter_backend_get_cogl_context (clutter_backend);
  int width = cogl_framebuffer_get_width (target_framebuffer);
  int height = cogl_framebuffer_get_height (target_framebuffer);
  ClutterEncodingRequiredFormat required_format;
  CoglPixelFormat formats[10];
  size_t n_formats = 0;
  g_autoptr (CoglTexture) texture = NULL;
  g_autoptr (CoglOffscreen) offscreen = NULL;
  g_autoptr (CoglPipeline) pipeline = NULL;

  if (source_monitor->blending_pipeline && source_monitor->blending_framebuffer)
    {
      *blending_pipeline = source_monitor->blending_pipeline;
      *blending_framebuffer = source_monitor->blending_framebuffer;
      return TRUE;
    }

  required_format = clutter_color_state_required_format (blending_color_state);
  if (required_format <= CLUTTER_ENCODING_REQUIRED_FORMAT_UINT8)
    {
      formats[n_formats++] =
        cogl_framebuffer_get_internal_format (target_framebuffer);
    }
  else
    {
      formats[n_formats++] = COGL_PIXEL_FORMAT_RGBX_FP_16161616;
      formats[n_formats++] = COGL_PIXEL_FORMAT_BGRX_FP_16161616;
      formats[n_formats++] = COGL_PIXEL_FORMAT_XRGB_FP_16161616;
      formats[n_formats++] = COGL_PIXEL_FORMAT_XBGR_FP_16161616;
      formats[n_formats++] = COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE;
      formats[n_formats++] = COGL_PIXEL_FORMAT_BGRA_FP_16161616_PRE;
      formats[n_formats++] = COGL_PIXEL_FORMAT_ARGB_FP_16161616_PRE;
      formats[n_formats++] = COGL_PIXEL_FORMAT_ABGR_FP_16161616_PRE;
    }

  offscreen = cogl_offscreen_new_from_formats (cogl_context,
                                               formats, n_formats,
                                               width, height,
                                               error);
  if (!offscreen)
    return FALSE;

  pipeline = cogl_pipeline_new (cogl_context);
  cogl_pipeline_set_layer_texture (pipeline, 0,
                                   cogl_offscreen_get_texture (offscreen));

  clutter_color_state_add_pipeline_transform (blending_color_state,
                                              target_color_state,
                                              pipeline,
                                              CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE);

  g_set_object (&source_monitor->blending_pipeline, g_steal_pointer (&pipeline));
  g_set_object (&source_monitor->blending_framebuffer,
                COGL_FRAMEBUFFER (g_steal_pointer (&offscreen)));
  *blending_pipeline = source_monitor->blending_pipeline;
  *blending_framebuffer = source_monitor->blending_framebuffer;
  return TRUE;
}

static gboolean
meta_stream_source_monitor_record_to_framebuffer (MetaStreamSource      *source,
                                                  MetaStreamPaintPhase   paint_phase,
                                                  CoglFramebuffer       *framebuffer,
                                                  MtkRegion             *damage,
                                                  GError               **error)
{
  MetaStreamSourceMonitor *source_monitor = META_STREAM_SOURCE_MONITOR (source);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaBackend *backend = get_backend (source_monitor);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage = get_stage (source_monitor);
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
  ClutterColorState *target_color_state =
    meta_stream_source_get_color_state (source);
  ClutterColorState *view_color_state;
  ClutterColorState *output_color_state;

  monitor = get_monitor (source_monitor);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  if (meta_backend_is_stage_views_scaled (backend))
    view_scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    view_scale = 1.0;

  if (paint_phase == META_STREAM_PAINT_PHASE_DETACHED)
    goto stage_paint;

  outputs = meta_monitor_get_outputs (monitor);
  if (outputs->next)
    goto stage_paint;

  crtc = meta_output_get_assigned_crtc (outputs->data);
  renderer_view = meta_renderer_get_view_for_crtc (renderer, crtc);

  g_assert (renderer_view != NULL);

  view = CLUTTER_STAGE_VIEW (renderer_view);
  clutter_stage_view_get_layout (view, &view_layout);

  view_color_state = clutter_stage_view_get_color_state (view);
  output_color_state = clutter_stage_view_get_output_color_state (view);

  switch (paint_phase)
    {
    case META_STREAM_PAINT_PHASE_PRE_PAINT:
      {
        CoglScanout *scanout = clutter_stage_view_peek_scanout (view);

        if (scanout &&
            clutter_color_state_equals (target_color_state, output_color_state))
          {
            g_autoptr (GError) local_error = NULL;

            if (cogl_scanout_copy_to_framebuffer (scanout,
                                                  framebuffer,
                                                  &local_error))
              {
                cogl_framebuffer_flush (framebuffer);
                do_stage_paint = FALSE;
              }
            else
              {
                g_warning ("Error copying to screencast framebuffer: %s",
                           local_error->message);
              }
          }
      }
      break;

    case META_STREAM_PAINT_PHASE_PRE_SWAP_BUFFER:
      {
        CoglFramebuffer *view_framebuffer =
          clutter_stage_view_get_framebuffer (view);
        CoglContext *cogl_context =
          cogl_framebuffer_get_context (view_framebuffer);
        CoglDriver *cogl_driver =
          cogl_context_get_driver (cogl_context);

        if (cogl_driver_has_feature (cogl_driver,
                                     COGL_FEATURE_ID_BLIT_FRAMEBUFFER) &&
            clutter_color_state_equals (target_color_state, view_color_state))
          {
            g_autoptr (GError) local_error = NULL;

            if (damage ?
                cogl_framebuffer_blit_region (view_framebuffer,
                                              framebuffer,
                                              damage,
                                              0, 0,
                                              &local_error) :
                cogl_framebuffer_blit (view_framebuffer,
                                       framebuffer,
                                       0, 0,
                                       0, 0,
                                       cogl_framebuffer_get_width (view_framebuffer),
                                       cogl_framebuffer_get_height (view_framebuffer),
                                       &local_error))
              {
                cogl_framebuffer_flush (framebuffer);
                do_stage_paint = FALSE;
              }
            else
              {
                g_warning ("Failed to blit view framebuffer: %s",
                           local_error->message);
              }
          }
      }
      break;

    case META_STREAM_PAINT_PHASE_DETACHED:
      g_assert_not_reached ();
    }

stage_paint:
  if (do_stage_paint)
    {
      MetaContext *context = meta_backend_get_context (backend);
      MetaDebugControl *debug_control = meta_context_get_debug_control (context);
      ClutterPaintFlag paint_flags = CLUTTER_PAINT_FLAG_CLEAR;
      ClutterColorState *blending_color_state;
      gboolean force_linear;

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

      force_linear = meta_debug_control_is_linear_blending_forced (debug_control);

      blending_color_state = clutter_color_state_get_blending (target_color_state,
                                                               force_linear);

      if (!clutter_color_state_equals (target_color_state, blending_color_state))
        {
          CoglPipeline *blending_pipeline;
          CoglFramebuffer *blending_framebuffer;

          if (!ensure_blending_pipeline (source_monitor,
                                         framebuffer,
                                         blending_color_state,
                                         target_color_state,
                                         &blending_pipeline,
                                         &blending_framebuffer,
                                         error))
            return FALSE;

          clutter_stage_paint_to_framebuffer_clipped (stage,
                                                      blending_framebuffer,
                                                      &logical_monitor_layout,
                                                      view_scale,
                                                      blending_color_state,
                                                      damage,
                                                      paint_flags);
          cogl_framebuffer_draw_textured_rectangle (framebuffer,
                                                    blending_pipeline,
                                                    -1, 1, 1, -1,
                                                    0, 0, 1, 1);
          cogl_framebuffer_flush (framebuffer);
        }
      else
        {
          clutter_stage_paint_to_framebuffer_clipped (stage,
                                                      framebuffer,
                                                      &logical_monitor_layout,
                                                      view_scale,
                                                      target_color_state,
                                                      damage,
                                                      paint_flags);
        }
    }

  return TRUE;
}

static void
meta_stream_source_monitor_queue_follow_up (MetaStreamSource  *source,
                                            MetaStreamRecordFlag  flags)
{
  MetaStreamSourceMonitor *source_monitor = META_STREAM_SOURCE_MONITOR (source);
  MetaBackend *backend = get_backend (source_monitor);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  ClutterStage *stage = get_stage (source_monitor);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;
  GList *l;

  if (flags & META_STREAM_RECORD_FLAG_CURSOR_ONLY &&
      !source_monitor->maybe_record_idle_id)
    {
      clutter_stage_schedule_update (stage);
      return;
    }

  g_clear_handle_id (&source_monitor->maybe_record_idle_id, g_source_remove);

  monitor = get_monitor (source_monitor);
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

static gboolean
should_cursor_metadata_be_set (MetaStreamSourceMonitor *source_monitor)
{
  MetaBackend *backend = get_backend (source_monitor);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);

  return (meta_cursor_tracker_get_pointer_visible (cursor_tracker) &&
          is_cursor_in_stream (source_monitor));
}

static float
get_view_scale (MetaStreamSourceMonitor *source_monitor)
{
  MetaBackend *backend = get_backend (source_monitor);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;

  monitor = get_monitor (source_monitor);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);

  if (meta_backend_is_stage_views_scaled (backend))
    return meta_logical_monitor_get_scale (logical_monitor);
  else
    return 1.0;
}

static void
get_cursor_position (MetaStreamSourceMonitor *source_monitor,
                     int                     *out_x,
                     int                     *out_y)
{
  MetaBackend *backend = get_backend (source_monitor);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle logical_monitor_layout;
  graphene_rect_t logical_monitor_rect;
  float view_scale;
  graphene_point_t cursor_position;

  monitor = get_monitor (source_monitor);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);
  logical_monitor_rect =
    mtk_rectangle_to_graphene_rect (&logical_monitor_layout);

  view_scale = get_view_scale (source_monitor);

  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
  cursor_position.x -= logical_monitor_rect.origin.x;
  cursor_position.y -= logical_monitor_rect.origin.y;
  cursor_position.x *= view_scale;
  cursor_position.y *= view_scale;

  *out_x = (int) roundf (cursor_position.x);
  *out_y = (int) roundf (cursor_position.y);
}

static void
meta_stream_source_monitor_set_cursor_metadata (MetaStreamSource       *source,
                                                struct spa_meta_cursor *spa_meta_cursor)
{
  MetaStreamSourceMonitor *source_monitor = META_STREAM_SOURCE_MONITOR (source);
  MetaBackend *backend = get_backend (source_monitor);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  ClutterCursor *cursor;
  int x, y;

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);

  if (!should_cursor_metadata_be_set (source_monitor))
    {
      source_monitor->last_cursor_matadata.set = FALSE;
      meta_stream_source_unset_cursor_metadata (source, spa_meta_cursor);
      return;
    }

  get_cursor_position (source_monitor, &x, &y);

  source_monitor->last_cursor_matadata.set = TRUE;
  source_monitor->last_cursor_matadata.x = x;
  source_monitor->last_cursor_matadata.y = y;

  if (source_monitor->cursor_bitmap_invalid)
    {
      if (cursor)
        {
          float view_scale;

          view_scale = get_view_scale (source_monitor);

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

      source_monitor->cursor_bitmap_invalid = FALSE;
    }
  else
    {
      meta_stream_source_set_cursor_position_metadata (source,
                                                       spa_meta_cursor,
                                                       x, y);
    }
}

static void
clear_format (MetaStreamFormat *format)
{
  g_clear_object (&format->color_state);
}

static void
update_formats (MetaStreamSourceMonitor *source_monitor)
{
  MetaMonitor *monitor = get_monitor (source_monitor);
  MetaBackend *backend = meta_monitor_get_backend (monitor);
  MetaColorManager *color_manager = meta_backend_get_color_manager (backend);
  MetaColorDevice *color_device;
  ClutterColorState *color_state = NULL;
  int i;
  const CoglPixelFormat basic_formats[] = {
    COGL_PIXEL_FORMAT_BGRX_8888,
    COGL_PIXEL_FORMAT_BGRA_8888_PRE,
  };

  color_device = meta_color_manager_get_color_device (color_manager, monitor);
  if (color_device)
    color_state = meta_color_device_get_color_state (color_device);

  g_clear_pointer (&source_monitor->formats, g_array_unref);
  source_monitor->formats = g_array_new (TRUE, TRUE, sizeof (MetaStreamFormat));
  g_array_set_clear_func (source_monitor->formats, (GDestroyNotify) clear_format);

  for (i = 0; i < G_N_ELEMENTS (basic_formats); i++)
    {
      MetaStreamFormat format = {
        .format = basic_formats[i],
      };

      g_array_append_val (source_monitor->formats, format);
    }

  if (CLUTTER_IS_COLOR_STATE_PARAMS (color_state))
    {
      ClutterColorStateParams *color_state_params =
        CLUTTER_COLOR_STATE_PARAMS (color_state);
      const ClutterColorimetry *colorimetry;
      const ClutterEOTF *eotf;

      colorimetry =
        clutter_color_state_params_get_colorimetry (color_state_params);
      eotf = clutter_color_state_params_get_eotf (color_state_params);

      if (colorimetry->type == CLUTTER_COLORIMETRY_TYPE_COLORSPACE &&
          colorimetry->colorspace == CLUTTER_COLORSPACE_BT2020 &&
          eotf->type == CLUTTER_EOTF_TYPE_NAMED &&
          eotf->tf_name == CLUTTER_TRANSFER_FUNCTION_PQ)
        {
          const CoglPixelFormat hdr_formats[] = {
              COGL_PIXEL_FORMAT_XRGB_2101010,
              COGL_PIXEL_FORMAT_XBGR_2101010,
              COGL_PIXEL_FORMAT_RGBA_FP_16161616_PRE,
          };

          for (i = 0; i < G_N_ELEMENTS (hdr_formats); i++)
            {
              MetaStreamFormat format = {
                .format = hdr_formats[i],
                .color_state = g_object_ref (color_state),
              };

              g_array_append_val (source_monitor->formats, format);
            }
        }
    }
}

static const MetaStreamFormat *
meta_stream_source_monitor_get_formats (MetaStreamSource *source)
{
  MetaStreamSourceMonitor *source_monitor = META_STREAM_SOURCE_MONITOR (source);

  update_formats (source_monitor);

  return (const MetaStreamFormat *) source_monitor->formats->data;
}

static gboolean
meta_stream_source_monitor_is_cursor_inhibited (MetaHwCursorInhibitor *inhibitor)
{
  MetaStreamSourceMonitor *source_monitor =
    META_STREAM_SOURCE_MONITOR (inhibitor);

  return is_cursor_in_stream (source_monitor);
}

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface)
{
  iface->is_cursor_inhibited =
    meta_stream_source_monitor_is_cursor_inhibited;
}

static void
meta_stream_source_monitor_finalize (GObject *object)
{
  MetaStreamSourceMonitor *source_monitor = META_STREAM_SOURCE_MONITOR (object);

  g_clear_pointer (&source_monitor->formats, g_array_unref);
  g_clear_object (&source_monitor->blending_pipeline);
  g_clear_object (&source_monitor->blending_framebuffer);

  G_OBJECT_CLASS (meta_stream_source_monitor_parent_class)->finalize (object);
}

static void
meta_stream_source_monitor_class_init (MetaStreamSourceMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaStreamSourceClass *source_class =
    META_STREAM_SOURCE_CLASS (klass);

  object_class->finalize = meta_stream_source_monitor_finalize;

  source_class->get_specs = meta_stream_source_monitor_get_specs;
  source_class->enable = meta_stream_source_monitor_enable;
  source_class->disable = meta_stream_source_monitor_disable;
  source_class->record_to_buffer =
    meta_stream_source_monitor_record_to_buffer;
  source_class->record_to_framebuffer =
    meta_stream_source_monitor_record_to_framebuffer;
  source_class->queue_follow_up =
    meta_stream_source_monitor_queue_follow_up;
  source_class->set_cursor_metadata =
    meta_stream_source_monitor_set_cursor_metadata;
  source_class->get_formats =
    meta_stream_source_monitor_get_formats;
}

static void
meta_stream_source_monitor_init (MetaStreamSourceMonitor *source_monitor)
{
  source_monitor->cursor_bitmap_invalid = TRUE;
}

MetaStreamSourceMonitor *
meta_stream_source_monitor_new (MetaStreamMonitor  *stream_monitor,
                                GError            **error)
{
  MetaMonitor *monitor =
    meta_stream_monitor_get_monitor (stream_monitor);
  MetaLogicalMonitor *logical_monitor =
    meta_monitor_get_logical_monitor (monitor);
  MtkRectangle layout = meta_logical_monitor_get_layout (logical_monitor);

  return g_initable_new (META_TYPE_STREAM_SOURCE_MONITOR, NULL, error,
                         "stream", stream_monitor,
                         "layout", &layout,
                         NULL);
}
