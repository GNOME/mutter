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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_KMS_IMPL_DEVICE_H
#define META_KMS_IMPL_DEVICE_H

#include <glib-object.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-page-flip-private.h"
#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-kms-update.h"

typedef struct _MetaKmsDeviceCaps
{
  gboolean has_cursor_size;
  uint64_t cursor_width;
  uint64_t cursor_height;
} MetaKmsDeviceCaps;

typedef struct _MetaKmsProp MetaKmsProp;

struct _MetaKmsProp
{
  const char *name;
  uint32_t type;
  void (* parse) (MetaKmsImplDevice  *impl_device,
                  MetaKmsProp        *prop,
                  drmModePropertyPtr  drm_prop,
                  uint64_t            value,
                  gpointer            user_data);

  uint32_t prop_id;
};

#define META_TYPE_KMS_IMPL_DEVICE (meta_kms_impl_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaKmsImplDevice, meta_kms_impl_device,
                          META, KMS_IMPL_DEVICE,
                          GObject)

struct _MetaKmsImplDeviceClass
{
  GObjectClass parent_class;

  void (* setup_drm_event_context) (MetaKmsImplDevice *impl_device,
                                    drmEventContext   *drm_event_context);
  MetaKmsFeedback * (* process_update) (MetaKmsImplDevice *impl_device,
                                        MetaKmsUpdate     *update);
  void (* handle_page_flip_callback) (MetaKmsImplDevice   *impl_device,
                                      MetaKmsPageFlipData *page_flip_data);
  void (* discard_pending_page_flips) (MetaKmsImplDevice *impl_device);
  void (* prepare_shutdown) (MetaKmsImplDevice *impl_device);
};

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

drmModePropertyPtr meta_kms_impl_device_find_property (MetaKmsImplDevice       *impl_device,
                                                       drmModeObjectProperties *props,
                                                       const char              *prop_name,
                                                       int                     *idx);

int meta_kms_impl_device_get_fd (MetaKmsImplDevice *impl_device);

int meta_kms_impl_device_leak_fd (MetaKmsImplDevice *impl_device);

void meta_kms_impl_device_update_states (MetaKmsImplDevice *impl_device);

void meta_kms_impl_device_predict_states (MetaKmsImplDevice *impl_device,
                                          MetaKmsUpdate     *update);

MetaKmsPlane * meta_kms_impl_device_add_fake_plane (MetaKmsImplDevice *impl_device,
                                                    MetaKmsPlaneType   plane_type,
                                                    MetaKmsCrtc       *crtc);

void meta_kms_impl_device_init_prop_table (MetaKmsImplDevice *impl_device,
                                           uint32_t          *drm_props,
                                           uint64_t          *drm_props_values,
                                           int                n_drm_props,
                                           MetaKmsProp       *props,
                                           int                n_props,
                                           gpointer           user_data);

void meta_kms_impl_device_reload_prop_values (MetaKmsImplDevice *impl_device,
                                              uint32_t          *drm_props,
                                              uint64_t          *drm_prop_values,
                                              int                n_drm_props,
                                              gpointer           user_data,
                                              ...);

MetaKmsFeedback * meta_kms_impl_device_process_update (MetaKmsImplDevice *impl_device,
                                                       MetaKmsUpdate     *update);

void meta_kms_impl_device_handle_page_flip_callback (MetaKmsImplDevice   *impl_device,
                                                     MetaKmsPageFlipData *page_flip_data);

void meta_kms_impl_device_discard_pending_page_flips (MetaKmsImplDevice *impl_device);

int meta_kms_impl_device_close (MetaKmsImplDevice *impl_device);

gboolean meta_kms_impl_device_init_mode_setting (MetaKmsImplDevice  *impl_device,
                                                 GError            **error);

void meta_kms_impl_device_prepare_shutdown (MetaKmsImplDevice *impl_device);

#endif /* META_KMS_IMPL_DEVICE_H */
