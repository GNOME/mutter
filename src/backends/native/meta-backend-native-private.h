/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat
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
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#pragma once

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-barrier-native.h"

META_EXPORT_TEST
MetaDevicePool * meta_backend_native_get_device_pool (MetaBackendNative *native);


MetaRenderDevice * meta_backend_native_take_render_device (MetaBackendNative  *backend_native,
                                                           const char         *device_path,
                                                           GError            **error);
