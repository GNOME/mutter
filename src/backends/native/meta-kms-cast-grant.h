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

#include <glib-object.h>
#include <stdint.h>

#include "backends/native/meta-kms-types.h"

typedef struct _MetaKmsCastGrantInfo
{
  uint32_t grant_id;
  uint32_t connector_id;
  uint32_t output_index;
  uint32_t rights;
  uint32_t flags;
  uint32_t initial_state;
  uint16_t capture_uapi_major;
  uint16_t capture_uapi_minor;
} MetaKmsCastGrantInfo;

#define META_TYPE_KMS_CAST_GRANT (meta_kms_cast_grant_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsCastGrant, meta_kms_cast_grant,
                      META, KMS_CAST_GRANT, GObject)

MetaKmsCastGrant * meta_kms_cast_grant_new (MetaKmsDevice  *kms_device,
                                            uint32_t        connector_id,
                                            uint32_t        rights,
                                            GError        **error);

const MetaKmsCastGrantInfo *
meta_kms_cast_grant_get_info (MetaKmsCastGrant *grant);

int meta_kms_cast_grant_steal_holder_fd (MetaKmsCastGrant *grant);

int meta_kms_cast_grant_get_control_fd (MetaKmsCastGrant *grant);

gboolean meta_kms_cast_grant_revoke (MetaKmsCastGrant  *grant,
                                     GError           **error);
