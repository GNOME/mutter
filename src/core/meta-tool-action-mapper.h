/*
 * Copyright (C) 2020 Red Hat
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

#include "clutter/clutter.h"
#include "meta/display.h"
#include "meta/meta-monitor-manager.h"
#include "core/meta-tablet-action-mapper.h"

#define META_TYPE_TOOL_ACTION_MAPPER (meta_tool_action_mapper_get_type ())
G_DECLARE_FINAL_TYPE (MetaToolActionMapper, meta_tool_action_mapper,
                      META, TOOL_ACTION_MAPPER, MetaTabletActionMapper)

MetaToolActionMapper *meta_tool_action_mapper_new (MetaBackend * backend);
