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

#include "backends/meta-screen-cast-virtual-stream.h"

#include "backends/meta-eis.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-screen-cast-virtual-stream-src.h"
#include "backends/meta-virtual-monitor.h"

struct _MetaScreenCastVirtualStream
{
  MetaScreenCastStream parent;
};

static void meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastVirtualStream,
                         meta_screen_cast_virtual_stream,
                         META_TYPE_SCREEN_CAST_STREAM,
                         G_IMPLEMENT_INTERFACE (META_TYPE_EIS_VIEWPORT,
                                                meta_eis_viewport_iface_init))

MetaScreenCastVirtualStream *
meta_screen_cast_virtual_stream_new (MetaScreenCastSession     *session,
                                     GDBusConnection           *connection,
                                     MetaScreenCastCursorMode   cursor_mode,
                                     MetaScreenCastFlag         flags,
                                     GError                   **error)
{
  MetaScreenCastVirtualStream *virtual_stream;

  virtual_stream = g_initable_new (META_TYPE_SCREEN_CAST_VIRTUAL_STREAM,
                                   NULL,
                                   error,
                                   "session", session,
                                   "connection", connection,
                                   "cursor-mode", cursor_mode,
                                   "flags", flags,
                                   NULL);
  if (!virtual_stream)
    return NULL;

  return virtual_stream;
}

static gboolean
meta_screen_cast_virtual_stream_is_standalone (MetaEisViewport *viewport)
{
  return FALSE;
}

static const char *
meta_screen_cast_virtual_stream_get_mapping_id (MetaEisViewport *viewport)
{
  MetaScreenCastStream *stream = META_SCREEN_CAST_STREAM (viewport);

  return meta_screen_cast_stream_get_mapping_id (stream);
}

static gboolean
meta_screen_cast_virtual_stream_get_position (MetaEisViewport *viewport,
                                              int             *out_x,
                                              int             *out_y)
{
  MetaScreenCastVirtualStream *virtual_stream =
    META_SCREEN_CAST_VIRTUAL_STREAM (viewport);
  MetaScreenCastStream *stream = META_SCREEN_CAST_STREAM (virtual_stream);
  MetaScreenCastStreamSrc *src = meta_screen_cast_stream_get_src (stream);
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaLogicalMonitor *logical_monitor =
    meta_screen_cast_virtual_stream_src_logical_monitor (virtual_src);
  MtkRectangle layout;

  layout = meta_logical_monitor_get_layout (logical_monitor);
  *out_x = layout.x;
  *out_y = layout.y;
  return TRUE;
}

static void
meta_screen_cast_virtual_stream_get_size (MetaEisViewport *viewport,
                                          int             *out_width,
                                          int             *out_height)
{
  MetaScreenCastVirtualStream *virtual_stream =
    META_SCREEN_CAST_VIRTUAL_STREAM (viewport);
  MetaScreenCastStream *stream = META_SCREEN_CAST_STREAM (virtual_stream);
  MetaScreenCastStreamSrc *src = meta_screen_cast_stream_get_src (stream);
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaLogicalMonitor *logical_monitor =
    meta_screen_cast_virtual_stream_src_logical_monitor (virtual_src);
  MtkRectangle layout;

  layout = meta_logical_monitor_get_layout (logical_monitor);
  *out_width = layout.width;
  *out_height = layout.height;
}

static double
meta_screen_cast_virtual_stream_get_physical_scale (MetaEisViewport *viewport)
{
  MetaScreenCastVirtualStream *virtual_stream =
    META_SCREEN_CAST_VIRTUAL_STREAM (viewport);
  MetaScreenCastStream *stream = META_SCREEN_CAST_STREAM (virtual_stream);
  MetaScreenCastStreamSrc *src = meta_screen_cast_stream_get_src (stream);
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  MetaLogicalMonitor *logical_monitor =
    meta_screen_cast_virtual_stream_src_logical_monitor (virtual_src);

  return meta_logical_monitor_get_scale (logical_monitor);
}

static gboolean
meta_screen_cast_virtual_stream_transform_coordinate (MetaEisViewport *viewport,
                                                      double           x,
                                                      double           y,
                                                      double          *out_x,
                                                      double          *out_y)
{
  *out_x = x;
  *out_y = y;
  return TRUE;
}

static void
meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface)
{
  eis_viewport_iface->is_standalone = meta_screen_cast_virtual_stream_is_standalone;
  eis_viewport_iface->get_mapping_id = meta_screen_cast_virtual_stream_get_mapping_id;
  eis_viewport_iface->get_position = meta_screen_cast_virtual_stream_get_position;
  eis_viewport_iface->get_size = meta_screen_cast_virtual_stream_get_size;
  eis_viewport_iface->get_physical_scale = meta_screen_cast_virtual_stream_get_physical_scale;
  eis_viewport_iface->transform_coordinate = meta_screen_cast_virtual_stream_transform_coordinate;
}

static MetaScreenCastStreamSrc *
meta_screen_cast_virtual_stream_create_src (MetaScreenCastStream  *stream,
                                           GError               **error)
{
  MetaScreenCastVirtualStream *virtual_stream =
    META_SCREEN_CAST_VIRTUAL_STREAM (stream);
  MetaScreenCastVirtualStreamSrc *virtual_stream_src;

  virtual_stream_src = meta_screen_cast_virtual_stream_src_new (virtual_stream,
                                                                error);
  if (!virtual_stream_src)
    return NULL;

  return META_SCREEN_CAST_STREAM_SRC (virtual_stream_src);
}

static void
meta_screen_cast_virtual_stream_set_parameters (MetaScreenCastStream *stream,
                                                GVariantBuilder      *parameters_builder)
{
}

static gboolean
meta_screen_cast_virtual_stream_transform_position (MetaScreenCastStream *stream,
                                                    double                stream_x,
                                                    double                stream_y,
                                                    double               *x,
                                                    double               *y)
{
  MetaScreenCastStreamSrc *src = meta_screen_cast_stream_get_src (stream);
  MetaScreenCastVirtualStreamSrc *virtual_src =
    META_SCREEN_CAST_VIRTUAL_STREAM_SRC (src);
  ClutterStageView *view;
  MtkRectangle view_layout;

  if (!meta_screen_cast_stream_src_is_enabled (src))
    return FALSE;

  view = meta_screen_cast_virtual_stream_src_get_view (virtual_src);
  if (!view)
    return FALSE;

  clutter_stage_view_get_layout (view, &view_layout);
  *x = stream_x + view_layout.x;
  *y = stream_y + view_layout.y;

  return TRUE;
}

static void
meta_screen_cast_virtual_stream_init (MetaScreenCastVirtualStream *virtual_stream)
{
}

static void
meta_screen_cast_virtual_stream_class_init (MetaScreenCastVirtualStreamClass *klass)
{
  MetaScreenCastStreamClass *stream_class =
    META_SCREEN_CAST_STREAM_CLASS (klass);

  stream_class->create_src = meta_screen_cast_virtual_stream_create_src;
  stream_class->set_parameters = meta_screen_cast_virtual_stream_set_parameters;
  stream_class->transform_position = meta_screen_cast_virtual_stream_transform_position;
}
