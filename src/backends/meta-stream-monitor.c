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

#include "backends/meta-stream-monitor.h"

#include "backends/meta-eis.h"
#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-stream-source-monitor.h"

enum
{
  PROP_0,

  PROP_MONITOR,
};

struct _MetaStreamMonitor
{
  MetaStream parent;

  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;
};

static void meta_eis_viewport_iface_init (MetaEisViewportInterface *eis_viewport_iface);

G_DEFINE_TYPE_WITH_CODE (MetaStreamMonitor,
                         meta_stream_monitor,
                         META_TYPE_STREAM,
                         G_IMPLEMENT_INTERFACE (META_TYPE_EIS_VIEWPORT,
                                                meta_eis_viewport_iface_init))

static gboolean
update_monitor (MetaStreamMonitor *stream_monitor,
                MetaMonitor       *new_monitor)
{
  MetaLogicalMonitor *new_logical_monitor;

  new_logical_monitor = meta_monitor_get_logical_monitor (new_monitor);
  if (!new_logical_monitor)
    return FALSE;

  if (!mtk_rectangle_equal (&new_logical_monitor->rect,
                            &stream_monitor->logical_monitor->rect))
    return FALSE;

  g_set_object (&stream_monitor->monitor, new_monitor);
  g_set_object (&stream_monitor->logical_monitor, new_logical_monitor);

  return TRUE;
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     MetaStreamMonitor  *stream_monitor)
{
  MetaMonitor *new_monitor = NULL;
  GList *monitors;
  GList *l;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  for (l = monitors; l; l = l->next)
    {
      MetaMonitor *other_monitor = l->data;

      if (meta_monitor_is_same_as (stream_monitor->monitor, other_monitor))
        {
          new_monitor = other_monitor;
          break;
        }
    }

  if (!new_monitor || !update_monitor (stream_monitor, new_monitor))
    meta_stream_stop (META_STREAM (stream_monitor));
}

static MetaStreamSource *
meta_stream_monitor_create_source (MetaStream  *stream,
                                   GError     **error)
{
  MetaStreamMonitor *stream_monitor = META_STREAM_MONITOR (stream);
  MetaStreamSourceMonitor *source_monitor;

  source_monitor = meta_stream_source_monitor_new (stream_monitor, error);
  if (!source_monitor)
    return NULL;

  return META_STREAM_SOURCE (source_monitor);
}

static gboolean
meta_stream_monitor_transform_position (MetaStream *stream,
                                        double      stream_x,
                                        double      stream_y,
                                        double     *x,
                                        double     *y)
{
  MetaStreamMonitor *stream_monitor =
    META_STREAM_MONITOR (stream);
  MetaBackend *backend = meta_monitor_get_backend (stream_monitor->monitor);
  MtkRectangle logical_monitor_layout;
  double scale;

  logical_monitor_layout =
    meta_logical_monitor_get_layout (stream_monitor->logical_monitor);

  if (meta_backend_is_stage_views_scaled (backend))
    scale = meta_logical_monitor_get_scale (stream_monitor->logical_monitor);
  else
    scale = 1.0;

  *x = logical_monitor_layout.x + stream_x / scale;
  *y = logical_monitor_layout.y + stream_y / scale;

  return TRUE;
}

static gboolean
meta_stream_monitor_is_standalone (MetaEisViewport *viewport)
{
  return FALSE;
}

static const char *
meta_stream_monitor_get_mapping_id (MetaEisViewport *viewport)
{
  MetaStream *stream = META_STREAM (viewport);

  return meta_stream_get_mapping_id (stream);
}

static gboolean
meta_stream_monitor_get_position (MetaEisViewport *viewport,
                                  int             *out_x,
                                  int             *out_y)
{
  MetaStreamMonitor *stream_monitor =
    META_STREAM_MONITOR (viewport);
  MtkRectangle layout;

  layout = meta_logical_monitor_get_layout (stream_monitor->logical_monitor);
  *out_x = layout.x;
  *out_y = layout.y;
  return TRUE;
}

static void
meta_stream_monitor_get_size (MetaEisViewport *viewport,
                              int             *out_width,
                              int             *out_height)
{
  MetaStreamMonitor *stream_monitor =
    META_STREAM_MONITOR (viewport);
  MtkRectangle layout;

  layout = meta_logical_monitor_get_layout (stream_monitor->logical_monitor);
  *out_width = layout.width;
  *out_height = layout.height;
}

static double
meta_stream_monitor_get_physical_scale (MetaEisViewport *viewport)
{
  MetaStreamMonitor *stream_monitor =
    META_STREAM_MONITOR (viewport);

  return meta_logical_monitor_get_scale (stream_monitor->logical_monitor);
}

static gboolean
meta_stream_monitor_transform_coordinate (MetaEisViewport *viewport,
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
  eis_viewport_iface->is_standalone = meta_stream_monitor_is_standalone;
  eis_viewport_iface->get_mapping_id = meta_stream_monitor_get_mapping_id;
  eis_viewport_iface->get_position = meta_stream_monitor_get_position;
  eis_viewport_iface->get_size = meta_stream_monitor_get_size;
  eis_viewport_iface->get_physical_scale = meta_stream_monitor_get_physical_scale;
  eis_viewport_iface->transform_coordinate = meta_stream_monitor_transform_coordinate;
}

static void
meta_stream_monitor_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaStreamMonitor *stream_monitor =
    META_STREAM_MONITOR (object);
  MetaLogicalMonitor *logical_monitor;

  switch (prop_id)
    {
    case PROP_MONITOR:
      g_set_object (&stream_monitor->monitor, g_value_get_object (value));
      logical_monitor = meta_monitor_get_logical_monitor (stream_monitor->monitor);
      g_set_object (&stream_monitor->logical_monitor, logical_monitor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stream_monitor_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  MetaStreamMonitor *stream_monitor =
    META_STREAM_MONITOR (object);

  switch (prop_id)
    {
    case PROP_MONITOR:
      g_value_set_object (value, stream_monitor->monitor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_stream_monitor_finalize (GObject *object)
{
  MetaStreamMonitor *stream_monitor =
    META_STREAM_MONITOR (object);

  g_clear_object (&stream_monitor->monitor);
  g_clear_object (&stream_monitor->logical_monitor);

  G_OBJECT_CLASS (meta_stream_monitor_parent_class)->finalize (object);
}

static void
meta_stream_monitor_class_init (MetaStreamMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaStreamClass *stream_class = META_STREAM_CLASS (klass);

  object_class->set_property = meta_stream_monitor_set_property;
  object_class->get_property = meta_stream_monitor_get_property;
  object_class->finalize = meta_stream_monitor_finalize;

  stream_class->create_source = meta_stream_monitor_create_source;
  stream_class->transform_position = meta_stream_monitor_transform_position;

  g_object_class_install_property (object_class,
                                   PROP_MONITOR,
                                   g_param_spec_object ("monitor", NULL, NULL,
                                                        META_TYPE_MONITOR,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
meta_stream_monitor_init (MetaStreamMonitor *stream_monitor)
{
}

MetaMonitor *
meta_stream_monitor_get_monitor (MetaStreamMonitor *stream_monitor)
{
  return stream_monitor->monitor;
}

MetaStreamMonitor *
meta_stream_monitor_new (MetaBackend           *backend,
                         MetaMonitor           *monitor,
                         MetaStreamCursorMode   cursor_mode,
                         GError               **error)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaStreamMonitor *stream_monitor;

  if (!meta_monitor_is_active (monitor))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Monitor not active");
      return NULL;
    }

  stream_monitor = g_object_new (META_TYPE_STREAM_MONITOR,
                                 "backend", backend,
                                 "cursor-mode", cursor_mode,
                                 "is-configured", TRUE,
                                 "monitor", monitor,
                                 NULL);
  if (!stream_monitor)
    return NULL;

  g_signal_connect_object (monitor_manager, "monitors-changed-internal",
                           G_CALLBACK (on_monitors_changed),
                           stream_monitor, 0);

  return stream_monitor;
}

void
meta_stream_monitor_get_geometry (MetaStreamMonitor *stream_monitor,
                                  MtkRectangle      *geometry)
{
  *geometry = meta_logical_monitor_get_layout (stream_monitor->logical_monitor);
}
