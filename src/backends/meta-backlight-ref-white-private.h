/*
 * Copyright (C) 2025 Red Hat
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
#include "backends/meta-backlight-private.h"
#include "backends/meta-output.h"

#define META_TYPE_BACKLIGHT_REF_WHITE (meta_backlight_ref_white_get_type ())
G_DECLARE_FINAL_TYPE (MetaBacklightRefWhite,
                      meta_backlight_ref_white,
                      META, BACKLIGHT_REF_WHITE,
                      MetaBacklight)

MetaBacklightRefWhite * meta_backlight_ref_white_new (MetaBackend *backend,
                                                      MetaMonitor *monitor,
                                                      float        original_ref_white);

float meta_backlight_ref_white_get_original_ref_white (MetaBacklightRefWhite *backlight);
