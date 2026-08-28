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

#include "backends/native/meta-backend-native-types.h"

#include "meta-dbus-cast-kms.h"

#define META_TYPE_CAST_KMS_GRANT_BROKER \
  (meta_cast_kms_grant_broker_get_type ())
G_DECLARE_FINAL_TYPE (MetaCastKmsGrantBroker, meta_cast_kms_grant_broker,
                      META, CAST_KMS_GRANT_BROKER,
                      MetaDBusCastKmsSkeleton)

MetaCastKmsGrantBroker *
meta_cast_kms_grant_broker_new (MetaBackendNative *backend_native);
