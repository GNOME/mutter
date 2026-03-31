/*
 * Copyright (C) 2021-2026 Red Hat Inc.
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

#include "backends/meta-stream-source-virtual.h"

#include <spa/param/video/format-utils.h>
#include <spa/buffer/meta.h>

#include "backends/meta-crtc-mode.h"
#include "backends/meta-cursor-tracker-private.h"
#include "backends/meta-eis-viewport.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-private.h"
#include "backends/meta-output.h"
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

#define META_TYPE_STREAM_FRAME_CLOCK_DRIVER (meta_stream_frame_clock_driver_get_type ())
G_DECLARE_FINAL_TYPE (MetaStreamFrameClockDriver,
                      meta_stream_frame_clock_driver,
                      META, STREAM_FRAME_CLOCK_DRIVER,
                      ClutterFrameClockDriver)

struct _MetaStreamSourceVirtual
{
  MetaStreamSource parent;

  MetaVirtualMonitor *virtual_monitor;
  GList *mode_infos;
  gboolean has_preferred_scale;
  float preferred_scale;

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

  MetaStreamFrameClockDriver *driver;
};

static void init_initable_iface (GInitableIface *iface);

static GInitableIface *initable_parent_iface;

G_DEFINE_FINAL_TYPE_WITH_CODE (MetaStreamSourceVirtual,
                               meta_stream_source_virtual,
                               META_TYPE_STREAM_SOURCE,
                               G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                      init_initable_iface))

struct _MetaStreamFrameClockDriver
{
  ClutterFrameClockDriver parent;

  MetaStreamSource *source;
};

G_DEFINE_TYPE (MetaStreamFrameClockDriver, meta_stream_frame_clock_driver,
               CLUTTER_TYPE_FRAME_CLOCK_DRIVER)

static gboolean
meta_stream_source_virtual_get_specs (MetaStreamSource *source,
                                      int              *width,
                                      int              *height,
                                      float            *frame_rate)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  MetaCrtcMode *crtc_mode;
  const MetaCrtcModeInfo *crtc_mode_info;

  if (!source_virtual->mode_infos)
    return FALSE;

  crtc_mode = meta_virtual_monitor_get_crtc_mode (source_virtual->virtual_monitor);
  crtc_mode_info = meta_crtc_mode_get_info (crtc_mode);

  *width = crtc_mode_info->width;
  *height = crtc_mode_info->height;
  *frame_rate = crtc_mode_info->refresh_rate;
  return TRUE;
}

static MetaBackend *
backend_from_source (MetaStreamSource *source)
{
  MetaStream *stream = meta_stream_source_get_stream (source);

  return meta_stream_get_backend (stream);
}

static ClutterStageView *
view_from_source (MetaStreamSource *source)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  MetaVirtualMonitor *virtual_monitor = source_virtual->virtual_monitor;
  MetaCrtc *crtc = meta_virtual_monitor_get_crtc (virtual_monitor);
  MetaRenderer *renderer = meta_backend_get_renderer (backend_from_source (source));
  MetaRendererView *view = meta_renderer_get_view_for_crtc (renderer, crtc);

  return view ? CLUTTER_STAGE_VIEW (view) : NULL;
}

static ClutterStage *
stage_from_source (MetaStreamSource *source)
{
  return CLUTTER_STAGE (meta_backend_get_stage (backend_from_source (source)));
}

static void
pointer_position_invalidated (MetaCursorTracker *cursor_tracker,
                              MetaStreamSource  *source)
{
  clutter_stage_view_schedule_update (view_from_source (source));
}

static void
cursor_changed (MetaCursorTracker       *cursor_tracker,
                MetaStreamSourceVirtual *source_virtual)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);

  source_virtual->cursor_bitmap_invalid = TRUE;

  clutter_stage_view_schedule_update (view_from_source (source));
}

static void
on_after_paint (MetaStage        *stage,
                ClutterStageView *view,
                const MtkRegion  *redraw_clip,
                ClutterFrame     *frame,
                gpointer          user_data)
{
  MetaStreamSource *source = META_STREAM_SOURCE (user_data);
  MetaStreamPaintPhase paint_phase;
  MetaStreamRecordFlag flags;

  flags = META_STREAM_RECORD_FLAG_NONE;
  paint_phase = META_STREAM_PAINT_PHASE_PRE_SWAP_BUFFER;

  meta_stream_source_record_frame (source,
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
  MetaStreamSource *source = META_STREAM_SOURCE (user_data);
  MetaStreamRecordFlag flags;
  MetaStreamPaintPhase paint_phase;

  flags = META_STREAM_RECORD_FLAG_CURSOR_ONLY;
  paint_phase = META_STREAM_PAINT_PHASE_DETACHED;

  meta_stream_source_record_frame (source,
                                   flags,
                                   paint_phase,
                                   redraw_clip);
}

static void
update_frame_clock_driver (MetaStreamSourceVirtual *source_virtual,
                           MetaStreamFrameClockDriver *driver)
{
  if (source_virtual->driver)
    source_virtual->driver->source = NULL;
  g_set_object (&source_virtual->driver, driver);
}

static void
make_frame_clock_passive (MetaStreamSourceVirtual *source_virtual,
                          ClutterStageView        *view)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  ClutterFrameClock *frame_clock =
    clutter_stage_view_get_frame_clock (view);
  g_autoptr (MetaStreamFrameClockDriver) driver = NULL;

  driver = g_object_new (META_TYPE_STREAM_FRAME_CLOCK_DRIVER, NULL);
  driver->source = source;

  update_frame_clock_driver (source_virtual, driver);

  clutter_frame_clock_set_passive (frame_clock,
                                   CLUTTER_FRAME_CLOCK_DRIVER (driver));
}

static void
setup_view (MetaStreamSourceVirtual *source_virtual,
            ClutterStageView        *view)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaStage *meta_stage = META_STAGE (stage_from_source (source));

  g_return_if_fail (!source_virtual->paint_watch &&
                    !source_virtual->skipped_watch);

  switch (meta_stream_get_cursor_mode (stream))
    {
    case META_STREAM_CURSOR_MODE_METADATA:
    case META_STREAM_CURSOR_MODE_HIDDEN:
      meta_stage_view_inhibit_cursor_overlay (META_STAGE_VIEW (view));
      break;
    case META_STREAM_CURSOR_MODE_EMBEDDED:
      break;
    }

  source_virtual->paint_watch =
    meta_stage_watch_view (meta_stage,
                           view,
                           META_STAGE_WATCH_AFTER_PAINT,
                           on_after_paint,
                           source_virtual);
  source_virtual->skipped_watch =
    meta_stage_watch_view (meta_stage,
                           view,
                           META_STAGE_WATCH_SKIPPED_PAINT,
                           on_skipped_paint,
                           source_virtual);

  g_set_object (&source_virtual->layout_binding,
                g_object_bind_property (view, "layout",
                                        source, "layout",
                                        G_BINDING_SYNC_CREATE));

  if (meta_stream_source_is_enabled (source) &&
      !meta_stream_source_is_driving (source))
    make_frame_clock_passive (source_virtual, view);
}

static void
on_monitors_changed (MetaMonitorManager      *monitor_manager,
                     MetaStreamSourceVirtual *source_virtual)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaStage *stage = META_STAGE (stage_from_source (source));
  MetaStream *stream = meta_stream_source_get_stream (source);
  ClutterStageView *view;

  if (meta_stream_source_is_enabled (source))
    {
      meta_stage_remove_watch (stage, source_virtual->paint_watch);
      source_virtual->paint_watch = NULL;
      meta_stage_remove_watch (stage, source_virtual->skipped_watch);
      source_virtual->skipped_watch = NULL;

      view = view_from_source (source);
      setup_view (source_virtual, view);

      meta_eis_viewport_notify_changed (META_EIS_VIEWPORT (stream));
    }

  meta_stream_source_renegotiate (source);
}

static void
setup_cursor_handling (MetaStreamSourceVirtual *source_virtual)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaStream *stream = meta_stream_source_get_stream (source);
  MetaBackend *backend = backend_from_source (source);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);

  switch (meta_stream_get_cursor_mode (stream))
    {
    case META_STREAM_CURSOR_MODE_METADATA:
      source_virtual->position_invalidated_handler_id =
        g_signal_connect_after (cursor_tracker, "position-invalidated",
                                G_CALLBACK (pointer_position_invalidated),
                                source_virtual);
      source_virtual->cursor_changed_handler_id =
        g_signal_connect_after (cursor_tracker, "cursor-changed",
                                G_CALLBACK (cursor_changed),
                                source_virtual);
      break;
    case META_STREAM_CURSOR_MODE_EMBEDDED:
    case META_STREAM_CURSOR_MODE_HIDDEN:
      break;
    }
}

static void
meta_stream_source_virtual_enable (MetaStreamSource *source)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  MetaStream *stream = meta_stream_source_get_stream (source);
  ClutterStageView *view;

  view = view_from_source (source);
  if (view)
    setup_view (source_virtual, view);

  setup_cursor_handling (source_virtual);

  if (!meta_stream_is_configured (stream))
    meta_stream_notify_is_configured (stream);
  else
    meta_eis_viewport_notify_changed (META_EIS_VIEWPORT (stream));

  clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage_from_source (source)),
                                        NULL);
}

static void
meta_stream_source_virtual_disable (MetaStreamSource *source)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  MetaBackend *backend = backend_from_source (source);
  MetaCursorTracker *cursor_tracker = meta_backend_get_cursor_tracker (backend);
  ClutterStage *stage = stage_from_source (source);

  if (source_virtual->paint_watch)
    {
      meta_stage_remove_watch (META_STAGE (stage), source_virtual->paint_watch);
      source_virtual->paint_watch = NULL;
    }

  if (source_virtual->skipped_watch)
    {
      meta_stage_remove_watch (META_STAGE (stage), source_virtual->skipped_watch);
      source_virtual->skipped_watch = NULL;
    }

  g_clear_object (&source_virtual->layout_binding);

  g_clear_signal_handler (&source_virtual->position_invalidated_handler_id,
                          cursor_tracker);
  g_clear_signal_handler (&source_virtual->cursor_changed_handler_id,
                          cursor_tracker);
}

static gboolean
meta_stream_source_virtual_record_to_buffer (MetaStreamSource      *source,
                                             MetaStreamRecordFlag   flags,
                                             MetaStreamPaintPhase   paint_phase,
                                             int                    width,
                                             int                    height,
                                             int                    stride,
                                             uint8_t               *data,
                                             MtkRegion             *damage,
                                             GError               **error)
{
  ClutterStageView *view;
  CoglFramebuffer *framebuffer;
  MtkRectangle view_rect;
  float scale;

  view = view_from_source (source);
  framebuffer = clutter_stage_view_get_onscreen (view);
  scale = clutter_stage_view_get_scale (view);
  clutter_stage_view_get_layout (view, &view_rect);

  return meta_stream_source_paint_to_buffer (source,
                                             NULL,
                                             framebuffer,
                                             &view_rect, scale,
                                             width, height, stride, data,
                                             COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT,
                                             damage,
                                             error);
}

static gboolean
meta_stream_source_virtual_record_to_framebuffer (MetaStreamSource      *source,
                                                  MetaStreamPaintPhase   paint_phase,
                                                  CoglFramebuffer       *framebuffer,
                                                  MtkRegion             *damage,
                                                  GError               **error)
{
  ClutterStageView *view;
  CoglFramebuffer *view_framebuffer;

  view = view_from_source (source);
  view_framebuffer = clutter_stage_view_get_framebuffer (view);
  if (damage ?
      !cogl_framebuffer_blit_region (view_framebuffer,
                                     framebuffer,
                                     damage,
                                     0, 0,
                                     error) :
      !cogl_framebuffer_blit (view_framebuffer,
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
meta_stream_source_virtual_queue_follow_up (MetaStreamSource     *source,
                        MetaStreamRecordFlag  flags)
{
  if (flags & META_STREAM_RECORD_FLAG_CURSOR_ONLY)
    {
      clutter_stage_view_schedule_update (view_from_source (source));
    }
  else
    {
      MtkRectangle damage;

      clutter_stage_view_get_layout (view_from_source (source), &damage);
      damage.width = 1;
      damage.height = 1;

      clutter_actor_queue_redraw_with_clip (CLUTTER_ACTOR (stage_from_source (source)),
                                            &damage);
    }
}

static gboolean
is_cursor_in_stream (MetaStreamSourceVirtual *source_virtual)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaBackend *backend = backend_from_source (source);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  ClutterStageView *stage_view = view_from_source (source);
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
should_cursor_metadata_be_set (MetaStreamSourceVirtual *source_virtual)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaBackend *backend = backend_from_source (source);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);

  return (meta_cursor_tracker_get_pointer_visible (cursor_tracker) &&
          is_cursor_in_stream (source_virtual));
}

static void
get_cursor_position (MetaStreamSourceVirtual *source_virtual,
                     int                            *out_x,
                     int                            *out_y)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaBackend *backend = backend_from_source (source);
  MetaCursorTracker *cursor_tracker =
    meta_backend_get_cursor_tracker (backend);
  ClutterStageView *stage_view;
  MtkRectangle view_layout;
  graphene_rect_t view_rect;
  float view_scale;
  graphene_point_t cursor_position;

  stage_view = view_from_source (source);
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
meta_stream_source_virtual_is_cursor_metadata_valid (MetaStreamSource *source)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);

  if (should_cursor_metadata_be_set (source_virtual))
    {
      int x, y;

      if (!source_virtual->last_cursor_matadata.set)
        return FALSE;

      if (source_virtual->cursor_bitmap_invalid)
        return FALSE;

      get_cursor_position (source_virtual, &x, &y);

      return (source_virtual->last_cursor_matadata.x == x &&
              source_virtual->last_cursor_matadata.y == y);
    }
  else
    {
      return !source_virtual->last_cursor_matadata.set;
    }
}

static void
meta_stream_source_virtual_set_cursor_metadata (MetaStreamSource       *source,
                                                struct spa_meta_cursor *spa_meta_cursor)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  MetaBackend *backend = backend_from_source (source);
  MetaCursorRenderer *cursor_renderer =
    meta_backend_get_cursor_renderer (backend);
  ClutterCursor *cursor;
  int x, y;

  cursor = meta_cursor_renderer_get_cursor (cursor_renderer);

  if (!should_cursor_metadata_be_set (source_virtual))
    {
      source_virtual->last_cursor_matadata.set = FALSE;
      meta_stream_source_unset_cursor_metadata (source, spa_meta_cursor);
      return;
    }

  get_cursor_position (source_virtual, &x, &y);

  source_virtual->last_cursor_matadata.set = TRUE;
  source_virtual->last_cursor_matadata.x = x;
  source_virtual->last_cursor_matadata.y = y;

  if (source_virtual->cursor_bitmap_invalid)
    {

      if (cursor)
        {
          ClutterStageView *stage_view;
          float view_scale;

          stage_view = view_from_source (source);
          view_scale = clutter_stage_view_get_scale (stage_view);

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

      source_virtual->cursor_bitmap_invalid = FALSE;
    }
  else
    {
      meta_stream_source_set_cursor_position_metadata (source,
                                                       spa_meta_cursor,
                                                       x, y);
    }
}

static MetaVirtualModeInfo *
create_mode_info (MetaStreamSourceVirtual   *source_virtual,
                  struct spa_video_info_raw *video_format)
{
  int width, height;
  float refresh_rate;
  MetaVirtualModeInfo *mode_info;

  width = (int) video_format->size.width;
  height = (int) video_format->size.height;
  refresh_rate = ((float) video_format->max_framerate.num /
                  video_format->max_framerate.denom);

  mode_info = meta_virtual_mode_info_new (width, height, refresh_rate);
  if (source_virtual->has_preferred_scale)
    {
      meta_virtual_mode_info_set_preferred_scale (mode_info,
                                                  source_virtual->preferred_scale);
    }

  return mode_info;
}

static char *
generate_next_virtual_monitor_serial (void)
{
  static int virtual_monitor_source_seq = 0;

  return g_strdup_printf ("0x%.6x", ++virtual_monitor_source_seq);
}

static MetaVirtualMonitor *
create_virtual_monitor (MetaStreamSourceVirtual    *source_virtual,
                        struct spa_video_info_raw  *video_format,
                        GError                    **error)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaBackend *backend = backend_from_source (source);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  g_autofree char *serial = NULL;
  g_autolist (MetaVirtualModeInfo) mode_infos = NULL;
  g_autoptr (MetaVirtualMonitorInfo) info = NULL;

  serial = generate_next_virtual_monitor_serial ();

  mode_infos = g_list_append (mode_infos, create_mode_info (source_virtual,
                                                            video_format));

  info = meta_virtual_monitor_info_new ("MetaVendor",
                                        "Virtual remote monitor",
                                        serial,
                                        mode_infos);
  return meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                      info,
                                                      error);
}

static void
ensure_virtual_monitor (MetaStreamSourceVirtual   *source_virtual,
                        struct spa_video_info_raw *video_format)
{
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaBackend *backend = backend_from_source (source);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  g_autoptr (GError) error = NULL;
  MetaVirtualMonitor *virtual_monitor;

  virtual_monitor = source_virtual->virtual_monitor;
  if (virtual_monitor)
    {
      MetaCrtcMode *crtc_mode =
        meta_virtual_monitor_get_crtc_mode (virtual_monitor);
      g_autolist (MetaVirtualModeInfo) mode_infos = NULL;
      const MetaCrtcModeInfo *mode_info = meta_crtc_mode_get_info (crtc_mode);

      if (mode_info->width == video_format->size.width &&
          mode_info->height == video_format->size.height &&
          mode_info->preferred_scale == source_virtual->preferred_scale)
        return;

      mode_infos = g_list_append (mode_infos, create_mode_info (source_virtual,
                                                                video_format));
      meta_virtual_monitor_set_modes (virtual_monitor, mode_infos);
      meta_monitor_manager_reload (monitor_manager);
      return;
    }

  virtual_monitor = create_virtual_monitor (source_virtual, video_format, &error);
  if (!virtual_monitor)
    {
      g_warning ("Failed to create virtual monitor with size %dx%d: %s",
                 video_format->size.width, video_format->size.height,
                 error->message);
      meta_stream_source_close (source);
      return;
    }
  source_virtual->virtual_monitor = virtual_monitor;

  meta_monitor_manager_reload (monitor_manager);
}

static void
meta_stream_source_virtual_notify_params_updated (MetaStreamSource          *source,
                                                  struct spa_video_info_raw *video_format)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);

  if (source_virtual->mode_infos)
    return;

  ensure_virtual_monitor (source_virtual, video_format);
}

static void
meta_stream_source_virtual_dispatch (MetaStreamSource *source)
{
  ClutterStageView *view = view_from_source (source);
  ClutterFrameClock *frame_clock = clutter_stage_view_get_frame_clock (view);
  ClutterFrameResult result;

  result = clutter_frame_clock_dispatch (frame_clock, g_get_monotonic_time ());

  switch (result)
    {
    case CLUTTER_FRAME_RESULT_PENDING_PRESENTED:
    case CLUTTER_FRAME_RESULT_IDLE:
      break;
    case CLUTTER_FRAME_RESULT_IGNORED:
      meta_stream_source_queue_empty_buffer (source);
      break;
    }
}

static void
meta_stream_source_virtual_append_tags (MetaStreamSource *source,
                                        GArray           *tags)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  MetaLogicalMonitor *logical_monitor;
  MetaSpaDictEntry dict_entry;

  logical_monitor =
    meta_stream_source_virtual_logical_monitor (source_virtual);
  if (!logical_monitor)
    return;

  dict_entry.key = g_strdup ("org.gnome.scale");
  dict_entry.value = g_new0 (char, G_ASCII_DTOSTR_BUF_SIZE);
  g_ascii_dtostr (dict_entry.value, G_ASCII_DTOSTR_BUF_SIZE,
                  meta_logical_monitor_get_scale (logical_monitor));

  g_array_append_val (tags, dict_entry);
}

static void
meta_stream_source_virtual_tag_changed (MetaStreamSource *source,
                                        const char       *key,
                                        const char       *value)
{
  if (g_strcmp0 (key, "org.gnome.preferred-scale") == 0)
    {
      MetaStreamSourceVirtual *source_virtual =
        META_STREAM_SOURCE_VIRTUAL (source);
      double scale = g_ascii_strtod (value, NULL);

      source_virtual->has_preferred_scale = TRUE;
      source_virtual->preferred_scale = (float) scale;
    }
}

static gboolean
meta_stream_source_virtual_initable_init (GInitable     *initable,
                                          GCancellable  *cancellable,
                                          GError       **error)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (initable);
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaBackend *backend = backend_from_source (source);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (source_virtual->mode_infos)
    {
      g_autofree char *serial = NULL;
      g_autoptr (MetaVirtualMonitorInfo) info = NULL;
      MetaVirtualMonitor *virtual_monitor;

      serial = generate_next_virtual_monitor_serial ();
      info = meta_virtual_monitor_info_new ("MetaVendor",
                                            "Virtual remote monitor",
                                            serial,
                                            source_virtual->mode_infos);
      virtual_monitor =
        meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                     info,
                                                     error);
      if (!virtual_monitor)
        return FALSE;

      source_virtual->virtual_monitor = virtual_monitor;
      meta_monitor_manager_reload (monitor_manager);
    }

  source_virtual->monitors_changed_handler_id =
    g_signal_connect (monitor_manager, "monitors-changed-internal",
                      G_CALLBACK (on_monitors_changed),
                      source_virtual);

  return initable_parent_iface->init (initable, cancellable, error);
}

static void
init_initable_iface (GInitableIface *iface)
{
  initable_parent_iface = g_type_interface_peek_parent (iface);

  iface->init = meta_stream_source_virtual_initable_init;
}

static void
meta_stream_source_virtual_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (object);

  switch (prop_id)
    {
    case PROP_MODE_INFOS:
      source_virtual->mode_infos =
        g_list_copy_deep (g_value_get_pointer (value),
                          (GCopyFunc) meta_virtual_mode_info_dup,
                          NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stream_source_virtual_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (object);

  switch (prop_id)
    {
    case PROP_MODE_INFOS:
      g_value_set_pointer (value, source_virtual->mode_infos);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stream_source_virtual_dispose (GObject *object)
{
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (object);
  MetaStreamSource *source = META_STREAM_SOURCE (source_virtual);
  MetaBackend *backend = backend_from_source (source);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GObjectClass *parent_class =
    G_OBJECT_CLASS (meta_stream_source_virtual_parent_class);

  update_frame_clock_driver (source_virtual, NULL);

  g_clear_signal_handler (&source_virtual->monitors_changed_handler_id,
                          monitor_manager);

  parent_class->dispose (object);

  g_clear_object (&source_virtual->virtual_monitor);
  g_clear_list (&source_virtual->mode_infos,
                (GDestroyNotify) meta_virtual_mode_info_free);
}

static void
meta_stream_source_virtual_class_init (MetaStreamSourceVirtualClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaStreamSourceClass *source_class =
    META_STREAM_SOURCE_CLASS (klass);

  object_class->set_property = meta_stream_source_virtual_set_property;
  object_class->get_property = meta_stream_source_virtual_get_property;
  object_class->dispose = meta_stream_source_virtual_dispose;

  source_class->get_specs = meta_stream_source_virtual_get_specs;
  source_class->enable = meta_stream_source_virtual_enable;
  source_class->disable = meta_stream_source_virtual_disable;
  source_class->record_to_buffer =
    meta_stream_source_virtual_record_to_buffer;
  source_class->record_to_framebuffer =
    meta_stream_source_virtual_record_to_framebuffer;
  source_class->queue_follow_up =
    meta_stream_source_virtual_queue_follow_up;
  source_class->is_cursor_metadata_valid =
    meta_stream_source_virtual_is_cursor_metadata_valid;
  source_class->set_cursor_metadata =
    meta_stream_source_virtual_set_cursor_metadata;
  source_class->notify_params_updated =
    meta_stream_source_virtual_notify_params_updated;
  source_class->dispatch =
    meta_stream_source_virtual_dispatch;
  source_class->append_tags =
    meta_stream_source_virtual_append_tags;
  source_class->tag_changed =
    meta_stream_source_virtual_tag_changed;

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
meta_stream_source_virtual_init (MetaStreamSourceVirtual *source_virtual)
{
  source_virtual->cursor_bitmap_invalid = TRUE;
}

static void
meta_stream_frame_clock_driver_schedule_update (ClutterFrameClockDriver *frame_clock_driver)
{
  MetaStreamFrameClockDriver *driver =
    META_STREAM_FRAME_CLOCK_DRIVER (frame_clock_driver);

  if (driver->source)
    meta_stream_source_request_process (driver->source);
}

static void
meta_stream_frame_clock_driver_class_init (MetaStreamFrameClockDriverClass *klass)
{
  ClutterFrameClockDriverClass *driver_class =
    CLUTTER_FRAME_CLOCK_DRIVER_CLASS (klass);

  driver_class->schedule_update =
    meta_stream_frame_clock_driver_schedule_update;
}

static void
meta_stream_frame_clock_driver_init (MetaStreamFrameClockDriver *driver)
{
}

MetaStreamSourceVirtual *
meta_stream_source_virtual_new (MetaStreamVirtual  *virtual_stream,
                                GList              *mode_infos,
                                GError            **error)
{
  return g_initable_new (META_TYPE_STREAM_SOURCE_VIRTUAL, NULL, error,
                         "stream", virtual_stream,
                         "must-drive", FALSE,
                         "mode-infos", mode_infos,
                         NULL);
}

ClutterStageView *
meta_stream_source_virtual_get_view (MetaStreamSourceVirtual *source_virtual)
{
  return view_from_source (META_STREAM_SOURCE (source_virtual));
}

MetaLogicalMonitor *
meta_stream_source_virtual_logical_monitor (MetaStreamSourceVirtual *source_virtual)
{
  MetaVirtualMonitor *virtual_monitor;
  MetaOutput *output;
  MetaMonitor *monitor;

  virtual_monitor = source_virtual->virtual_monitor;
  if (!virtual_monitor)
    return NULL;

  output = meta_virtual_monitor_get_output (virtual_monitor);
  monitor = meta_output_get_monitor (output);
  return meta_monitor_get_logical_monitor (monitor);
}
