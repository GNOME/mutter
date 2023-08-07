/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
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

#include "backends/meta-monitor-config-manager.h"

typedef enum _MetaConfigStore
{
  META_CONFIG_STORE_SYSTEM,
  META_CONFIG_STORE_USER,
} MetaConfigStore;

typedef struct _MetaMonitorConfigPolicy
{
  gboolean enable_dbus;
} MetaMonitorConfigPolicy;

#define META_TYPE_MONITOR_CONFIG_STORE (meta_monitor_config_store_get_type ())
G_DECLARE_FINAL_TYPE (MetaMonitorConfigStore, meta_monitor_config_store,
                      META, MONITOR_CONFIG_STORE, GObject)

META_EXPORT_TEST
MetaMonitorConfigStore * meta_monitor_config_store_new (MetaMonitorManager *monitor_manager);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_store_lookup (MetaMonitorConfigStore *config_store,
                                                       MetaMonitorsConfigKey  *key);

META_EXPORT_TEST
void meta_monitor_config_store_add (MetaMonitorConfigStore *config_store,
                                    MetaMonitorsConfig     *config);

META_EXPORT_TEST
void meta_monitor_config_store_remove (MetaMonitorConfigStore *config_store,
                                       MetaMonitorsConfig     *config);

META_EXPORT_TEST
gboolean meta_monitor_config_store_set_custom (MetaMonitorConfigStore  *config_store,
                                               const char              *read_path,
                                               const char              *write_path,
                                               MetaMonitorsConfigFlag   flags,
                                               GError                 **error);

META_EXPORT_TEST
GList * meta_monitor_config_store_get_stores_policy (MetaMonitorConfigStore *config_store);

META_EXPORT_TEST
int meta_monitor_config_store_get_config_count (MetaMonitorConfigStore *config_store);

META_EXPORT_TEST
MetaMonitorManager * meta_monitor_config_store_get_monitor_manager (MetaMonitorConfigStore *config_store);

META_EXPORT_TEST
void meta_monitor_config_store_reset (MetaMonitorConfigStore *config_store);

META_EXPORT_TEST
const MetaMonitorConfigPolicy * meta_monitor_config_store_get_policy (MetaMonitorConfigStore *config_store);
