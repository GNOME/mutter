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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_KMS_IMPL_H
#define META_KMS_IMPL_H

#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-page-flip-private.h"
#include "backends/native/meta-kms.h"

#define META_TYPE_KMS_IMPL (meta_kms_impl_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsImpl, meta_kms_impl,
                      META, KMS_IMPL, GObject)

MetaKms * meta_kms_impl_get_kms (MetaKmsImpl *impl);

MetaKmsFeedback * meta_kms_impl_process_update (MetaKmsImpl   *impl,
                                                MetaKmsUpdate *update);

void meta_kms_impl_add_impl_device (MetaKmsImpl       *impl,
                                    MetaKmsImplDevice *impl_device);

void meta_kms_impl_remove_impl_device (MetaKmsImpl       *impl,
                                       MetaKmsImplDevice *impl_device);

void meta_kms_impl_discard_pending_page_flips (MetaKmsImpl *impl);

void meta_kms_impl_prepare_shutdown (MetaKmsImpl *impl);

MetaKmsImpl * meta_kms_impl_new (MetaKms *kms);

#endif /* META_KMS_IMPL_H */
