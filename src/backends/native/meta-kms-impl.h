/*
 * Copyright (C) 2018 Red Hat
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-page-flip-private.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-thread-impl.h"

typedef struct _MetaKmsUpdateFilter MetaKmsUpdateFilter;

#define META_TYPE_KMS_IMPL (meta_kms_impl_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsImpl, meta_kms_impl,
                      META, KMS_IMPL, MetaThreadImpl)

typedef MetaKmsUpdate * (* MetaKmsUpdateFilterFunc) (MetaKmsImpl       *impl_device,
                                                     MetaKmsCrtc       *crtc,
                                                     MetaKmsUpdate     *update,
                                                     MetaKmsUpdateFlag  flags,
                                                     gpointer           user_data);

MetaKms * meta_kms_impl_get_kms (MetaKmsImpl *impl);

void meta_kms_impl_add_impl_device (MetaKmsImpl       *impl,
                                    MetaKmsImplDevice *impl_device);

void meta_kms_impl_remove_impl_device (MetaKmsImpl       *impl,
                                       MetaKmsImplDevice *impl_device);

void meta_kms_impl_discard_pending_page_flips (MetaKmsImpl *impl);

void meta_kms_impl_resume (MetaKmsImpl *impl);

void meta_kms_impl_prepare_shutdown (MetaKmsImpl *impl);

void meta_kms_impl_notify_modes_set (MetaKmsImpl *impl);

MetaKmsImpl * meta_kms_impl_new (MetaKms *kms);

MetaKmsUpdateFilter * meta_kms_impl_add_update_filter (MetaKmsImpl             *impl,
                                                       MetaKmsUpdateFilterFunc  func,
                                                       gpointer                 user_data);

void meta_kms_impl_remove_update_filter (MetaKmsImpl         *impl,
                                         MetaKmsUpdateFilter *filter);

MetaKmsUpdate * meta_kms_impl_filter_update (MetaKmsImpl       *impl,
                                             MetaKmsCrtc       *crtc,
                                             MetaKmsUpdate     *update,
                                             MetaKmsUpdateFlag  flags);
