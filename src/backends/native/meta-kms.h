/*
 * Copyright (C) 2018 Red Hat
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

#include "backends/meta-backend-private.h"
#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-thread.h"

enum
{
  META_KMS_ERROR_USER_INHIBITED,
  META_KMS_ERROR_DENY_LISTED,
  META_KMS_ERROR_NOT_SUPPORTED,
  META_KMS_ERROR_EMPTY_UPDATE,
  META_KMS_ERROR_DISCARDED,
};

typedef enum _MetaKmsFlags
{
  META_KMS_FLAG_NONE = 0,
  META_KMS_FLAG_NO_MODE_SETTING = 1 << 0,
} MetaKmsFlags;

#define META_KMS_ERROR meta_kms_error_quark ()
GQuark meta_kms_error_quark (void);

#define META_TYPE_KMS (meta_kms_get_type ())
G_DECLARE_FINAL_TYPE (MetaKms, meta_kms, META, KMS, MetaThread)

void meta_kms_discard_pending_page_flips (MetaKms *kms);

void meta_kms_notify_modes_set (MetaKms *kms);

META_EXPORT_TEST
MetaBackend * meta_kms_get_backend (MetaKms *kms);

META_EXPORT_TEST
GList * meta_kms_get_devices (MetaKms *kms);

void meta_kms_resume (MetaKms *kms);

MetaKmsDevice * meta_kms_create_device (MetaKms            *kms,
                                        const char         *path,
                                        MetaKmsDeviceFlag   flags,
                                        GError            **error);

gboolean meta_kms_is_shutting_down (MetaKms *kms);

MetaKms * meta_kms_new (MetaBackend   *backend,
                        MetaKmsFlags   flags,
                        GError       **error);

void meta_kms_notify_probed (MetaKms *kms);

META_EXPORT_TEST
void meta_kms_inhibit_kernel_thread (MetaKms *kms);

META_EXPORT_TEST
void meta_kms_uninhibit_kernel_thread (MetaKms *kms);

META_EXPORT_TEST
MetaKmsCursorManager * meta_kms_get_cursor_manager (MetaKms *kms);
