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
  gboolean hw_cursor_inhibited;

  GList *watches;

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
stage_painted (MetaStage           *stage,
               ClutterStageView    *view,
               ClutterPaintContext *paint_context,
               gpointer             user_data)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (user_data);
  MetaScreenCastRecordFlag flags;

  flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags);
}

static void
before_stage_painted (MetaStage           *stage,
                      ClutterStageView    *view,
                      ClutterPaintContext *paint_context,
                      gpointer             user_data)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (user_data);
  CoglScanout *scanout;

  scanout = clutter_stage_view_peek_scanout (view);
  if (scanout)
    {
      MetaScreenCastRecordFlag flags;

      flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
      meta_screen_cast_stream_src_maybe_record_frame (src, flags);
    }
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
  graphene_rect_t logical_monitor_rect;
  MetaCursorSprite *cursor_sprite;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);
  logical_monitor_rect =
    meta_rectangle_to_graphene_rect (&logical_monitor_layout);

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
      graphene_point_t cursor_position;

      cursor_position = meta_cursor_renderer_get_position (cursor_renderer);
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
  MetaScreenCastRecordFlag flags;

  if (is_redraw_queued (monitor_src))
    return;

  if (meta_screen_cast_stream_src_pending_follow_up_frame (src))
    return;

  flags = META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags);
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

  g_return_if_fail (!monitor_src->hw_cursor_inhibited);

  cursor_renderer = get_cursor_renderer (monitor_src);
  inhibitor = META_HW_CURSOR_INHIBITOR (monitor_src);
  meta_cursor_renderer_add_hw_cursor_inhibitor (cursor_renderer, inhibitor);

  monitor_src->hw_cursor_inhibited = TRUE;
}

static void
uninhibit_hw_cursor (MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaCursorRenderer *cursor_renderer;
  MetaHwCursorInhibitor *inhibitor;

  g_return_if_fail (monitor_src->hw_cursor_inhibited);

  cursor_renderer = get_cursor_renderer (monitor_src);
  inhibitor = META_HW_CURSOR_INHIBITOR (monitor_src);
  meta_cursor_renderer_remove_hw_cursor_inhibitor (cursor_renderer, inhibitor);

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
  MetaRectangle logical_monitor_layout;
  GList *l;

  stage = get_stage (monitor_src);
  meta_stage = META_STAGE (stage);
  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      MetaRendererView *view = l->data;
      MetaRectangle view_layout;

      clutter_stage_view_get_layout (CLUTTER_STAGE_VIEW (view), &view_layout);
      if (meta_rectangle_overlap (&logical_monitor_layout, &view_layout))
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
meta_screen_cast_monitor_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage;
  MetaScreenCastStream *stream;

  stream = meta_screen_cast_stream_src_get_stream (src);
  stage = get_stage (monitor_src);

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
      meta_cursor_tracker_track_position (cursor_tracker);
      G_GNUC_FALLTHROUGH;
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      add_view_watches (monitor_src,
                        META_STAGE_WATCH_BEFORE_PAINT,
                        before_stage_painted);
      add_view_watches (monitor_src,
                        META_STAGE_WATCH_AFTER_ACTOR_PAINT,
                        stage_painted);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      inhibit_hw_cursor (monitor_src);
      meta_cursor_tracker_track_position (cursor_tracker);
      add_view_watches (monitor_src,
                        META_STAGE_WATCH_AFTER_PAINT,
                        stage_painted);
      break;
    }

  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
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

  g_clear_signal_handler (&monitor_src->cursor_moved_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&monitor_src->cursor_changed_handler_id,
                          cursor_tracker);

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

static void
maybe_paint_cursor_sprite (MetaScreenCastMonitorStreamSrc *monitor_src,
                           uint8_t                        *data)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorSprite *cursor_sprite;
  CoglTexture *sprite_texture;
  int sprite_width, sprite_height, sprite_stride;
  float sprite_scale;
  uint8_t *sprite_data;
  cairo_surface_t *sprite_surface;
  graphene_rect_t sprite_rect;
  int width, height, stride;
  cairo_surface_t *surface;
  cairo_t *cr;

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (!cursor_sprite)
    return;

  if (meta_cursor_renderer_is_overlay_visible (cursor_renderer))
    return;

  sprite_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                     cursor_sprite);
  sprite_texture = meta_cursor_sprite_get_cogl_texture (cursor_sprite);
  sprite_width = cogl_texture_get_width (sprite_texture);
  sprite_height = cogl_texture_get_height (sprite_texture);
  sprite_stride = sprite_width * 4;
  sprite_scale = meta_cursor_sprite_get_texture_scale (cursor_sprite);
  sprite_data = g_new0 (uint8_t, sprite_stride * sprite_height);
  cogl_texture_get_data (sprite_texture,
                         CLUTTER_CAIRO_FORMAT_ARGB32,
                         sprite_stride,
                         sprite_data);
  sprite_surface = cairo_image_surface_create_for_data (sprite_data,
                                                        CAIRO_FORMAT_ARGB32,
                                                        sprite_width,
                                                        sprite_height,
                                                        sprite_stride);
  cairo_surface_set_device_scale (sprite_surface, sprite_scale, sprite_scale);

  stride = meta_screen_cast_stream_src_get_stride (src);
  width = meta_screen_cast_stream_src_get_width (src);
  height = meta_screen_cast_stream_src_get_height (src);
  surface = cairo_image_surface_create_for_data (data,
                                                 CAIRO_FORMAT_ARGB32,
                                                 width, height, stride);

  cr = cairo_create (surface);
  cairo_set_source_surface (cr, sprite_surface,
                            sprite_rect.origin.x,
                            sprite_rect.origin.y);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_destroy (sprite_surface);
  cairo_surface_destroy (surface);
  g_free (sprite_data);
}

static gboolean
meta_screen_cast_monitor_stream_src_record_to_buffer (MetaScreenCastStreamSrc  *src,
                                                      uint8_t                  *data,
                                                      GError                  **error)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  ClutterStage *stage;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  stage = get_stage (monitor_src);
  clutter_stage_capture_into (stage, FALSE, &logical_monitor->rect, data);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      maybe_paint_cursor_sprite (monitor_src, data);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      break;
    }

  return TRUE;
}

static gboolean
meta_screen_cast_monitor_stream_src_record_to_framebuffer (MetaScreenCastStreamSrc  *src,
                                                           CoglFramebuffer          *framebuffer,
                                                           GError                  **error)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  MetaBackend *backend = get_backend (monitor_src);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MetaRectangle logical_monitor_layout;
  GList *l;
  float view_scale;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  if (meta_is_stage_views_scaled ())
    view_scale = meta_logical_monitor_get_scale (logical_monitor);
  else
    view_scale = 1.0;

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      ClutterStageView *view = CLUTTER_STAGE_VIEW (l->data);
      CoglFramebuffer *view_framebuffer;
      CoglScanout *scanout;
      MetaRectangle view_layout;
      int x, y;

      clutter_stage_view_get_layout (view, &view_layout);

      if (!meta_rectangle_overlap (&logical_monitor_layout, &view_layout))
        continue;

      x = (int) roundf ((view_layout.x - logical_monitor_layout.x) * view_scale);
      y = (int) roundf ((view_layout.y - logical_monitor_layout.y) * view_scale);

      scanout = clutter_stage_view_peek_scanout (view);
      if (scanout)
        {
          if (!cogl_scanout_blit_to_framebuffer (scanout,
                                                 framebuffer,
                                                 x, y,
                                                 error))
            return FALSE;
        }
      else
        {
          view_framebuffer = clutter_stage_view_get_framebuffer (view);
          if (!cogl_blit_framebuffer (view_framebuffer,
                                      framebuffer,
                                      0, 0,
                                      x, y,
                                      cogl_framebuffer_get_width (view_framebuffer),
                                      cogl_framebuffer_get_height (view_framebuffer),
                                      error))
            return FALSE;
        }
    }

  cogl_framebuffer_finish (framebuffer);

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
  MetaRectangle logical_monitor_layout;
  GList *l;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  for (l = meta_renderer_get_views (renderer); l; l = l->next)
    {
      MetaRendererView *view = l->data;
      MetaRectangle view_layout;
      MetaRectangle damage;

      clutter_stage_view_get_layout (CLUTTER_STAGE_VIEW (view), &view_layout);

      if (!meta_rectangle_overlap (&logical_monitor_layout, &view_layout))
        continue;

      damage = (cairo_rectangle_int_t) {
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
  MetaRectangle logical_monitor_layout;
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
    meta_rectangle_to_graphene_rect (&logical_monitor_layout);

  if (meta_is_stage_views_scaled ())
    view_scale = meta_logical_monitor_get_scale (logical_monitor);
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
  src_class->record_to_buffer =
    meta_screen_cast_monitor_stream_src_record_to_buffer;
  src_class->record_to_framebuffer =
    meta_screen_cast_monitor_stream_src_record_to_framebuffer;
  src_class->record_follow_up =
    meta_screen_cast_monitor_stream_record_follow_up;
  src_class->set_cursor_metadata =
    meta_screen_cast_monitor_stream_src_set_cursor_metadata;
}
