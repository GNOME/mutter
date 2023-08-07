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

#include "backends/meta-monitor-manager-private.h"

META_EXPORT_TEST
gboolean meta_migrate_old_monitors_config (MetaMonitorConfigStore *config_store,
                                           GFile                  *in_file,
                                           GError                **error);

META_EXPORT_TEST
gboolean meta_migrate_old_user_monitors_config (MetaMonitorConfigStore *config_store,
                                                GError                **error);

META_EXPORT_TEST
gboolean meta_finish_monitors_config_migration (MetaMonitorManager *monitor_manager,
                                                MetaMonitorsConfig *config,
                                                GError            **error);
