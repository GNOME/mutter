/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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

typedef enum
{
  META_MONITOR_SWITCH_CONFIG_ALL_MIRROR,
  META_MONITOR_SWITCH_CONFIG_ALL_LINEAR,
  META_MONITOR_SWITCH_CONFIG_EXTERNAL,
  META_MONITOR_SWITCH_CONFIG_BUILTIN,
  META_MONITOR_SWITCH_CONFIG_UNKNOWN,
} MetaMonitorSwitchConfigType;

typedef enum _MetaPowerSaveChangeReason
{
  META_POWER_SAVE_CHANGE_REASON_MODE_CHANGE,
  META_POWER_SAVE_CHANGE_REASON_HOTPLUG,
} MetaPowerSaveChangeReason;

typedef struct _MetaMonitorManagerClass    MetaMonitorManagerClass;
typedef struct _MetaMonitorManager         MetaMonitorManager;

META_EXPORT
GType meta_monitor_manager_get_type (void);

META_EXPORT
gint meta_monitor_manager_get_monitor_for_connector (MetaMonitorManager *manager,
                                                     const char         *connector);

META_EXPORT
gboolean meta_monitor_manager_get_is_builtin_display_on (MetaMonitorManager *manager);

META_EXPORT
void meta_monitor_manager_switch_config (MetaMonitorManager          *manager,
                                         MetaMonitorSwitchConfigType  config_type);

META_EXPORT
gboolean meta_monitor_manager_can_switch_config (MetaMonitorManager *manager);

META_EXPORT
MetaMonitorSwitchConfigType meta_monitor_manager_get_switch_config (MetaMonitorManager *manager);

META_EXPORT
int meta_monitor_manager_get_display_configuration_timeout (MetaMonitorManager *manager);

META_EXPORT
gboolean meta_monitor_manager_get_panel_orientation_managed (MetaMonitorManager *manager);

META_EXPORT
GList * meta_monitor_manager_get_monitors (MetaMonitorManager *manager);

META_EXPORT
GList * meta_monitor_manager_get_logical_monitors (MetaMonitorManager *manager);
