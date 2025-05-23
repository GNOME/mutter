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

#include "backends/meta-monitor-private.h"
#include "backends/meta-monitor-manager-private.h"

#define META_TYPE_MONITOR_CONFIG_MANAGER (meta_monitor_config_manager_get_type ())
G_DECLARE_FINAL_TYPE (MetaMonitorConfigManager, meta_monitor_config_manager,
                      META, MONITOR_CONFIG_MANAGER, GObject)

typedef struct _MetaMonitorConfig
{
  MetaMonitorSpec *monitor_spec;
  MetaMonitorModeSpec *mode_spec;
  gboolean enable_underscanning;
  gboolean has_max_bpc;
  unsigned int max_bpc;
  MetaOutputRGBRange rgb_range;
  MetaColorMode color_mode;
} MetaMonitorConfig;

typedef struct _MetaLogicalMonitorConfig
{
  MtkRectangle layout;
  GList *monitor_configs;
  MtkMonitorTransform transform;
  float scale;
  gboolean is_primary;
  gboolean is_presentation;
} MetaLogicalMonitorConfig;

typedef struct _MetaMonitorsConfigKey
{
  GList *monitor_specs;
  MetaLogicalMonitorLayoutMode layout_mode;
} MetaMonitorsConfigKey;

enum _MetaMonitorsConfigFlag
{
  META_MONITORS_CONFIG_FLAG_NONE = 0,
  META_MONITORS_CONFIG_FLAG_SYSTEM_CONFIG = (1 << 0),
};

struct _MetaMonitorsConfig
{
  GObject parent;

  MetaMonitorsConfig *parent_config;
  MetaMonitorsConfigKey *key;
  GList *logical_monitor_configs;

  GList *disabled_monitor_specs;
  GList *for_lease_monitor_specs;

  MetaMonitorsConfigFlag flags;

  MetaLogicalMonitorLayoutMode layout_mode;

  MetaMonitorSwitchConfigType switch_config;
};

#define META_TYPE_MONITORS_CONFIG (meta_monitors_config_get_type ())
G_DECLARE_FINAL_TYPE (MetaMonitorsConfig, meta_monitors_config,
                      META, MONITORS_CONFIG, GObject)

META_EXPORT_TEST
MetaMonitorConfigManager * meta_monitor_config_manager_new (MetaMonitorManager *monitor_manager);

META_EXPORT_TEST
MetaMonitorConfigStore * meta_monitor_config_manager_get_store (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
gboolean meta_monitor_config_manager_assign (MetaMonitorManager *manager,
                                             MetaMonitorsConfig *config,
                                             GPtrArray         **crtc_assignments,
                                             GPtrArray         **output_assignments,
                                             GError            **error);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_get_stored (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_create_linear (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_create_fallback (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_create_suggested (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_create_for_orientation (MetaMonitorConfigManager *config_manager,
                                                                         MetaMonitorsConfig       *base_config,
                                                                         MtkMonitorTransform       transform);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_create_for_builtin_orientation (MetaMonitorConfigManager *config_manager,
                                                                                 MetaMonitorsConfig       *base_config);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_create_for_rotate_monitor (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_create_for_switch_config (MetaMonitorConfigManager    *config_manager,
                                                                           MetaMonitorSwitchConfigType  config_type);

META_EXPORT_TEST
void meta_monitor_config_manager_set_current (MetaMonitorConfigManager *config_manager,
                                              MetaMonitorsConfig       *config);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_get_current (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_pop_previous (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitor_config_manager_get_previous (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
void meta_monitor_config_manager_clear_history (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
void meta_monitor_config_manager_save_current (MetaMonitorConfigManager *config_manager);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitors_config_new_full (GList                        *logical_monitor_configs,
                                                    GList                        *disabled_monitors,
                                                    GList                        *for_lease_monitors,
                                                    MetaLogicalMonitorLayoutMode  layout_mode,
                                                    MetaMonitorsConfigFlag        flags);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitors_config_new (MetaMonitorManager           *monitor_manager,
                                               GList                        *logical_monitor_configs,
                                               MetaLogicalMonitorLayoutMode  layout_mode,
                                               MetaMonitorsConfigFlag        flags);

META_EXPORT_TEST
MetaMonitorSwitchConfigType meta_monitors_config_get_switch_config (MetaMonitorsConfig *config);

META_EXPORT_TEST
void meta_monitors_config_set_switch_config (MetaMonitorsConfig          *config,
                                             MetaMonitorSwitchConfigType  switch_config);

META_EXPORT_TEST
void meta_monitors_config_set_parent_config (MetaMonitorsConfig *config,
                                             MetaMonitorsConfig *parent_config);

META_EXPORT_TEST
unsigned int meta_monitors_config_key_hash (gconstpointer config_key);

META_EXPORT_TEST
gboolean meta_monitors_config_key_equal (gconstpointer config_key_a,
                                         gconstpointer config_key_b);

META_EXPORT_TEST
void meta_monitors_config_key_free (MetaMonitorsConfigKey *config_key);

META_EXPORT_TEST
void meta_logical_monitor_config_free (MetaLogicalMonitorConfig *logical_monitor_config);

META_EXPORT_TEST
void meta_monitor_config_free (MetaMonitorConfig *monitor_config);

META_EXPORT_TEST
MetaMonitorsConfigKey * meta_create_monitors_config_key_for_current_state (MetaMonitorManager *monitor_manager);

META_EXPORT_TEST
gboolean meta_logical_monitor_configs_have_monitor (GList           *logical_monitor_configs,
                                                    MetaMonitorSpec *monitor_spec);

gboolean meta_logical_monitor_configs_have_visible_monitor (MetaMonitorManager *monitor_manager,
                                                            GList              *logical_monitor_configs,
                                                            MetaMonitor        *monitor);

META_EXPORT_TEST
gboolean meta_verify_monitor_mode_spec (MetaMonitorModeSpec *monitor_mode_spec,
                                        GError             **error);

META_EXPORT_TEST
gboolean meta_verify_monitor_spec (MetaMonitorSpec *monitor_spec,
                                   GError         **error);

META_EXPORT_TEST
gboolean meta_verify_monitor_config (MetaMonitorConfig *monitor_config,
                                     GError           **error);

META_EXPORT_TEST
gboolean meta_verify_logical_monitor_config (MetaLogicalMonitorConfig    *logical_monitor_config,
                                             MetaLogicalMonitorLayoutMode layout_mode,
                                             MetaMonitorManager          *monitor_manager,
                                             GError                     **error);

META_EXPORT_TEST
gboolean meta_verify_monitors_config (MetaMonitorsConfig *config,
                                      MetaMonitorManager *monitor_manager,
                                      GError            **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaMonitorConfig, meta_monitor_config_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaLogicalMonitorConfig,
                               meta_logical_monitor_config_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaMonitorsConfigKey,
                               meta_monitors_config_key_free)
