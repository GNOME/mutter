/*
 * Copyright (C) 2019 Red Hat
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

#include "backends/native/meta-kms-types.h"
#include "core/util-private.h"

#define META_TYPE_KMS_DEVICE (meta_kms_device_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaKmsDevice, meta_kms_device,
                      META, KMS_DEVICE,
                      GObject)

META_EXPORT_TEST
MetaKms * meta_kms_device_get_kms (MetaKmsDevice *device);

META_EXPORT_TEST
const char * meta_kms_device_get_path (MetaKmsDevice *device);

META_EXPORT_TEST
const char * meta_kms_device_get_driver_name (MetaKmsDevice *device);

const char * meta_kms_device_get_driver_description (MetaKmsDevice *device);

MetaKmsDeviceFlag meta_kms_device_get_flags (MetaKmsDevice *device);

META_EXPORT_TEST
gboolean meta_kms_device_get_cursor_size (MetaKmsDevice *device,
                                          uint64_t      *out_cursor_width,
                                          uint64_t      *out_cursor_height);

gboolean meta_kms_device_prefers_shadow_buffer (MetaKmsDevice *device);

META_EXPORT_TEST
gboolean meta_kms_device_uses_monotonic_clock (MetaKmsDevice *device);

META_EXPORT_TEST
GList * meta_kms_device_get_connectors (MetaKmsDevice *device);

META_EXPORT_TEST
GList * meta_kms_device_get_crtcs (MetaKmsDevice *device);

META_EXPORT_TEST
GList * meta_kms_device_get_planes (MetaKmsDevice *device);

gboolean meta_kms_device_has_cursor_plane_for (MetaKmsDevice*device,
                                               MetaKmsCrtc  *crtc);

GList * meta_kms_device_get_fallback_modes (MetaKmsDevice *device);

META_EXPORT_TEST
MetaKmsFeedback * meta_kms_device_process_update_sync (MetaKmsDevice     *device,
                                                       MetaKmsUpdate     *update,
                                                       MetaKmsUpdateFlag  flags)
  G_GNUC_WARN_UNUSED_RESULT;

META_EXPORT_TEST
void meta_kms_device_post_update (MetaKmsDevice     *device,
                                  MetaKmsUpdate     *update,
                                  MetaKmsUpdateFlag  flags);

META_EXPORT_TEST
void meta_kms_device_await_flush (MetaKmsDevice *device,
                                  MetaKmsCrtc   *crtc);

gboolean meta_kms_device_handle_flush (MetaKmsDevice *device,
                                       MetaKmsCrtc   *crtc);

META_EXPORT_TEST
void meta_kms_device_disable (MetaKmsDevice *device);

MetaKmsDevice * meta_kms_device_new (MetaKms            *kms,
                                     const char         *path,
                                     MetaKmsDeviceFlag   flags,
                                     GError            **error);
