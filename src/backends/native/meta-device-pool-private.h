/*
 * Copyright (C) 2021 Red Hat, Inc.
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
#include "backends/native/meta-device-pool.h"

#define META_TYPE_DEVICE_POOL (meta_device_pool_get_type ())
G_DECLARE_FINAL_TYPE (MetaDevicePool, meta_device_pool,
                      META, DEVICE_POOL,
                      GObject)

MetaDevicePool * meta_device_pool_new (MetaBackendNative *backend_native);
