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

#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-kms-types.h"
#include "meta/meta-context.h"
#include "meta/boxes.h"

MetaKmsDevice * meta_get_test_kms_device (MetaContext *context);

MetaKmsCrtc * meta_get_test_kms_crtc (MetaKmsDevice *device);

MetaKmsConnector * meta_get_test_kms_connector (MetaKmsDevice *device);

MetaKmsPlane * meta_get_primary_test_plane_for (MetaKmsDevice *device,
                                                MetaKmsCrtc   *crtc);

MetaKmsPlane * meta_get_cursor_test_plane_for (MetaKmsDevice *device,
                                               MetaKmsCrtc   *crtc);

MetaDrmBuffer * meta_create_test_dumb_buffer (MetaKmsDevice *device,
                                              int            width,
                                              int            height);

MetaDrmBuffer * meta_create_test_mode_dumb_buffer (MetaKmsDevice *device,
                                                   MetaKmsMode   *mode);

MetaFixed16Rectangle meta_get_mode_fixed_rect_16 (MetaKmsMode *mode);

MtkRectangle meta_get_mode_rect (MetaKmsMode *mode);
