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
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-page-flip-private.h"
#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms.h"

typedef struct _MetaKmsDeviceCaps
{
  gboolean has_cursor_size;
  uint64_t cursor_width;
  uint64_t cursor_height;

  gboolean prefers_shadow_buffer;
  gboolean uses_monotonic_clock;
  gboolean addfb2_modifiers;
} MetaKmsDeviceCaps;


typedef struct _MetaKmsEnum
{
  const char *name;
  gboolean valid;
  uint64_t value;
  uint64_t bitmask;
} MetaKmsEnum;

typedef struct _MetaKmsProp MetaKmsProp;

struct _MetaKmsProp
{
  const char *name;
  uint32_t type;
  MetaKmsPropType internal_type;

  unsigned int num_enum_values;
  MetaKmsEnum *enum_values;
  uint64_t default_value;

  uint64_t range_min;
  uint64_t range_max;

  int64_t range_min_signed;
  int64_t range_max_signed;

  uint64_t supported_variants;

  uint32_t prop_id;
  uint64_t value;
};

#define META_TYPE_KMS_IMPL_DEVICE (meta_kms_impl_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaKmsImplDevice, meta_kms_impl_device,
                          META, KMS_IMPL_DEVICE,
                          GObject)

struct _MetaKmsImplDeviceClass
{
  GObjectClass parent_class;

  MetaDeviceFile * (* open_device_file) (MetaKmsImplDevice  *impl_device,
                                         const char         *path,
                                         GError            **error);
  void (* setup_drm_event_context) (MetaKmsImplDevice *impl_device,
                                    drmEventContext   *drm_event_context);
  MetaKmsFeedback * (* process_update) (MetaKmsImplDevice *impl_device,
                                        MetaKmsUpdate     *update,
                                        MetaKmsUpdateFlag  flags);
  void (* disable) (MetaKmsImplDevice *impl_device);
  void (* handle_page_flip_callback) (MetaKmsImplDevice   *impl_device,
                                      MetaKmsPageFlipData *page_flip_data);
  void (* discard_pending_page_flips) (MetaKmsImplDevice *impl_device);
  void (* prepare_shutdown) (MetaKmsImplDevice *impl_device);
};

enum
{
  META_KMS_DEVICE_FILE_TAG_ATOMIC = 1 << 0,
  META_KMS_DEVICE_FILE_TAG_SIMPLE = 1 << 1,
};

MetaKmsImpl * meta_kms_impl_device_get_impl (MetaKmsImplDevice *impl_device);

MetaKmsDevice * meta_kms_impl_device_get_device (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_copy_connectors (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_copy_crtcs (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_copy_planes (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_peek_connectors (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_peek_crtcs (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_peek_planes (MetaKmsImplDevice *impl_device);

const MetaKmsDeviceCaps * meta_kms_impl_device_get_caps (MetaKmsImplDevice *impl_device);

GList * meta_kms_impl_device_copy_fallback_modes (MetaKmsImplDevice *impl_device);

const char * meta_kms_impl_device_get_driver_name (MetaKmsImplDevice *impl_device);

const char * meta_kms_impl_device_get_driver_description (MetaKmsImplDevice *impl_device);

const char * meta_kms_impl_device_get_path (MetaKmsImplDevice *impl_device);

gboolean meta_kms_impl_device_dispatch (MetaKmsImplDevice  *impl_device,
                                        GError            **error);

void meta_kms_impl_device_disable (MetaKmsImplDevice *impl_device);

drmModePropertyPtr meta_kms_impl_device_find_property (MetaKmsImplDevice       *impl_device,
                                                       drmModeObjectProperties *props,
                                                       const char              *prop_name,
                                                       int                     *idx);

int meta_kms_impl_device_get_fd (MetaKmsImplDevice *impl_device);

void meta_kms_impl_device_hold_fd (MetaKmsImplDevice *impl_device);

void meta_kms_impl_device_unhold_fd (MetaKmsImplDevice *impl_device);

int meta_kms_impl_device_get_signaled_sync_file (MetaKmsImplDevice *impl_device);

MetaKmsResourceChanges meta_kms_impl_device_update_states (MetaKmsImplDevice *impl_device,
                                                           uint32_t           crtc_id,
                                                           uint32_t           connector_id);

void meta_kms_impl_device_notify_modes_set (MetaKmsImplDevice *impl_device);

MetaKmsPlane * meta_kms_impl_device_add_fake_plane (MetaKmsImplDevice *impl_device,
                                                    MetaKmsPlaneType   plane_type,
                                                    MetaKmsCrtc       *crtc);

void meta_kms_impl_device_update_prop_table (MetaKmsImplDevice *impl_device,
                                             uint32_t          *drm_props,
                                             uint64_t          *drm_props_values,
                                             int                n_drm_props,
                                             MetaKmsProp       *props,
                                             int                n_props);

void meta_kms_impl_device_reload_prop_values (MetaKmsImplDevice *impl_device,
                                              uint32_t          *drm_props,
                                              uint64_t          *drm_prop_values,
                                              int                n_drm_props,
                                              gpointer           user_data,
                                              ...);

MetaKmsFeedback * meta_kms_impl_device_process_update (MetaKmsImplDevice *impl_device,
                                                       MetaKmsUpdate     *update,
                                                       MetaKmsUpdateFlag  flags)
  G_GNUC_WARN_UNUSED_RESULT;

void meta_kms_impl_device_handle_update (MetaKmsImplDevice *impl_device,
                                         MetaKmsUpdate     *update,
                                         MetaKmsUpdateFlag  flags);

void meta_kms_impl_device_await_flush (MetaKmsImplDevice *impl_device,
                                       MetaKmsCrtc       *crtc);

META_EXPORT_TEST
void meta_kms_impl_device_schedule_process (MetaKmsImplDevice *impl_device,
                                            MetaKmsCrtc       *crtc);

void meta_kms_impl_device_handle_page_flip_callback (MetaKmsImplDevice   *impl_device,
                                                     MetaKmsPageFlipData *page_flip_data);

void meta_kms_impl_device_discard_pending_page_flips (MetaKmsImplDevice *impl_device);

gboolean meta_kms_impl_device_init_mode_setting (MetaKmsImplDevice  *impl_device,
                                                 GError            **error);

void meta_kms_impl_device_resume (MetaKmsImplDevice *impl_device);

void meta_kms_impl_device_prepare_shutdown (MetaKmsImplDevice *impl_device);

uint64_t meta_kms_prop_convert_value (MetaKmsProp *prop,
                                      uint64_t     value);
