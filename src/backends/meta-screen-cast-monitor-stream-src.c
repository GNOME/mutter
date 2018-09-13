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

#include "backends/meta-backend-private.h"
#include "backends/meta-screen-cast-monitor-stream.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"
#include "core/boxes-private.h"

struct _MetaScreenCastMonitorStreamSrc
{
  MetaScreenCastStreamSrc parent;

  gulong stage_painted_handler_id;
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

  scale = logical_monitor->scale;
  *width = (int) roundf (logical_monitor->rect.width * scale);
  *height = (int) roundf (logical_monitor->rect.height * scale);
  *frame_rate = meta_monitor_mode_get_refresh_rate (mode);
}

static void
stage_painted (ClutterActor                   *actor,
               MetaScreenCastMonitorStreamSrc *monitor_src)
{
  MetaScreenCastStreamSrc *src = META_SCREEN_CAST_STREAM_SRC (monitor_src);

  meta_screen_cast_stream_src_maybe_record_frame (src);
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
  ClutterStage *stage;

  inhibit_hw_cursor (monitor_src);

  stage = get_stage (monitor_src);
  monitor_src->stage_painted_handler_id =
    g_signal_connect_after (stage, "paint",
                            G_CALLBACK (stage_painted),
                            monitor_src);
  clutter_actor_queue_redraw (CLUTTER_ACTOR (stage));
}

static void
meta_screen_cast_monitor_stream_src_disable (MetaScreenCastStreamSrc *src)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  ClutterStage *stage;

  uninhibit_hw_cursor (monitor_src);

  stage = get_stage (monitor_src);
  g_signal_handler_disconnect (stage, monitor_src->stage_painted_handler_id);
  monitor_src->stage_painted_handler_id = 0;
}

static void
meta_screen_cast_monitor_stream_src_record_frame (MetaScreenCastStreamSrc *src,
                                                  uint8_t                 *data)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (src);
  ClutterStage *stage;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;

  stage = get_stage (monitor_src);
  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  clutter_stage_capture_into (stage, FALSE, &logical_monitor->rect, data);
}

static gboolean
meta_screen_cast_monitor_stream_src_is_cursor_sprite_inhibited (MetaHwCursorInhibitor *inhibitor,
                                                                MetaCursorSprite      *cursor_sprite)
{
  MetaScreenCastMonitorStreamSrc *monitor_src =
    META_SCREEN_CAST_MONITOR_STREAM_SRC (inhibitor);
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
  MetaRectangle logical_monitor_layout;
  ClutterRect logical_monitor_rect;
  MetaCursorRenderer *cursor_renderer;
  ClutterRect cursor_rect;

  monitor = get_monitor (monitor_src);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  logical_monitor_layout = meta_logical_monitor_get_layout (logical_monitor);
  logical_monitor_rect =
    meta_rectangle_to_clutter_rect (&logical_monitor_layout);

  cursor_renderer = get_cursor_renderer (monitor_src);
  cursor_rect = meta_cursor_renderer_calculate_rect (cursor_renderer,
                                                     cursor_sprite);

  return clutter_rect_intersection (&cursor_rect, &logical_monitor_rect, NULL);
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
}
