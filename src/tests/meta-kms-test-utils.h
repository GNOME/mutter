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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#ifndef META_KMS_TEST_UTILS_H
#define META_KMS_TEST_UTILS_H

#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-kms-types.h"
#include "meta/meta-context.h"
#include "meta/boxes.h"

MetaKmsDevice * meta_get_test_kms_device (MetaContext *context);

MetaKmsCrtc * meta_get_test_kms_crtc (MetaKmsDevice *device);

MetaKmsConnector * meta_get_test_kms_connector (MetaKmsDevice *device);

MetaDrmBuffer * meta_create_test_dumb_buffer (MetaKmsDevice *device,
                                              int            width,
                                              int            height);

MetaDrmBuffer * meta_create_test_mode_dumb_buffer (MetaKmsDevice *device,
                                                   MetaKmsMode   *mode);

MetaFixed16Rectangle meta_get_mode_fixed_rect_16 (MetaKmsMode *mode);

MetaRectangle meta_get_mode_rect (MetaKmsMode *mode);

#endif /* META_KMS_TEST_UTILS_H */
