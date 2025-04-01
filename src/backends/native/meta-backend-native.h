/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#pragma once

#include "backends/meta-backend-private.h"
#include "backends/meta-launcher.h"
#include "backends/meta-udev.h"
#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-clutter-backend-native.h"
#include "backends/native/meta-kms-types.h"

#define META_BACKEND_HEADLESS_INPUT_SEAT "meta-headless-seat0"
#define META_BACKEND_TEST_INPUT_SEAT "meta-test-seat0"

#define META_TYPE_BACKEND_NATIVE (meta_backend_native_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaBackendNative,
                          meta_backend_native,
                          META, BACKEND_NATIVE,
                          MetaBackend)

gboolean meta_backend_native_activate_vt (MetaBackendNative  *backend_native,
                                          int                 vt,
                                          GError            **error);

META_EXPORT_TEST
MetaKms * meta_backend_native_get_kms (MetaBackendNative *native);

MetaDrmLeaseManager * meta_backend_native_get_drm_lease_manager (MetaBackendNative *backend_native);
