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

#ifndef META_COLOR_PROFILE_H
#define META_COLOR_PROFILE_H

#include <colord.h>
#include <glib-object.h>
#include <stdint.h>

#include "backends/meta-backend-types.h"
#include "core/util-private.h"

#define META_TYPE_COLOR_PROFILE (meta_color_profile_get_type ())
G_DECLARE_FINAL_TYPE (MetaColorProfile, meta_color_profile,
                      META, COLOR_PROFILE,
                      GObject)

MetaColorProfile * meta_color_profile_new_from_icc (MetaColorManager *color_manager,
                                                    CdIcc            *icc,
                                                    GBytes           *raw_bytes);

gboolean meta_color_profile_equals_bytes (MetaColorProfile *color_profile,
                                          GBytes           *bytes);

const uint8_t * meta_color_profile_get_data (MetaColorProfile *color_profile);

size_t meta_color_profile_get_data_size (MetaColorProfile *color_profile);

META_EXPORT_TEST
CdIcc * meta_color_profile_get_cd_icc (MetaColorProfile *color_profile);

gboolean meta_color_profile_is_ready (MetaColorProfile *color_profile);

#endif /* META_COLOR_PROFILE_H */
