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

#include <glib.h>

#include "backends/meta-monitor-manager-private.h"

GList * meta_clone_logical_monitor_config_list (GList *logical_monitor_configs);

META_EXPORT_TEST
MetaMonitorsConfig * meta_monitors_config_copy (MetaMonitorsConfig *monitors_config);

gboolean meta_verify_logical_monitor_config_list (GList                         *logical_monitor_configs,
                                                  MetaLogicalMonitorLayoutMode   layout_mode,
                                                  MetaMonitorManager            *monitor_manager,
                                                  GError                       **error);
