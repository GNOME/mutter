/*
 * Copyright (C) 2026 Red Hat
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
#include <stdint.h>

typedef enum _MetaCastKmsGrantProfile
{
  META_CAST_KMS_GRANT_PROFILE_DISPLAY_V1 = 1,
  META_CAST_KMS_GRANT_PROFILE_DISPLAY_CEC_V1 = 2,
  META_CAST_KMS_GRANT_PROFILE_DISPLAY_CEC_AUDIO_V1 = 3,
} MetaCastKmsGrantProfile;

struct drm_castkms_capture_query_caps;

gboolean meta_cast_kms_grant_profile_get_rights (uint16_t   profile,
                                                 uint32_t  *out_rights);

gboolean meta_cast_kms_grant_check_caps (const struct drm_castkms_capture_query_caps *caps,
                                       uint32_t                                     rights);
