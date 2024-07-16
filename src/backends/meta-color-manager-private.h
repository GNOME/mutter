/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include <colord.h>
#include <lcms2.h>

#include "backends/meta-backend-types.h"
#include "backends/meta-color-manager.h"

struct _MetaColorManagerClass
{
  GObjectClass parent_class;
};

void meta_color_manager_monitors_changed (MetaColorManager *color_manager);

CdClient * meta_color_manager_get_cd_client (MetaColorManager *color_manager);

META_EXPORT_TEST
MetaColorStore * meta_color_manager_get_color_store (MetaColorManager *color_manager);

META_EXPORT_TEST
gboolean meta_color_manager_is_ready (MetaColorManager *color_manager);

META_EXPORT_TEST
int meta_color_manager_get_num_color_devices (MetaColorManager *color_manager);

cmsContext meta_color_manager_get_lcms_context (MetaColorManager *color_manager);

unsigned int meta_color_manager_get_temperature (MetaColorManager *color_manager);
