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

/* Till https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/4065 is fixed */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-conversion"

#include "config.h"

#include "backends/meta-screen-cast-virtual-stream-src.h"

#include <spa/param/video/format-utils.h>
#include <spa/buffer/meta.h>

#include "backends/meta-crtc-mode.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-eis-viewport.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-private.h"
#include "backends/meta-output.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-stage-private.h"
#include "backends/meta-virtual-monitor.h"
#include "core/boxes-private.h"

enum
{
  PROP_0,

  PROP_MODE_INFOS,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

#define META_TYPE_SCREEN_CAST_FRAME_CLOCK_DRIVER (meta_screen_cast_frame_clock_driver_get_type ())
G_DECLARE_FINAL_TYPE (MetaScreenCastFrameClockDriver,
                      meta_screen_cast_frame_clock_driver,
                      META, SCREEN_CAST_FRAME_CLOCK_DRIVER,
                      ClutterFrameClockDriver)

struct _MetaScreenCastVirtualStreamSrc
{
  MetaScreenCastStreamSrc parent;

  MetaVirtualMonitor *virtual_monitor;
  GList *mode_infos;

  gboolean cursor_bitmap_invalid;

  struct {
    gboolean set;
    int x;
    int y;
  } last_cursor_matadata;

  MetaStageWatch *paint_watch;
  MetaStageWatch *skipped_watch;
  GBinding *layout_binding;

  gulong position_invalidated_handler_id;
  gulong cursor_changed_handler_id;

  gulong monitors_changed_handler_id;

  MetaScreenCastFrameClockDriver *driver;
};

static void init_initable_iface (GInitableIface *iface);

static GInitableIface *initable_parent_iface;

G_DEFINE_FINAL_TYPE_WITH_CODE (MetaScreenCastVirtualStreamSrc,
                               meta_screen_cast_virtual_stream_src,
                               META_TYPE_SCREEN_CAST_STREAM_SRC,
                               G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                      init_initable_iface))

struct _MetaScreenCastFrameClockDriver
{
  ClutterFrameClockDriver parent;

  MetaScreenCastStreamSrc *src;
};

G_DEFINE_TYPE (MetaScreenCastFrameClockDriver, meta_screen_cast_frame_clock_driver,
               CLUTTER_TYPE_FRAME_CLOCK_DRIVER)

static gboolean
meta_screen_cast_virtual_stream_src_get_specs (MetaScreenCastStreamSrc *src,
                                               int                     *width,
                                               int                     *height,
                                               float                   *frame_rate)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaCrtcMode *crtc_mode;
  const MetaCrtcModeInfo *crtc_mode_info;

  if (!virtual_src->mode_infos)
    return FALSE;

  crtc_mode = meta_virtual_monitor_get_crtc_mode (virtual_src->virtual_monitor);
  crtc_mode_info = meta_crtc_mode_get_info (crtc_mode);

  *width = crtc_mode_info->width;
  *height = crtc_mode_info->height;
  *frame_rate = crtc_mode_info->refresh_rate;
  return TRUE;
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

  return view ? CLUTTER_STAGE_VIEW (view) : NULL;
}

static ClutterStage *
stage_from_src (MetaScreenCastStreamSrc *src)
{
  return CLUTTER_STAGE (meta_backend_get_stage (backend_from_src (src)));
}

ClutterStageView *
meta_screen_cast_virtual_stream_src_get_view (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  return view_from_src (META_SCREEN_CAST_STREAM_SRC (virtual_src));
}

MetaLogicalMonitor *
meta_screen_cast_virtual_stream_src_logical_monitor (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaVirtualMonitor *virtual_monitor;
  MetaOutput *output;
  MetaMonitor *monitor;

  virtual_monitor = virtual_src->virtual_monitor;
  if (!virtual_monitor)
    return NULL;

  output = meta_virtual_monitor_get_output (virtual_monitor);
  monitor = meta_output_get_monitor (output);
  return meta_monitor_get_logical_monitor (monitor);
}

static void
pointer_position_invalidated (MetaCursorTracker       *cursor_tracker,
                              MetaScreenCastStreamSrc *src)
{
  clutter_stage_view_schedule_update (view_from_src (src));
}

static void
cursor_changed (MetaCursorTracker              *cursor_tracker,
                MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);

  virtual_src->cursor_bitmap_invalid = TRUE;

  clutter_stage_view_schedule_update (view_from_src (src));
}

static void
on_after_paint (MetaStage        *stage,
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

  meta_screen_cast_stream_src_record_frame (src,
                                            flags,
                                            paint_phase,
                                            redraw_clip);
}

static void
on_skipped_paint (MetaStage        *stage,
                  ClutterStageView *view,
                  const MtkRegion  *redraw_clip,
                  ClutterFrame     *frame,
                  gpointer          user_data)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (user_data);
  MetaScreenCastRecordFlag flags;
  MetaScreenCastPaintPhase paint_phase;

  flags = META_SCREEN_CAST_RECORD_FLAG_CURSOR_ONLY;
  paint_phase = META_SCREEN_CAST_PAINT_PHASE_DETACHED;

  meta_screen_cast_stream_src_record_frame (src,
                                            flags,
                                            paint_phase,
                                            redraw_clip);
}

static void
update_frame_clock_driver (MetaScreenCastVirtualStreamSrc *virtual_src,
                           MetaScreenCastFrameClockDriver *driver)
{
  if (virtual_src->driver)
    virtual_src->driver->src = NULL;
  g_set_object (&virtual_src->driver, driver);
}

static void
make_frame_clock_passive (MetaScreenCastVirtualStreamSrc *virtual_src,
                          ClutterStageView               *view)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  ClutterFrameClock *frame_clock =
    clutter_stage_view_get_frame_clock (view);
  g_autoptr (MetaScreenCastFrameClockDriver) driver = NULL;

  driver = g_object_new (META_TYPE_SCREEN_CAST_FRAME_CLOCK_DRIVER, NULL);
  driver->src = src;

  update_frame_clock_driver (virtual_src, driver);

  clutter_frame_clock_set_passive (frame_clock,
                                   CLUTTER_FRAME_CLOCK_DRIVER (driver));
}

static void
setup_view (MetaScreenCastVirtualStreamSrc *virtual_src,
            ClutterStageView               *view)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaStage *meta_stage = META_STAGE (stage_from_src (src));

  g_return_if_fail (!virtual_src->paint_watch &&
                    !virtual_src->skipped_watch);

  switch (meta_screen_cast_stream_get_cursor_mode (stream))
    {
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      meta_stage_view_inhibit_cursor_overlay (META_STAGE_VIEW (view));
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
      break;
    }

  virtual_src->paint_watch =
    meta_stage_watch_view (meta_stage,
                           view,
                           META_STAGE_WATCH_AFTER_PAINT,
                           on_after_paint,
                           virtual_src);
  virtual_src->skipped_watch =
    meta_stage_watch_view (meta_stage,
                           view,
                           META_STAGE_WATCH_SKIPPED_PAINT,
                           on_skipped_paint,
                           virtual_src);

  g_set_object (&virtual_src->layout_binding,
                g_object_bind_property (view, "layout",
                                        src, "layout",
                                        G_BINDING_SYNC_CREATE));

  if (meta_screen_cast_stream_src_is_enabled (src) &&
      !meta_screen_cast_stream_src_is_driving (src))
    make_frame_clock_passive (virtual_src, view);
}

static void
on_monitors_changed (MetaMonitorManager             *monitor_manager,
                     MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaStage *stage = META_STAGE (stage_from_src (src));
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  ClutterStageView *view;

  if (meta_screen_cast_stream_src_is_enabled (src))
    {
      meta_stage_remove_watch (stage, virtual_src->paint_watch);
      virtual_src->paint_watch = NULL;
      meta_stage_remove_watch (stage, virtual_src->skipped_watch);
      virtual_src->skipped_watch = NULL;

      view = view_from_src (src);
      setup_view (virtual_src, view);

      meta_eis_viewport_notify_changed (META_EIS_VIEWPORT (stream));
    }

  meta_screen_cast_stream_src_renegotiate (src);
}

static void
setup_cursor_handling (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  MetaBackend *backend = backend_from_src (src);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);

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
      break;
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
      break;
    }
}

static void
meta_screen_cast_virtual_stream_src_enable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaScreenCastStream *stream = meta_screen_cast_stream_src_get_stream (src);
  ClutterStageView *view;

  view = view_from_src (src);
  if (view)
    setup_view (virtual_src, view);

  setup_cursor_handling (virtual_src);

  if (!meta_screen_cast_stream_is_configured (stream))
    meta_screen_cast_stream_notify_is_configured (stream);
  else
    meta_eis_viewport_notify_changed (META_EIS_VIEWPORT (stream));

  clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage_from_src (src)),
                                        NULL);
}

static void
meta_screen_cast_virtual_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaBackend *backend = backend_from_src (src);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage = stage_from_src (src);

  if (virtual_src->paint_watch)
    {
      meta_stage_remove_watch (META_STAGE (stage), virtual_src->paint_watch);
      virtual_src->paint_watch = NULL;
    }

  if (virtual_src->skipped_watch)
    {
      meta_stage_remove_watch (META_STAGE (stage), virtual_src->skipped_watch);
      virtual_src->skipped_watch = NULL;
    }

  g_clear_object (&virtual_src->layout_binding);

  g_clear_signal_handler (&virtual_src->position_invalidated_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&virtual_src->cursor_changed_handler_id,
                          cursor_tracker);
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
  if (!cogl_framebuffer_blit (view_framebuffer,
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
  ClutterCursor *cursor;

  clutter_stage_view_get_layout (stage_view, &view_layout);
  view_rect = mtk_rectangle_to_graphene_rect (&view_layout);

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);
  if (cursor)
    {
      graphene_rect_t cursor_rect;

      cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                         cursor);
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

static gboolean
should_cursor_metadata_be_set (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaBackend *backend = backend_from_src (src);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);

  return (meta_cursor_tracker_get_pointer_visible (cursor_tracker) &&
          is_cursor_in_stream (virtual_src));
}

static void
get_cursor_position (MetaScreenCastVirtualStreamSrc *virtual_src,
                     int                            *out_x,
                     int                            *out_y)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaBackend *backend = backend_from_src (src);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  ClutterStageView *stage_view;
  MtkRectangle view_layout;
  graphene_rect_t view_rect;
  float view_scale;
  graphene_point_t cursor_position;

  stage_view = view_from_src (src);
  view_scale = clutter_stage_view_get_scale (stage_view);
  clutter_stage_view_get_layout (stage_view, &view_layout);
  view_rect = mtk_rectangle_to_graphene_rect (&view_layout);

  meta_cursor_tracker_get_pointer (cursor_tracker, &cursor_position, NULL);
  cursor_position.x -= view_rect.origin.x;
  cursor_position.y -= view_rect.origin.y;
  cursor_position.x *= view_scale;
  cursor_position.y *= view_scale;

  *out_x = (int) roundf (cursor_position.x);
  *out_y = (int) roundf (cursor_position.y);
}

static gboolean
meta_screen_cast_virtual_stream_src_is_cursor_metadata_valid (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);

  if (should_cursor_metadata_be_set (virtual_src))
    {
      int x, y;

      if (!virtual_src->last_cursor_matadata.set)
        return FALSE;

      if (virtual_src->cursor_bitmap_invalid)
        return FALSE;

      get_cursor_position (virtual_src, &x, &y);

      return (virtual_src->last_cursor_matadata.x == x &&
              virtual_src->last_cursor_matadata.y == y);
    }
  else
    {
      return !virtual_src->last_cursor_matadata.set;
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
  ClutterCursor *cursor;
  int x, y;

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);

  if (!should_cursor_metadata_be_set (virtual_src))
    {
      virtual_src->last_cursor_matadata.set = FALSE;
      meta_screen_cast_stream_src_unset_cursor_metadata (src,
                                                         spa_meta_cursor);
      return;
    }

  get_cursor_position (virtual_src, &x, &y);

  virtual_src->last_cursor_matadata.set = TRUE;
  virtual_src->last_cursor_matadata.x = x;
  virtual_src->last_cursor_matadata.y = y;

  if (virtual_src->cursor_bitmap_invalid)
    {

      if (cursor)
        {
          ClutterStageView *stage_view;
          float view_scale;

          stage_view = view_from_src (src);
          view_scale = clutter_stage_view_get_scale (stage_view);

          meta_screen_cast_stream_src_set_cursor_sprite_metadata (src,
                                                                  spa_meta_cursor,
                                                                  cursor,
                                                                  x, y,
                                                                  view_scale);
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

static MetaVirtualModeInfo *
create_mode_info (struct spa_video_info_raw *video_format)
{
  int width, height;
  float refresh_rate;

  width = (int) video_format->size.width;
  height = (int) video_format->size.height;
  refresh_rate = ((float) video_format->max_framerate.num /
                  video_format->max_framerate.denom);

  return meta_virtual_mode_info_new (width, height, refresh_rate);
}

static char *
generate_next_virtual_monitor_serial (void)
{
  static int virtual_monitor_src_seq = 0;

  return g_strdup_printf ("0x%.6x", ++virtual_monitor_src_seq);
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
  g_autofree char *serial = NULL;
  g_autolist (MetaVirtualModeInfo) mode_infos = NULL;
  g_autoptr (MetaVirtualMonitorInfo) info = NULL;

  serial = generate_next_virtual_monitor_serial ();

  mode_infos = g_list_append (mode_infos, create_mode_info (video_format));

  info = meta_virtual_monitor_info_new ("MetaVendor",
                                        "Virtual remote monitor",
                                        serial,
                                        mode_infos);
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
      g_autolist (MetaVirtualModeInfo) mode_infos = NULL;
      const MetaCrtcModeInfo *mode_info = meta_crtc_mode_get_info (crtc_mode);

      if (mode_info->width == video_format->size.width &&
          mode_info->height == video_format->size.height)
        return;

      mode_infos = g_list_append (mode_infos, create_mode_info (video_format));
      meta_virtual_monitor_set_modes (virtual_monitor, mode_infos);
      meta_monitor_manager_reload (monitor_manager);
      return;
    }

  virtual_monitor = create_virtual_monitor (virtual_src, video_format, &error);
  if (!virtual_monitor)
    {
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

  if (virtual_src->mode_infos)
    return;

  ensure_virtual_monitor (virtual_src, video_format);
}

static void
meta_screen_cast_virtual_stream_src_dispatch (MetaScreenCastStreamSrc *src)
{
  ClutterStageView *view = view_from_src (src);
  ClutterFrameClock *frame_clock = clutter_stage_view_get_frame_clock (view);
  ClutterFrameResult result;

  result = clutter_frame_clock_dispatch (frame_clock, g_get_monotonic_time ());

  switch (result)
    {
    case CLUTTER_FRAME_RESULT_PENDING_PRESENTED:
    case CLUTTER_FRAME_RESULT_IDLE:
      break;
    case CLUTTER_FRAME_RESULT_IGNORED:
      meta_screen_cast_stream_src_queue_empty_buffer (src);
      break;
    }
}

static void
meta_screen_cast_virtual_stream_src_append_tags (MetaScreenCastStreamSrc *src,
                                                 GArray                  *tags)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaLogicalMonitor *logical_monitor;
  MetaTagEntry tag_entry;

  logical_monitor =
    meta_screen_cast_virtual_stream_src_logical_monitor (virtual_src);
  if (!logical_monitor)
    return;

  tag_entry.key = g_strdup ("org.gnome.scale");
  tag_entry.value = g_new0 (char, G_ASCII_DTOSTR_BUF_SIZE);
  g_ascii_dtostr (tag_entry.value, G_ASCII_DTOSTR_BUF_SIZE,
                  meta_logical_monitor_get_scale (logical_monitor));

  g_array_append_val (tags, tag_entry);
}

MetaScreenCastVirtualStreamSrc *
meta_screen_cast_virtual_stream_src_new (MetaScreenCastVirtualStream  *virtual_stream,
                                         GList                        *mode_infos,
                                         GError                      **error)
{
  return g_initable_new (META_TYPE_SCREEN_CAST_VIRTUAL_STREAM_SRC, NULL, error,
                         "stream", virtual_stream,
                         "must-drive", FALSE,
                         "mode-infos", mode_infos,
                         NULL);
}

static gboolean
meta_screen_cast_virtual_stream_src_initable_init (GInitable     *initable,
                                                   GCancellable  *cancellable,
                                                   GError       **error)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (initable);
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaBackend *backend = backend_from_src (src);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (virtual_src->mode_infos)
    {
      g_autofree char *serial = NULL;
      g_autoptr (MetaVirtualMonitorInfo) info = NULL;
      MetaVirtualMonitor *virtual_monitor;

      serial = generate_next_virtual_monitor_serial ();
      info = meta_virtual_monitor_info_new ("MetaVendor",
                                            "Virtual remote monitor",
                                            serial,
                                            virtual_src->mode_infos);
      virtual_monitor =
        meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                     info,
                                                     error);
      if (!virtual_monitor)
        return FALSE;

      virtual_src->virtual_monitor = virtual_monitor;
      meta_monitor_manager_reload (monitor_manager);
    }

  virtual_src->monitors_changed_handler_id =
    g_signal_connect (monitor_manager, "monitors-changed-internal",
                      G_CALLBACK (on_monitors_changed),
                      virtual_src);

  return initable_parent_iface->init (initable, cancellable, error);
}

static void
init_initable_iface (GInitableIface *iface)
{
  initable_parent_iface = g_type_interface_peek_parent (iface);

  iface->init = meta_screen_cast_virtual_stream_src_initable_init;
}

static void
meta_screen_cast_virtual_stream_src_set_property (GObject      *object,
                                                  guint         prop_id,
                                                  const GValue *value,
                                                  GParamSpec   *pspec)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (object);

  switch (prop_id)
    {
    case PROP_MODE_INFOS:
      virtual_src->mode_infos =
        g_list_copy_deep (g_value_get_pointer (value),
                          (GCopyFunc) meta_virtual_mode_info_dup,
                          NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_virtual_stream_src_get_property (GObject    *object,
                                                  guint       prop_id,
                                                  GValue     *value,
                                                  GParamSpec *pspec)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (object);

  switch (prop_id)
    {
    case PROP_MODE_INFOS:
      g_value_set_pointer (value, virtual_src->mode_infos);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_screen_cast_virtual_stream_src_dispose (GObject *object)
{
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (object);
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (virtual_src);
  MetaBackend *backend = backend_from_src (src);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GObjectClass *parent_class =
    G_OBJECT_CLASS (meta_screen_cast_virtual_stream_src_parent_class);

  update_frame_clock_driver (virtual_src, NULL);

  g_clear_signal_handler (&virtual_src->monitors_changed_handler_id,
                          monitor_manager);

  parent_class->dispose (object);

  g_clear_object (&virtual_src->virtual_monitor);
  g_clear_list (&virtual_src->mode_infos,
                (GDestroyNotify) meta_virtual_mode_info_free);
}

static void
meta_screen_cast_virtual_stream_src_init (MetaScreenCastVirtualStreamSrc *virtual_src)
{
  virtual_src->cursor_bitmap_invalid = TRUE;
}

static void
meta_screen_cast_virtual_stream_src_class_init (MetaScreenCastVirtualStreamSrcClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaScreenCastStreamSrcClass *src_class =
    META_SCREEN_CAST_STREAM_SRC_CLASS (klass);

  object_class->set_property = meta_screen_cast_virtual_stream_src_set_property;
  object_class->get_property = meta_screen_cast_virtual_stream_src_get_property;
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
  src_class->is_cursor_metadata_valid =
    meta_screen_cast_virtual_stream_src_is_cursor_metadata_valid;
  src_class->set_cursor_metadata =
    meta_screen_cast_virtual_stream_src_set_cursor_metadata;
  src_class->notify_params_updated =
    meta_screen_cast_virtual_stream_src_notify_params_updated;
  src_class->dispatch =
    meta_screen_cast_virtual_stream_src_dispatch;
  src_class->append_tags =
    meta_screen_cast_virtual_stream_src_append_tags;

  obj_props[PROP_MODE_INFOS] =
    g_param_spec_pointer ("mode-infos", NULL, NULL,
                          G_PARAM_WRITABLE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class,
                                     N_PROPS,
                                     obj_props);
}

static void
meta_screen_cast_frame_clock_driver_schedule_update (ClutterFrameClockDriver *frame_clock_driver)
{
  MetaScreenCastFrameClockDriver *driver =
    META_SCREEN_CAST_FRAME_CLOCK_DRIVER (frame_clock_driver);

  if (driver->src)
    meta_screen_cast_stream_src_request_process (driver->src);
}

static void
meta_screen_cast_frame_clock_driver_class_init (MetaScreenCastFrameClockDriverClass *klass)
{
  ClutterFrameClockDriverClass *driver_class =
    CLUTTER_FRAME_CLOCK_DRIVER_CLASS (klass);

  driver_class->schedule_update =
    meta_screen_cast_frame_clock_driver_schedule_update;
}

static void
meta_screen_cast_frame_clock_driver_init (MetaScreenCastFrameClockDriver *driver)
{
}

#pragma GCC diagnostic pop
