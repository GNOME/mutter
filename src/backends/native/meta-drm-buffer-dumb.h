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

#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-drm-buffer-private.h"

#define META_TYPE_DRM_BUFFER_DUMB (meta_drm_buffer_dumb_get_type ())
G_DECLARE_FINAL_TYPE (MetaDrmBufferDumb,
                      meta_drm_buffer_dumb,
                      META, DRM_BUFFER_DUMB,
                      MetaDrmBuffer)

META_EXPORT_TEST
MetaDrmBufferDumb * meta_drm_buffer_dumb_new (MetaDeviceFile  *device,
                                              int              width,
                                              int              height,
                                              uint32_t         format,
                                              GError         **error);

int meta_drm_buffer_dumb_ensure_dmabuf_fd (MetaDrmBufferDumb  *buffer_dumb,
                                           GError            **error);

void * meta_drm_buffer_dumb_get_data (MetaDrmBufferDumb *buffer_dumb);
