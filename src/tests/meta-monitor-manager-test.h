/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat, Inc.
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

#include "backends/native/meta-monitor-manager-native.h"
#include "core/util-private.h"

typedef struct _MetaMonitorTestSetup
{
  GList *modes;
  GList *outputs;
  GList *crtcs;
} MetaMonitorTestSetup;

typedef MetaMonitorTestSetup * (* MetaCreateTestSetupFunc) (MetaBackend *backend);

#define META_TYPE_MONITOR_MANAGER_TEST (meta_monitor_manager_test_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaMonitorManagerTest, meta_monitor_manager_test,
                      META, MONITOR_MANAGER_TEST, MetaMonitorManagerNative)

META_EXPORT
void meta_init_monitor_test_setup (MetaCreateTestSetupFunc func);

META_EXPORT
void meta_monitor_manager_test_read_current (MetaMonitorManager *manager);

META_EXPORT
void meta_monitor_manager_test_emulate_hotplug (MetaMonitorManagerTest *manager_test,
                                                MetaMonitorTestSetup   *test_setup);

META_EXPORT
void meta_monitor_manager_test_set_handles_transforms (MetaMonitorManagerTest *manager_test,
                                                       gboolean                handles_transforms);

META_EXPORT
int meta_monitor_manager_test_get_tiled_monitor_count (MetaMonitorManagerTest *manager_test);

META_EXPORT
void meta_monitor_manager_test_set_layout_mode (MetaMonitorManagerTest       *manager_test,
                                                MetaLogicalMonitorLayoutMode  layout_mode);
