/*
 * Copyright (C) 2018 Canonical Ltd.
 * Copyright (C) 2019 Red Hat Inc.
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
 */

#pragma once

#include <gbm.h>

#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-drm-buffer-private.h"

#define META_TYPE_DRM_BUFFER_GBM (meta_drm_buffer_gbm_get_type ())
G_DECLARE_FINAL_TYPE (MetaDrmBufferGbm,
                      meta_drm_buffer_gbm,
                      META, DRM_BUFFER_GBM,
                      MetaDrmBuffer)

MetaDrmBufferGbm * meta_drm_buffer_gbm_new_lock_front (MetaDeviceFile      *device_file,
                                                       struct gbm_surface  *gbm_surface,
                                                       MetaDrmBufferFlags   flags,
                                                       GError             **error);


MetaDrmBufferGbm * meta_drm_buffer_gbm_new_take (MetaDeviceFile      *device_file,
                                                 struct gbm_bo       *gbm_bo,
                                                 MetaDrmBufferFlags   flags,
                                                 GError             **error);

struct gbm_bo * meta_drm_buffer_gbm_get_bo (MetaDrmBufferGbm *buffer_gbm);
