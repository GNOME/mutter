/*
 * Copyright 2021 Red Hat, Inc.
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

#pragma once

#include "backends/meta-backend-types.h"
#include "clutter/clutter.h"

typedef struct _MetaIdleMonitor MetaIdleMonitor;
typedef struct _MetaIdleManager MetaIdleManager;

MetaIdleMonitor * meta_idle_manager_get_core_monitor (MetaIdleManager *idle_manager);

void meta_idle_manager_reset_idle_time (MetaIdleManager *idle_manager);

MetaIdleManager * meta_idle_manager_new (MetaBackend *backend);

void meta_idle_manager_free (MetaIdleManager *idle_manager);
