/*
 * Copyright (C) 2016 Red Hat
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
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

#include "config.h"

#include "backends/meta-monitor-config-utils.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-monitor-manager-private.h"
#include "core/boxes-private.h"
#include "meta/meta-monitor-manager.h"

static MetaMonitorConfig *
meta_monitor_config_copy (MetaMonitorConfig *monitor_config)
{
  MetaMonitorConfig *new_monitor_config;

  new_monitor_config = g_new0 (MetaMonitorConfig, 1);
  *new_monitor_config = (MetaMonitorConfig) {
    .monitor_spec = meta_monitor_spec_clone (monitor_config->monitor_spec),
    .mode_spec = g_memdup2 (monitor_config->mode_spec,
                            sizeof (MetaMonitorModeSpec)),
    .enable_underscanning = monitor_config->enable_underscanning,
    .has_max_bpc = monitor_config->has_max_bpc,
    .max_bpc = monitor_config->max_bpc
  };

  return new_monitor_config;
}

static GList *
meta_clone_monitor_config_list (GList *monitor_configs)
{
  return g_list_copy_deep (monitor_configs,
                           (GCopyFunc) meta_monitor_config_copy,
                           NULL);
}

static MetaLogicalMonitorConfig *
meta_logical_monitor_config_copy (MetaLogicalMonitorConfig *logical_monitor_config)
{
  MetaLogicalMonitorConfig *new_logical_monitor_config;

  new_logical_monitor_config =
    g_memdup2 (logical_monitor_config,
               sizeof (MetaLogicalMonitorConfig));
  new_logical_monitor_config->monitor_configs =
    meta_clone_monitor_config_list (logical_monitor_config->monitor_configs);

  return new_logical_monitor_config;
}

GList *
meta_clone_logical_monitor_config_list (GList *logical_monitor_configs)
{
  return g_list_copy_deep (logical_monitor_configs,
                           (GCopyFunc) meta_logical_monitor_config_copy,
                           NULL);
}

MetaMonitorsConfig *
meta_monitors_config_copy (MetaMonitorsConfig *monitors_config)
{
  MetaMonitorsConfig *new_monitors_config;
  GList *logical_monitor_configs;
  GList *disabled_monitor_specs;
  GList *for_lease_monitor_specs;

  logical_monitor_configs =
    meta_clone_logical_monitor_config_list (monitors_config->logical_monitor_configs);
  disabled_monitor_specs =
    g_list_copy_deep (monitors_config->disabled_monitor_specs,
                      (GCopyFunc) meta_monitor_spec_clone,
                      NULL);
  for_lease_monitor_specs =
    g_list_copy_deep (monitors_config->for_lease_monitor_specs,
                      (GCopyFunc) meta_monitor_spec_clone,
                      NULL);

  new_monitors_config =
    meta_monitors_config_new_full (logical_monitor_configs,
                                   disabled_monitor_specs,
                                   for_lease_monitor_specs,
                                   monitors_config->layout_mode,
                                   monitors_config->flags);
  new_monitors_config->switch_config = monitors_config->switch_config;

  return new_monitors_config;
}

static GList *
find_adjacent_neighbours (GList                    *logical_monitor_configs,
                          MetaLogicalMonitorConfig *logical_monitor_config)
{
  GList *adjacent_neighbors = NULL;
  GList *l;

  if (!logical_monitor_configs->next)
    {
      g_assert (logical_monitor_configs->data == logical_monitor_config);
      return NULL;
    }

  for (l = logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *other_logical_monitor_config = l->data;

      if (logical_monitor_config == other_logical_monitor_config)
        continue;

      if (mtk_rectangle_is_adjacent_to (&logical_monitor_config->layout,
                                        &other_logical_monitor_config->layout))
        {
          adjacent_neighbors = g_list_prepend (adjacent_neighbors,
                                               other_logical_monitor_config);
        }
    }

  return adjacent_neighbors;
}

static void
traverse_new_neighbours (GList                    *logical_monitor_configs,
                         MetaLogicalMonitorConfig *logical_monitor_config,
                         GHashTable               *neighbourhood)
{
  g_autoptr (GList) adjacent_neighbours = NULL;
  GList *l;

  g_hash_table_add (neighbourhood, logical_monitor_config);

  adjacent_neighbours = find_adjacent_neighbours (logical_monitor_configs,
                                                  logical_monitor_config);

  for (l = adjacent_neighbours; l; l = l->next)
    {
      MetaLogicalMonitorConfig *neighbour = l->data;

      if (g_hash_table_contains (neighbourhood, neighbour))
        continue;

      traverse_new_neighbours (logical_monitor_configs, neighbour, neighbourhood);
    }
}

static gboolean
is_connected_to_all (MetaLogicalMonitorConfig *logical_monitor_config,
                     GList                    *logical_monitor_configs)
{
  g_autoptr (GHashTable) neighbourhood = NULL;

  neighbourhood = g_hash_table_new (NULL, NULL);

  traverse_new_neighbours (logical_monitor_configs,
                           logical_monitor_config,
                           neighbourhood);

  return g_hash_table_size (neighbourhood) == g_list_length (logical_monitor_configs);
}

gboolean
meta_verify_logical_monitor_config_list (GList                         *logical_monitor_configs,
                                         MetaLogicalMonitorLayoutMode   layout_mode,
                                         MetaMonitorManager            *monitor_manager,
                                         GError                       **error)
{
  int min_x, min_y;
  gboolean has_primary;
  GList *region;
  GList *l;
  gboolean global_scale_required;

  if (!logical_monitor_configs)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Monitors config incomplete");
      return FALSE;
    }

  global_scale_required =
    !!(meta_monitor_manager_get_capabilities (monitor_manager) &
       META_MONITOR_MANAGER_CAPABILITY_GLOBAL_SCALE_REQUIRED);

  min_x = INT_MAX;
  min_y = INT_MAX;
  region = NULL;
  has_primary = FALSE;

  for (l = logical_monitor_configs; l; l = l->next)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;

      if (!meta_verify_logical_monitor_config (logical_monitor_config,
                                               layout_mode,
                                               monitor_manager,
                                               error))
        return FALSE;

      if (global_scale_required)
        {
          MetaLogicalMonitorConfig *prev_logical_monitor_config =
            l->prev ? l->prev->data : NULL;

          if (prev_logical_monitor_config &&
              (prev_logical_monitor_config->scale !=
               logical_monitor_config->scale))
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Logical monitor scales must be identical");
              return FALSE;
            }
        }

      if (meta_rectangle_overlaps_with_region (region,
                                               &logical_monitor_config->layout))
        {
          g_list_free (region);
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Logical monitors overlap");
          return FALSE;
        }

      if (has_primary && logical_monitor_config->is_primary)
        {
          g_list_free (region);
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Config contains multiple primary logical monitors");
          return FALSE;
        }
      else if (logical_monitor_config->is_primary)
        {
          has_primary = TRUE;
        }

      if (!is_connected_to_all (logical_monitor_config, logical_monitor_configs))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Logical monitors not adjacent");
          return FALSE;
        }

      min_x = MIN (logical_monitor_config->layout.x, min_x);
      min_y = MIN (logical_monitor_config->layout.y, min_y);

      region = g_list_prepend (region, &logical_monitor_config->layout);
    }

  g_list_free (region);

  if (min_x != 0 || min_y != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Logical monitors positions are offset");
      return FALSE;
    }

  if (!has_primary)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Config is missing primary logical");
      return FALSE;
    }

  return TRUE;
}
