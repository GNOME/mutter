/*
 * Copyright (C) 2024 Red Hat
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
#include <gio/gio.h>

#include "backends/meta-backend-types.h"
#include "core/util-private.h"

#define META_TYPE_BACKLIGHT (meta_backlight_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaBacklight,
                          meta_backlight,
                          META, BACKLIGHT,
                          GObject)

META_EXPORT_TEST
void meta_backlight_get_brightness_info (MetaBacklight *backlight,
                                         int           *brightness_min_out,
                                         int           *brightness_max_out);

META_EXPORT_TEST
int meta_backlight_get_brightness (MetaBacklight *backlight);

META_EXPORT_TEST
void meta_backlight_set_brightness (MetaBacklight *backlight,
                                    int            brightness);
