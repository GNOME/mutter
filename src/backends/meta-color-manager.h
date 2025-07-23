/*
 * Copyright (C) 2021 Jeremy Cline
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

#include "backends/meta-backend-types.h"
#include "core/util-private.h"

#define META_TYPE_COLOR_MANAGER (meta_color_manager_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaColorManager, meta_color_manager,
                          META, COLOR_MANAGER,
                          GObject)

MetaBackend *
meta_color_manager_get_backend (MetaColorManager *color_manager);

META_EXPORT_TEST
MetaColorDevice * meta_color_manager_get_color_device (MetaColorManager *color_manager,
                                                       MetaMonitor      *monitor);

void meta_color_manager_set_brightness (MetaColorManager *color_manager,
                                        int               brightness);

unsigned int meta_color_manager_get_default_temperature (MetaColorManager *color_manager);
