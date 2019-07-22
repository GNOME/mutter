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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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

  MetaStageWatch *paint_watch;
  MetaStageWatch *after_paint_watch;

  gulong cursor_moved_handler_id;
  gulong cursor_changed_handler_id;
};

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastMonitorStreamSrc,
                         meta_screen_cast_monitor_stream_src,
                         META_TYPE_SCREEN_CAST_STREAM_SRC,
                         G_IMPLEMENT_INTERFACE (META_TYPE_HW_CURSOR_INHIBITOR,
                                                hw_cursor_inhibitor_iface_init))

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

static void
meta_screen_cast_monitor_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                               int                     *width,
                                               int                     *height,
                                               float                   *frame_rate)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  float scale;
  MetaMonitorMode *mode;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  mode = meta_monitor_get_current_mode (monitor);

  if (meta_is_stage_views_scaled ())
    scale = logical_monitor->scale;
  else
    scale = 1.0;

  *width = (int) roundf (logical_monitor->rect.width * scale);
  *height = (int) roundf (logical_monitor->rect.height * scale);
  *frame_rate = meta_monitor_mode_get_refresh_rate (mode);
}

static void
stage_painted (MetaStage        *stage,
               ClutterStageView *view,
               gpointer          user_data)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (user_data);

  meta_screen_cast_stream_src_maybe_record_frame (src);
}

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

static gboolean
is_cursor_in_stream (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaBackend *backend = get_backend (monitor_src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MetaRectangle logical_monitor_layout;
  ClutterRect logical_monitor_rect;
  MetaCursorSprite *cursor_sprite;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);
  logical_monitor_rect =
    meta_rectangle_to_clutter_rect (&logical_monitor_layout);

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (cursor_sprite)
    {
      ClutterRect cursor_rect;

      cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                         cursor_sprite);
      return clutter_rect_intersection (&cursor_rect,
                                        &logical_monitor_rect,
                                        NULL);
    }
  else
    {
      ClutterPoint cursor_position;

      cursor_position = meta_cursor_renderer_get_position (cursor_renderer);
      return clutter_rect_contains_point (&logical_monitor_rect,
                                          &cursor_position);
    }
}

static void
sync_cursor_state (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  ClutterStage *stage = get_stage (monitor_src);

  if (!is_cursor_in_stream (monitor_src))
    return;

  if (clutter_stage_is_redraw_queued (stage))
    return;

  meta_screen_cast_stream_src_maybe_record_frame (src);
}

static void
cursor_moved (MetaCursorTracker              *cursor_tracker,
              float                           x,
              float                           y,
              MetaScreenCastMonitorStreamSrc *monitor_src)
{
  sync_cursor_state (monitor_src);
}

static void
cursor_changed (MetaCursorTracker              *cursor_tracker,
                MetaScreenCastMonitorStreamSrc *monitor_src)
{
  monitor_src->cursor_bitmap_invalid = TRUE;
  sync_cursor_state (monitor_src);
}

static MetaCursorRenderer *
get_cursor_renderer (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastSession *session = meta_screen_cast_stream_get_session (stream);
  MetaScreenCast *screen_cast =
    meta_screen_cast_session_get_screen_cast (session);
  MetaBackend *backend = meta_screen_cast_get_backend (screen_cast);

  return meta_backend_get_cursor_renderer (backend);
}

static void
inhibit_hw_cursor (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaCursorRenderer *cursor_renderer;
  MetaHwCursorInhibitor *inhibitor;

  cursor_renderer = get_cursor_renderer (monitor_src);
  inhibitor = META_HW_CURSOR_INHIBITOR (monitor_src);
  meta_cursor_renderer_add_hw_cursor_inhibitor (cursor_renderer, inhibitor);
}

static void
uninhibit_hw_cursor (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaCursorRenderer *cursor_renderer;
  MetaHwCursorInhibitor *inhibitor;

  cursor_renderer = get_cursor_renderer (monitor_src);
  inhibitor = META_HW_CURSOR_INHIBITOR (monitor_src);
  meta_cursor_renderer_remove_hw_cursor_inhibitor (cursor_renderer, inhibitor);
}

static void
meta_screen_cast_monitor_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  MetaRendererView *view;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MetaStage *meta_stage;
  ClutterStageView *stage_view;
  ClutterStage *stage;
  MetaScreenCastStream *stream;

  stream = meta_screen_cast_stream_src_get_stream (src);
  stage = get_stage (monitor_src);
  meta_stage = META_STAGE (stage);
  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  view = meta_renderer_get_view_from_logical_monitor (renderer,
                                                      logical_monitor);

  if (view)
    stage_view = CLUTTER_STAGE_VIEW (view);
  else
    stage_view = NULL;

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
      monitor_src->cursor_moved_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-moved",
                                G_CALLBACK (cursor_moved),
                                monitor_src);
      monitor_src->cursor_changed_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-changed",
                                G_CALLBACK (cursor_changed),
                                monitor_src);
      /* Intentional fall-through */
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      monitor_src->paint_watch =
        meta_stage_watch_view (meta_stage,
                               stage_view,
                               META_STAGE_WATCH_AFTER_ACTOR_PAINT,
                               stage_painted,
                               monitor_src);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      inhibit_hw_cursor (monitor_src);
      monitor_src->after_paint_watch =
        meta_stage_watch_view (meta_stage,
                               stage_view,
                               META_STAGE_WATCH_AFTER_PAINT,
                               stage_painted,
                               monitor_src);
      break;
    }

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
}

static void
meta_screen_cast_monitor_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage;
  MetaStage *meta_stage;

  stage = get_stage (monitor_src);
  meta_stage = META_STAGE (stage);

  if (monitor_src->paint_watch)
    {
      meta_stage_remove_watch (meta_stage, monitor_src->paint_watch);
      monitor_src->paint_watch = NULL;
    }

  if (monitor_src->after_paint_watch)
    {
      meta_stage_remove_watch (meta_stage, monitor_src->after_paint_watch);
      monitor_src->after_paint_watch = NULL;
      uninhibit_hw_cursor (monitor_src);
    }

  if (monitor_src->cursor_moved_handler_id)
    {
      g_signal_handler_disconnect (cursor_tracker,
                                   monitor_src->cursor_moved_handler_id);
      monitor_src->cursor_moved_handler_id = 0;
    }

  if (monitor_src->cursor_changed_handler_id)
    {
      g_signal_handler_disconnect (cursor_tracker,
                                   monitor_src->cursor_changed_handler_id);
      monitor_src->cursor_changed_handler_id = 0;
    }
}

static gboolean
meta_screen_cast_monitor_stream_src_record_frame (MetaScreenCastStreamSrc *src,
                                                  uint8_t                 *data)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  ClutterStage *stage;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;

  stage = get_stage (monitor_src);
  if (!clutter_stage_is_redraw_queued (stage))
    return FALSE;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  clutter_stage_capture_into (stage, FALSE, &logical_monitor->rect, data);

  return TRUE;
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
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaCursorSprite *cursor_sprite;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MetaRectangle logical_monitor_layout;
  ClutterRect logical_monitor_rect;
  MetaRendererView *view;
  float view_scale;
  ClutterPoint cursor_position;
  int x, y;

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);

  if (!is_cursor_in_stream (monitor_src))
    {
      meta_screen_cast_stream_src_unset_cursor_metadata (src,
                                                         spa_meta_cursor);
      return;
    }

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);
  logical_monitor_rect =
    meta_rectangle_to_clutter_rect (&logical_monitor_layout);

  view = meta_renderer_get_view_from_logical_monitor (renderer,
                                                      logical_monitor);
  if (view)
    view_scale = clutter_stage_view_get_scale (CLUTTER_STAGE_VIEW (view));
  else
    view_scale = 1.0;

  cursor_position = meta_cursor_renderer_get_position (cursor_renderer);
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

          cursor_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);
          scale = view_scale * cursor_scale;
          meta_screen_cast_stream_src_set_cursor_sprite_metadata (src,
                                                                  spa_meta_cursor,
                                                                  cursor_sprite,
                                                                  x, y,
                                                                  scale);
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
meta_screen_cast_monitor_stream_src_is_cursor_sprite_inhibited (MetaHwCursorInhibitor *inhibitor,
                                                                MetaCursorSprite      *cursor_sprite)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (inhibitor);

  return is_cursor_in_stream (monitor_src);
}

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface)
{
  iface->is_cursor_sprite_inhibited =
    meta_screen_cast_monitor_stream_src_is_cursor_sprite_inhibited;
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
  src_class->record_frame = meta_screen_cast_monitor_stream_src_record_frame;
  src_class->set_cursor_metadata =
    meta_screen_cast_monitor_stream_src_set_cursor_metadata;
}
