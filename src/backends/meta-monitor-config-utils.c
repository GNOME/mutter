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
#include "meta/meta-monitor-manager.h"

static GList *
meta_clone_monitor_config_list (GList *monitor_configs_in)
{
  MetaMonitorConfig *monitor_config_in;
  MetaMonitorConfig *monitor_config_out;
  GList *monitor_configs_out = NULL;
  GList *l;

  for (l = monitor_configs_in; l; l = l->next)
    {
      monitor_config_in = l->data;
      monitor_config_out = g_new0 (MetaMonitorConfig, 1);
      *monitor_config_out = (MetaMonitorConfig) {
        .monitor_spec = meta_monitor_spec_clone (monitor_config_in->monitor_spec),
        .mode_spec = g_memdup2 (monitor_config_in->mode_spec,
                                sizeof (MetaMonitorModeSpec)),
        .enable_underscanning = monitor_config_in->enable_underscanning,
        .has_max_bpc = monitor_config_in->has_max_bpc,
        .max_bpc = monitor_config_in->max_bpc
      };
      monitor_configs_out =
        g_list_append (monitor_configs_out, monitor_config_out);
    }

  return monitor_configs_out;
}

GList *
meta_clone_logical_monitor_config_list (GList *logical_monitor_configs_in)
{
  MetaLogicalMonitorConfig *logical_monitor_config_in;
  MetaLogicalMonitorConfig *logical_monitor_config_out;
  GList *logical_monitor_configs_out = NULL;
  GList *l;

  for (l = logical_monitor_configs_in; l; l = l->next)
    {
      logical_monitor_config_in = l->data;

      logical_monitor_config_out =
        g_memdup2 (logical_monitor_config_in,
                   sizeof (MetaLogicalMonitorConfig));
      logical_monitor_config_out->monitor_configs =
        meta_clone_monitor_config_list (logical_monitor_config_in->monitor_configs);

      logical_monitor_configs_out =
        g_list_append (logical_monitor_configs_out, logical_monitor_config_out);
    }

  return logical_monitor_configs_out;
}
