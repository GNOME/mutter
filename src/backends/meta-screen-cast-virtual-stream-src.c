/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include "backends/meta-screen-cast-virtual-stream-src.h"

#include "backends/meta-crtc-mode.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-eis-viewport.h"
#include "backends/meta-monitor.h"
#include "backends/meta-output.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-stage-private.h"
#include "backends/meta-virtual-monitor.h"
#include "core/boxes-private.h"

struct _MetaScreenCastVirtualStreamSrc
{
  MetaScreenCastStreamSrc parent;

  MetaVirtualMonitor *virtual_monitor;

  gboolean cursor_bitmap_invalid;
  gboolean hw_cursor_inhibited;

  MetaStageWatch *watch;

  gulong position_invalidated_handler_id;
  gulong cursor_changed_handler_id;
  gulong prepare_frame_handler_id;

  gulong monitors_changed_handler_id;
};

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastVirtualStreamSrc,
                         meta_screen_cast_virtual_stream_src,
                         META_TYPE_SCREEN_CAST_STREAM_SRC,
                         G_IMPLEMENT_INTERFACE (META_TYPE_HW_CURSOR_INHIBITOR,
                                                hw_cursor_inhibitor_iface_init))

static gboolean
meta_screen_cast_virtual_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                               int                     *width,
                                               int                     *height,
                                               float                   *frame_rate)
{
  return FALSE;
}

static MetaBackend *
backend_from_src (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaScreenCastSession *session = meta_screen_cast_stream_get_session (stream);
  MetaScreenCast *screen_cast =
    meta_screen_cast_session_get_screen_cast (session);

  return meta_screen_cast_get_backend (screen_cast);
}

static ClutterStageView *
view_from_src (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaVirtualMonitor *virtual_monitor = virtual_src->virtual_monitor;
  MetaCrtc *crtc = meta_virtual_monitor_get_crtc (virtual_monitor);
  MetaRenderer *renderer = meta_backend_get_renderer (backend_from_src (src));
  MetaRendererView *view = meta_renderer_get_view_for_crtc (renderer, crtc);

  return CLUTTER_STAGE_VIEW (view);
}

static ClutterStage *
stage_from_src (MetaScreenCastStreamSrc *src)
{
  return CLUTTER_STAGE (meta_backend_get_stage (backend_from_src (src)));
}

static gboolean
is_redraw_queued (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);

  return clutter_stage_is_redraw_queued_on_view (stage_from_src (src),
                                                 view_from_src (src));
}

ClutterStageView *
meta_screen_cast_virtual_stream_src_get_view (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  return view_from_src (META_SCREEN_CAST_STREAM_SRC (virtual_src));
}

MetaLogicalMonitor *
meta_screen_cast_virtual_stream_src_logical_monitor (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaVirtualMonitor *virtual_monitor = virtual_src->virtual_monitor;
  MetaOutput *output = meta_virtual_monitor_get_output (virtual_monitor);
  MetaMonitor *monitor = meta_output_get_monitor (output);

  return meta_monitor_get_logical_monitor (monitor);
}

static void
sync_cursor_state (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaScreenCastPaintPhase paint_phase;
  MetaScreenCastRecordFlag flags;

  if (is_redraw_queued (virtual_src))
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
pointer_position_invalidated (MetaCursorTracker       *cursor_tracker,
                              MetaScreenCastStreamSrc *src)
{
  ClutterStage *stage = stage_from_src (src);

  clutter_stage_schedule_update (stage);
}

static void
cursor_changed (MetaCursorTracker              *cursor_tracker,
                MetaScreenCastVirtualStreamSrc *virtual_src)
{
  virtual_src->cursor_bitmap_invalid = TRUE;
  sync_cursor_state (virtual_src);
}

static void
on_prepare_frame (ClutterStage                   *stage,
                  ClutterStageView               *stage_view,
                  ClutterFrame                   *frame,
                  MetaScreenCastVirtualStreamSrc *virtual_src)
{
  sync_cursor_state (virtual_src);
}

static void
inhibit_hw_cursor (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (!virtual_src->hw_cursor_inhibited);

  backend = backend_from_src (META_SCREEN_CAST_STREAM_SRC (virtual_src));
  inhibitor = META_HW_CURSOR_INHIBITOR (virtual_src);
  meta_backend_add_hw_cursor_inhibitor (backend, inhibitor);

  virtual_src->hw_cursor_inhibited = TRUE;
}

static void
uninhibit_hw_cursor (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaHwCursorInhibitor *inhibitor;
  MetaBackend *backend;

  g_return_if_fail (virtual_src->hw_cursor_inhibited);

  backend = backend_from_src (META_SCREEN_CAST_STREAM_SRC (virtual_src));
  inhibitor = META_HW_CURSOR_INHIBITOR (virtual_src);
  meta_backend_remove_hw_cursor_inhibitor (backend, inhibitor);

  virtual_src->hw_cursor_inhibited = FALSE;
}

static void
actors_painted (MetaStage        *stage,
                ClutterStageView *view,
                const MtkRegion  *redraw_clip,
                ClutterFrame     *frame,
                gpointer          user_data)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (user_data);
  MetaScreenCastPaintPhase paint_phase;
  MetaScreenCastRecordFlag flags;

  flags = META_SCREEN_CAST_RECORD_FLAG_NONE;
  paint_phase = META_SCREEN_CAST_PAINT_PHASE_PRE_SWAP_BUFFER;
  meta_screen_cast_stream_src_maybe_record_frame (src, flags,
                                                  paint_phase,
                                                  redraw_clip);
}

static void
add_watch (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaStage *meta_stage = META_STAGE (stage_from_src (src));

  g_return_if_fail (!virtual_src->watch);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      virtual_src->watch = meta_stage_watch_view (meta_stage,
                                                  view_from_src (src),
                                                  META_STAGE_WATCH_AFTER_ACTOR_PAINT,
                                                  actors_painted,
                                                  virtual_src);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      virtual_src->watch = meta_stage_watch_view (meta_stage,
                                                  view_from_src (src),
                                                  META_STAGE_WATCH_AFTER_PAINT,
                                                  actors_painted,
                                                  virtual_src);
      break;
    }
}

static void
on_monitors_changed (MetaMonitorManager             *monitor_manager,
                     MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaStage *stage = META_STAGE (stage_from_src (src));
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);

  meta_stage_remove_watch (stage, virtual_src->watch);
  virtual_src->watch = NULL;
  add_watch (virtual_src);

  meta_eis_viewport_notify_changed (META_EIS_VIEWPORT (stream));
}

static void
init_record_callbacks (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaBackend *backend = backend_from_src (src);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage = stage_from_src (src);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
      virtual_src->position_invalidated_handler_id =
        g_signal_connect_after (cursor_tracker, "position-invalidated",
                                G_CALLBACK (pointer_position_invalidated),
                                virtual_src);
      virtual_src->cursor_changed_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-changed",
                                G_CALLBACK (cursor_changed),
                                virtual_src);
      virtual_src->prepare_frame_handler_id =
        g_signal_connect_after (stage, "prepare-frame",
                                G_CALLBACK (on_prepare_frame),
                                virtual_src);
      G_GNUC_FALLTHROUGH;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      add_watch (virtual_src);
      break;
    }

  meta_screen_cast_stream_notify_is_configured (stream);

  if (meta_screen_cast_stream_get_cursor_mode (stream) ==
      META_SCREEN_CAST_CURSOR_MODE_EMBEDDED)
    inhibit_hw_cursor (virtual_src);

  virtual_src->monitors_changed_handler_id =
    g_signal_connect (monitor_manager, "monitors-changed-internal",
                      G_CALLBACK (on_monitors_changed),
                      virtual_src);
}

static void
meta_screen_cast_virtual_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaBackend *backend = backend_from_src (src);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      meta_cursor_tracker_track_position (cursor_tracker);
      break;
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      break;
    }

  init_record_callbacks (virtual_src);
  clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage_from_src (src)),
                                        NULL);
  clutter_stage_schedule_update (stage_from_src (src));
}

static void
meta_screen_cast_virtual_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaBackend *backend = backend_from_src (src);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterStage *stage = stage_from_src (src);

  if (virtual_src->hw_cursor_inhibited)
    uninhibit_hw_cursor (virtual_src);

  if (virtual_src->watch)
    {
      meta_stage_remove_watch (META_STAGE (stage_from_src (src)),
                               virtual_src->watch);
      virtual_src->watch = NULL;
    }

  g_clear_signal_handler (&virtual_src->position_invalidated_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&virtual_src->cursor_changed_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&virtual_src->prepare_frame_handler_id,
                          stage);

  g_clear_signal_handler (&virtual_src->monitors_changed_handler_id,
                          monitor_manager);

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
meta_screen_cast_virtual_stream_src_record_to_buffer (MetaScreenCastStreamSrc   *src,
                                                      MetaScreenCastPaintPhase   paint_phase,
                                                      int                        width,
                                                      int                        height,
                                                      int                        stride,
                                                      uint8_t                   *data,
                                                      GError                   **error)
{
  MetaScreenCastStream *stream;
  ClutterPaintFlag paint_flags;
  ClutterStageView *view;
  MtkRectangle view_rect;
  float scale;

  stream = meta_screen_cast_stream_src_get_stream (src);
  view = view_from_src (src);
  scale = clutter_stage_view_get_scale (view);
  clutter_stage_view_get_layout (view, &view_rect);

  paint_flags = CLUTTER_PAINT_FLAG_CLEAR;
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

  if (!clutter_stage_paint_to_buffer (stage_from_src (src),
                                      &view_rect,
                                      scale,
                                      data,
                                      stride,
                                      COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                      paint_flags,
                                      error))
    return FALSE;

  return TRUE;
}

static gboolean
meta_screen_cast_virtual_stream_src_record_to_framebuffer (MetaScreenCastStreamSrc   *src,
                                                           MetaScreenCastPaintPhase   paint_phase,
                                                           CoglFramebuffer           *framebuffer,
                                                           GError                   **error)
{
  ClutterStageView *view;
  CoglFramebuffer *view_framebuffer;

  view = view_from_src (src);
  view_framebuffer = clutter_stage_view_get_framebuffer (view);
  if (!cogl_blit_framebuffer (view_framebuffer,
                              framebuffer,
                              0, 0,
                              0, 0,
                              cogl_framebuffer_get_width (view_framebuffer),
                              cogl_framebuffer_get_height (view_framebuffer),
                              error))
    return FALSE;

  cogl_framebuffer_flush (framebuffer);
  return TRUE;
}

static void
meta_screen_cast_virtual_stream_record_follow_up (MetaScreenCastStreamSrc *src)
{
  MtkRectangle damage;

  clutter_stage_view_get_layout (view_from_src (src), &damage);
  damage.width = 1;
  damage.height = 1;

  clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage_from_src (src)),
                                        &damage);
}

static gboolean
is_cursor_in_stream (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaBackend *backend = backend_from_src (src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  ClutterStageView *stage_view = view_from_src (src);
  MtkRectangle view_layout;
  graphene_rect_t view_rect;
  MetaCursorSprite *cursor_sprite;

  clutter_stage_view_get_layout (stage_view, &view_layout);
  view_rect = mtk_rectangle_to_graphene_rect (&view_layout);

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (cursor_sprite)
    {
      graphene_rect_t cursor_rect;

      cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                         cursor_sprite);
      return graphene_rect_intersection (&cursor_rect, &view_rect, NULL);
    }
  else
    {
      MetaCursorTracker *cursor_tracker =
        meta_backend_get_cursor_tracker (backend);
      graphene_point_t cursor_position;

      meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
      return graphene_rect_contains_point (&view_rect,
                                           &cursor_position);
    }
}

static void
meta_screen_cast_virtual_stream_src_set_cursor_metadata (MetaScreenCastStreamSrc *src,
                                                         struct spa_meta_cursor  *spa_meta_cursor)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaBackend *backend = backend_from_src (src);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  MetaCursorSprite *cursor_sprite;
  ClutterStageView *stage_view;
  MtkRectangle view_layout;
  float view_scale;
  graphene_rect_t view_rect;
  graphene_point_t cursor_position;
  int x, y;

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);

  if (!meta_cursor_tracker_get_pointer_visible (cursor_tracker) ||
      !is_cursor_in_stream (virtual_src))
    {
      meta_screen_cast_stream_src_unset_cursor_metadata (src,
                                                         spa_meta_cursor);
      return;
    }

  stage_view = view_from_src (src);
  clutter_stage_view_get_layout (stage_view, &view_layout);
  view_rect = mtk_rectangle_to_graphene_rect (&view_layout);
  view_scale = clutter_stage_view_get_scale (stage_view);

  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
  cursor_position.x -= view_rect.origin.x;
  cursor_position.y -= view_rect.origin.y;
  cursor_position.x *= view_scale;
  cursor_position.y *= view_scale;

  x = (int) roundf (cursor_position.x);
  y = (int) roundf (cursor_position.y);

  if (virtual_src->cursor_bitmap_invalid)
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

      virtual_src->cursor_bitmap_invalid = FALSE;
    }
  else
    {
      meta_screen_cast_stream_src_set_cursor_position_metadata (src,
                                                                spa_meta_cursor,
                                                                x, y);
    }
}

static MetaVirtualMonitor *
create_virtual_monitor (MetaScreenCastVirtualStreamSrc  *virtual_src,
                        struct spa_video_info_raw       *video_format,
                        GError                         **error)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaBackend *backend = backend_from_src (src);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  static int virtual_monitor_src_seq = 0;
  int width, height;
  float refresh_rate;
  g_autofree char *serial = NULL;
  g_autoptr (MetaVirtualMonitorInfo) info = NULL;

  width = video_format->size.width;
  height = video_format->size.height;
  refresh_rate = ((float) video_format->max_framerate.num /
                  video_format->max_framerate.denom);
  serial = g_strdup_printf ("0x%.6x", ++virtual_monitor_src_seq);
  info = meta_virtual_monitor_info_new (width, height, refresh_rate,
                                        "MetaVendor",
                                        "Virtual remote monitor",
                                        serial);
  return meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                      info,
                                                      error);
}

static void
ensure_virtual_monitor (MetaScreenCastVirtualStreamSrc *virtual_src,
                        struct spa_video_info_raw      *video_format)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaBackend *backend = backend_from_src (src);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  g_autoptr (GError) error = NULL;
  MetaVirtualMonitor *virtual_monitor;

  virtual_monitor = virtual_src->virtual_monitor;
  if (virtual_monitor)
    {
      MetaCrtcMode *crtc_mode =
        meta_virtual_monitor_get_crtc_mode (virtual_monitor);
      const MetaCrtcModeInfo *mode_info = meta_crtc_mode_get_info (crtc_mode);
      float refresh_rate;

      if (mode_info->width == video_format->size.width &&
          mode_info->height == video_format->size.height)
        return;

      refresh_rate = ((float) video_format->max_framerate.num /
                      video_format->max_framerate.denom);
      meta_virtual_monitor_set_mode (virtual_monitor,
                                     video_format->size.width,
                                     video_format->size.height,
                                     refresh_rate);
      meta_monitor_manager_reload (monitor_manager);
      return;
    }

  virtual_monitor = create_virtual_monitor (virtual_src, video_format, &error);
  if (!virtual_monitor)
    {
      MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);

      g_warning ("Failed to create virtual monitor with size %dx%d: %s",
                 video_format->size.width, video_format->size.height,
                 error->message);
      meta_screen_cast_stream_src_close (src);
      return;
    }
  virtual_src->virtual_monitor = virtual_monitor;

  meta_monitor_manager_reload (monitor_manager);
}

static void
meta_screen_cast_virtual_stream_src_notify_params_updated (MetaScreenCastStreamSrc   *src,
                                                           struct spa_video_info_raw *video_format)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);

  ensure_virtual_monitor (virtual_src, video_format);
}

MetaScreenCastVirtualStreamSrc *
meta_screen_cast_virtual_stream_src_new (MetaScreenCastVirtualStream  *virtual_stream,
                                         GError                      **error)
{
  return g_initable_new (META_TYPE_SCREEN_CAST_VIRTUAL_STREAM_SRC, NULL, error,
                         "stream", virtual_stream,
                         NULL);
}

static gboolean
meta_screen_cast_virtual_stream_src_is_cursor_inhibited (MetaHwCursorInhibitor *inhibitor)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (inhibitor);

  return is_cursor_in_stream (virtual_src);
}

static void
hw_cursor_inhibitor_iface_init (MetaHwCursorInhibitorInterface *iface)
{
  iface->is_cursor_inhibited =
    meta_screen_cast_virtual_stream_src_is_cursor_inhibited;
}

static void
meta_screen_cast_virtual_stream_src_dispose (GObject *object)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (object);
  GObjectClass *parent_class =
    G_OBJECT_CLASS (meta_screen_cast_virtual_stream_src_parent_class);

  parent_class->dispose (object);

  g_clear_object (&virtual_src->virtual_monitor);
}

static void
meta_screen_cast_virtual_stream_src_init (MetaScreenCastVirtualStreamSrc *virtual_src)
{
}

static void
meta_screen_cast_virtual_stream_src_class_init (MetaScreenCastVirtualStreamSrcClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaScreenCastStreamSrcClass *src_class =
    META_SCREEN_CAST_STREAM_SRC_CLASS (klass);

  object_class->dispose = meta_screen_cast_virtual_stream_src_dispose;

  src_class->get_specs = meta_screen_cast_virtual_stream_src_get_specs;
  src_class->enable = meta_screen_cast_virtual_stream_src_enable;
  src_class->disable = meta_screen_cast_virtual_stream_src_disable;
  src_class->record_to_buffer =
    meta_screen_cast_virtual_stream_src_record_to_buffer;
  src_class->record_to_framebuffer =
    meta_screen_cast_virtual_stream_src_record_to_framebuffer;
  src_class->record_follow_up =
    meta_screen_cast_virtual_stream_record_follow_up;
  src_class->set_cursor_metadata =
    meta_screen_cast_virtual_stream_src_set_cursor_metadata;
  src_class->notify_params_updated =
    meta_screen_cast_virtual_stream_src_notify_params_updated;
}
