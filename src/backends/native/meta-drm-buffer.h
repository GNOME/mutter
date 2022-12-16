/*
 * Copyright (C) 2018 Canonical Ltd.
 * Copyright (C) 2019-2020 Red Hat Inc.
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
 * Author: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#pragma once

#include <glib-object.h>
#include <stdint.h>

#include "cogl/cogl.h"
#include "core/util-private.h"

typedef enum _MetaDrmBufferFlags
{
  META_DRM_BUFFER_FLAG_NONE = 0,
  META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS = 1 << 0,
} MetaDrmBufferFlags;

#define META_TYPE_DRM_BUFFER (meta_drm_buffer_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaDrmBuffer,
                          meta_drm_buffer,
                          META, DRM_BUFFER,
                          GObject)

int meta_drm_buffer_export_fd (MetaDrmBuffer  *buffer,
                               GError        **error);

int meta_drm_buffer_export_fd_for_plane (MetaDrmBuffer  *buffer,
                                         int             plane,
                                         GError        **error);

gboolean meta_drm_buffer_ensure_fb_id (MetaDrmBuffer  *buffer,
                                       GError        **error);

META_EXPORT_TEST
uint32_t meta_drm_buffer_get_fb_id (MetaDrmBuffer *buffer);

uint32_t meta_drm_buffer_get_handle (MetaDrmBuffer *buffer);

int meta_drm_buffer_get_width (MetaDrmBuffer *buffer);

int meta_drm_buffer_get_height (MetaDrmBuffer *buffer);

int meta_drm_buffer_get_n_planes (MetaDrmBuffer *buffer);

int meta_drm_buffer_get_stride (MetaDrmBuffer *buffer);

int meta_drm_buffer_get_stride_for_plane (MetaDrmBuffer *buffer,
                                          int            plane);

int meta_drm_buffer_get_bpp (MetaDrmBuffer *buffer);

uint32_t meta_drm_buffer_get_format (MetaDrmBuffer *buffer);

int meta_drm_buffer_get_offset_for_plane (MetaDrmBuffer *buffer,
                                          int            plane);

uint64_t meta_drm_buffer_get_modifier (MetaDrmBuffer *buffer);
