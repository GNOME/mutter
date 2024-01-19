/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "backends/native/meta-backend-native-types.h"

#define META_TYPE_RENDER_DEVICE (meta_render_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaRenderDevice, meta_render_device,
                          META, RENDER_DEVICE,
                          GObject)

MetaBackend * meta_render_device_get_backend (MetaRenderDevice *render_device);

EGLDisplay meta_render_device_get_egl_display (MetaRenderDevice *render_device);

const char * meta_render_device_get_name (MetaRenderDevice *render_device);

gboolean meta_render_device_is_hardware_accelerated (MetaRenderDevice *render_device);

MetaDeviceFile * meta_render_device_get_device_file (MetaRenderDevice *render_device);

MetaDrmBuffer * meta_render_device_allocate_dma_buf (MetaRenderDevice    *render_device,
                                                     int                  width,
                                                     int                  height,
                                                     uint32_t             format,
                                                     uint64_t            *modifiers,
                                                     int                  n_modifiers,
                                                     MetaDrmBufferFlags   flags,
                                                     GError             **error);

MetaDrmBuffer * meta_render_device_import_dma_buf (MetaRenderDevice  *render_device,
                                                   MetaDrmBuffer     *buffer,
                                                   GError           **error);

MetaDrmBuffer * meta_render_device_allocate_dumb_buf (MetaRenderDevice  *render_device,
                                                      int                width,
                                                      int                height,
                                                      uint32_t           format,
                                                      GError           **error);
