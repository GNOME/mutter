/*
 * Copyright (C) 2020 Red Hat
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

#ifndef META_KMS_MODE_PRIVATE_H
#define META_KMS_MODE_PRIVATE_H

#include "backends/native/meta-kms-mode.h"

uint32_t meta_kms_mode_ensure_blob_id (MetaKmsMode  *mode,
                                       GError      **error);

void meta_kms_mode_free (MetaKmsMode *mode);

MetaKmsMode * meta_kms_mode_new (MetaKmsImplDevice     *impl_device,
                                 const drmModeModeInfo *drm_mode,
                                 MetaKmsModeFlag        flags);

#endif /* META_KMS_MODE_PRIVATE_H */
