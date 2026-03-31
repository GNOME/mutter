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

#include "backends/meta-stream-virtual.h"

#include "backends/meta-eis.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-stream-source-virtual.h"
#include "backends/meta-virtual-monitor.h"

struct _MetaStreamVirtual
{
  MetaStream parent;

  GList *mode_infos;
};

static void meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface);

G_DEFINE_TYPE_WITH_CODE (MetaStreamVirtual, meta_stream_virtual,
                         META_TYPE_STREAM,
                         G_IMPLEMENT_INTERFACE (META_TYPE_EIS_VIEWPORT,
                                                meta_eis_viewport_iface_init))

static gboolean
meta_stream_virtual_is_standalone (MetaEisViewport *viewport)
{
  return FALSE;
}

static const char *
meta_stream_virtual_get_mapping_id (MetaEisViewport *viewport)
{
  MetaStream *stream = META_STREAM (viewport);

  return meta_stream_get_mapping_id (stream);
}

static gboolean
meta_stream_virtual_get_position (MetaEisViewport *viewport,
                                  int             *out_x,
                                  int             *out_y)
{
  MetaStreamVirtual *stream_virtual =
    META_STREAM_VIRTUAL (viewport);
  MetaStream *stream = META_STREAM (stream_virtual);
  MetaStreamSource *source = meta_stream_get_source (stream);
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  MetaLogicalMonitor *logical_monitor =
    meta_stream_source_virtual_logical_monitor (source_virtual);
  MtkRectangle layout;

  layout = meta_logical_monitor_get_layout (logical_monitor);
  *out_x = layout.x;
  *out_y = layout.y;
  return TRUE;
}

static void
meta_stream_virtual_get_size (MetaEisViewport *viewport,
                              int             *out_width,
                              int             *out_height)
{
  MetaStreamVirtual *stream_virtual =
    META_STREAM_VIRTUAL (viewport);
  MetaStream *stream = META_STREAM (stream_virtual);
  MetaStreamSource *source = meta_stream_get_source (stream);
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  MetaLogicalMonitor *logical_monitor =
    meta_stream_source_virtual_logical_monitor (source_virtual);
  MtkRectangle layout;

  layout = meta_logical_monitor_get_layout (logical_monitor);
  *out_width = layout.width;
  *out_height = layout.height;
}

static double
meta_stream_virtual_get_physical_scale (MetaEisViewport *viewport)
{
  MetaStreamVirtual *stream_virtual =
    META_STREAM_VIRTUAL (viewport);
  MetaStream *stream = META_STREAM (stream_virtual);
  MetaStreamSource *source = meta_stream_get_source (stream);
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  MetaLogicalMonitor *logical_monitor =
    meta_stream_source_virtual_logical_monitor (source_virtual);

  return meta_logical_monitor_get_scale (logical_monitor);
}

static gboolean
meta_stream_virtual_transform_coordinate (MetaEisViewport *viewport,
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
  eis_viewport_iface->is_standalone = meta_stream_virtual_is_standalone;
  eis_viewport_iface->get_mapping_id = meta_stream_virtual_get_mapping_id;
  eis_viewport_iface->get_position = meta_stream_virtual_get_position;
  eis_viewport_iface->get_size = meta_stream_virtual_get_size;
  eis_viewport_iface->get_physical_scale = meta_stream_virtual_get_physical_scale;
  eis_viewport_iface->transform_coordinate = meta_stream_virtual_transform_coordinate;
}

static MetaStreamSource *
meta_stream_virtual_create_source (MetaStream  *stream,
                                   GError     **error)
{
  MetaStreamVirtual *stream_virtual =
    META_STREAM_VIRTUAL (stream);
  MetaStreamSourceVirtual *source_virtual;

  source_virtual =
    meta_stream_source_virtual_new (stream_virtual,
                                    stream_virtual->mode_infos,
                                    error);
  if (!source_virtual)
    return NULL;

  return META_STREAM_SOURCE (source_virtual);
}

static gboolean
meta_stream_virtual_transform_position (MetaStream *stream,
                                        double      stream_x,
                                        double      stream_y,
                                        double     *x,
                                        double     *y)
{
  MetaStreamSource *source = meta_stream_get_source (stream);
  MetaStreamSourceVirtual *source_virtual =
    META_STREAM_SOURCE_VIRTUAL (source);
  ClutterStageView *view;
  MtkRectangle view_layout;

  if (!meta_stream_source_is_enabled (source))
    return FALSE;

  view = meta_stream_source_virtual_get_view (source_virtual);
  if (!view)
    return FALSE;

  clutter_stage_view_get_layout (view, &view_layout);
  *x = stream_x + view_layout.x;
  *y = stream_y + view_layout.y;

  return TRUE;
}

static void
meta_stream_virtual_finalize (GObject *object)
{
  MetaStreamVirtual *stream_virtual = META_STREAM_VIRTUAL (object);

  g_clear_list (&stream_virtual->mode_infos,
                (GDestroyNotify) meta_virtual_mode_info_free);

  G_OBJECT_CLASS (meta_stream_virtual_parent_class)->finalize (object);
}

static void
meta_stream_virtual_class_init (MetaStreamVirtualClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaStreamClass *stream_class = META_STREAM_CLASS (klass);

  object_class->finalize = meta_stream_virtual_finalize;

  stream_class->create_source = meta_stream_virtual_create_source;
  stream_class->transform_position = meta_stream_virtual_transform_position;
}

static void
meta_stream_virtual_init (MetaStreamVirtual *stream_virtual)
{
}

MetaStreamVirtual *
meta_stream_virtual_new (MetaBackend           *backend,
                         MetaStreamCursorMode   cursor_mode,
                         GList                 *mode_infos)
{
  MetaStreamVirtual *stream_virtual;

  stream_virtual = g_object_new (META_TYPE_STREAM_VIRTUAL,
                                 "backend", backend,
                                 "cursor-mode", cursor_mode,
                                 NULL);

  stream_virtual->mode_infos =
    g_list_copy_deep (mode_infos, (GCopyFunc) meta_virtual_mode_info_dup, NULL);

  return stream_virtual;
}
