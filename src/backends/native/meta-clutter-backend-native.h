/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "backends/native/meta-stage-native.h"
#include "clutter/clutter.h"

#define META_TYPE_CLUTTER_BACKEND_NATIVE (meta_clutter_backend_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaClutterBackendNative, meta_clutter_backend_native,
                      META, CLUTTER_BACKEND_NATIVE,
                      ClutterBackend)

MetaClutterBackendNative * meta_clutter_backend_native_new (MetaBackend    *backend,
                                                            ClutterContext *context);
