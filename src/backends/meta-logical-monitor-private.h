/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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
 */

#pragma once

#include <glib-object.h>

#include "backends/meta-monitor-private.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-monitor-manager-private.h"
#include "core/util-private.h"
#include "meta/boxes.h"

#include "meta/meta-logical-monitor.h"

#define META_MAX_OUTPUTS_PER_MONITOR 4

struct _MetaLogicalMonitor
{
  GObject parent;

  int number;
  MtkRectangle rect;
  gboolean is_primary;
  gboolean is_presentation; /* XXX: not yet used */
  gboolean in_fullscreen;
  float scale;
  MtkMonitorTransform transform;

  GList *monitors;
};

typedef struct _MetaLogicalMonitorId MetaLogicalMonitorId;

typedef void (* MetaLogicalMonitorCrtcFunc) (MetaLogicalMonitor *logical_monitor,
                                             MetaMonitor        *monitor,
                                             MetaOutput         *output,
                                             MetaCrtc           *crtc,
                                             gpointer            user_data);

MetaLogicalMonitor * meta_logical_monitor_new (MetaMonitorManager       *monitor_manager,
                                               MetaLogicalMonitorConfig *logical_monitor_config,
                                               int                       monitor_number);

MetaLogicalMonitor * meta_logical_monitor_new_derived (MetaMonitorManager *monitor_manager,
                                                       MetaMonitor        *monitor,
                                                       MtkRectangle        layout,
                                                       float               scale,
                                                       int                 monitor_number);

void meta_logical_monitor_add_monitor (MetaLogicalMonitor *logical_monitor,
                                       MetaMonitor        *monitor);

META_EXPORT_TEST
gboolean meta_logical_monitor_is_primary (MetaLogicalMonitor *logical_monitor);

void meta_logical_monitor_make_primary (MetaLogicalMonitor *logical_monitor);

META_EXPORT_TEST
float meta_logical_monitor_get_scale (MetaLogicalMonitor *logical_monitor);

MtkMonitorTransform meta_logical_monitor_get_transform (MetaLogicalMonitor *logical_monitor);

META_EXPORT_TEST
MtkRectangle meta_logical_monitor_get_layout (MetaLogicalMonitor *logical_monitor);

gboolean meta_logical_monitor_has_neighbor (MetaLogicalMonitor   *logical_monitor,
                                            MetaLogicalMonitor   *neighbor,
                                            MetaDisplayDirection  neighbor_dir);

void meta_logical_monitor_foreach_crtc (MetaLogicalMonitor        *logical_monitor,
                                        MetaLogicalMonitorCrtcFunc func,
                                        gpointer                   user_data);

void meta_logical_monitor_id_free (MetaLogicalMonitorId *id);

MetaLogicalMonitorId * meta_logical_monitor_id_dup (const MetaLogicalMonitorId *id);

gboolean meta_logical_monitor_id_equal (const MetaLogicalMonitorId *id,
                                        const MetaLogicalMonitorId *other_id);

const MetaLogicalMonitorId * meta_logical_monitor_get_id (MetaLogicalMonitor *logical_monitor);

MetaLogicalMonitorId * meta_logical_monitor_dup_id (MetaLogicalMonitor *logical_monitor);

MetaMonitorManager * meta_logical_monitor_get_monitor_manager (MetaLogicalMonitor *logical_monitor);

gboolean meta_logical_monitor_update (MetaLogicalMonitor       *logical_monitor,
                                      MetaLogicalMonitorConfig *logical_monitor_config,
                                      int                       number);

gboolean meta_logical_monitor_update_derived (MetaLogicalMonitor *logical_monitor,
                                              int                 number,
                                              float               global_scale);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaLogicalMonitorId, meta_logical_monitor_id_free)
