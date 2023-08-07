/*
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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

#include <stddef.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "core/util-private.h"

typedef struct _MetaDrmFormatBuf
{
  char s[5];
} MetaDrmFormatBuf;

META_EXPORT_TEST
float meta_calculate_drm_mode_refresh_rate (const drmModeModeInfo *drm_mode);

META_EXPORT_TEST
int64_t meta_calculate_drm_mode_vblank_duration_us (const drmModeModeInfo *drm_mode);

const char * meta_drm_format_to_string (MetaDrmFormatBuf *tmp,
                                        uint32_t          drm_format);
