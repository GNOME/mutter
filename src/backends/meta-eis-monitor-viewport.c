/*
 * Copyright (C) 2023-2025 Red Hat Inc.
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

#include "backends/meta-eis-monitor-viewport.h"

#include "backends/meta-eis-viewport.h"
#include "backends/meta-logical-monitor-private.h"

struct _MetaEisMonitorViewport
{
  GObject parent;

  MetaLogicalMonitor *logical_monitor;
};

static void meta_eis_viewport_init_iface (MetaEisViewportInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (MetaEisMonitorViewport,
                               meta_eis_monitor_viewport,
                               G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (META_TYPE_EIS_VIEWPORT,
                                                      meta_eis_viewport_init_iface))

static gboolean
meta_eis_monitor_viewport_is_standalone (MetaEisViewport *viewport)
{
  return FALSE;
}

static const char *
meta_eis_monitor_viewport_get_mapping_id (MetaEisViewport *viewport)
{
  return NULL;
}

static gboolean
meta_eis_monitor_viewport_get_position (MetaEisViewport *viewport,
                                        int             *out_x,
                                        int             *out_y)
{
  MetaEisMonitorViewport *eis_monitor_viewport =
    META_EIS_MONITOR_VIEWPORT (viewport);
  MtkRectangle layout;

  layout = meta_logical_monitor_get_layout (eis_monitor_viewport->logical_monitor);
  *out_x = layout.x;
  *out_y = layout.y;
  return TRUE;
}

static void
meta_eis_monitor_viewport_get_size (MetaEisViewport *viewport,
                                    int             *out_width,
                                    int             *out_height)
{
  MetaEisMonitorViewport *eis_monitor_viewport =
    META_EIS_MONITOR_VIEWPORT (viewport);
  MtkRectangle layout;

  layout = meta_logical_monitor_get_layout (eis_monitor_viewport->logical_monitor);
  *out_width = layout.width;
  *out_height = layout.height;
}

static double
meta_eis_monitor_viewport_get_physical_scale (MetaEisViewport *viewport)
{
  MetaEisMonitorViewport *eis_monitor_viewport =
    META_EIS_MONITOR_VIEWPORT (viewport);

  return meta_logical_monitor_get_scale (eis_monitor_viewport->logical_monitor);
}

static gboolean
meta_eis_monitor_viewport_transform_coordinate (MetaEisViewport *viewport,
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
meta_eis_viewport_init_iface (MetaEisViewportInterface *eis_viewport_iface)
{
  eis_viewport_iface->is_standalone = meta_eis_monitor_viewport_is_standalone;
  eis_viewport_iface->get_mapping_id = meta_eis_monitor_viewport_get_mapping_id;
  eis_viewport_iface->get_position = meta_eis_monitor_viewport_get_position;
  eis_viewport_iface->get_size = meta_eis_monitor_viewport_get_size;
  eis_viewport_iface->get_physical_scale = meta_eis_monitor_viewport_get_physical_scale;
  eis_viewport_iface->transform_coordinate = meta_eis_monitor_viewport_transform_coordinate;
}

static void
meta_eis_monitor_viewport_class_init (MetaEisMonitorViewportClass *klass)
{
}

static void
meta_eis_monitor_viewport_init (MetaEisMonitorViewport *eis_monitor_viewport)
{
}

MetaEisMonitorViewport *
meta_eis_monitor_viewport_new (MetaLogicalMonitor *logical_monitor)
{
  MetaEisMonitorViewport *eis_monitor_viewport;

  eis_monitor_viewport = g_object_new (META_TYPE_EIS_MONITOR_VIEWPORT,
                                       NULL);
  eis_monitor_viewport->logical_monitor = logical_monitor;

  return eis_monitor_viewport;
}
