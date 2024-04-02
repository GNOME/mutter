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

#include "backends/native/meta-kms-types.h"
#include "backends/native/meta-kms-update-private.h"

META_EXPORT_TEST
MetaKmsImplDevice * meta_kms_device_get_impl_device (MetaKmsDevice *device);

MetaKmsResourceChanges meta_kms_device_update_states_in_impl (MetaKmsDevice *device,
                                                              uint32_t       crtc_id,
                                                              uint32_t       connector_id);

MetaKmsCrtc * meta_kms_device_find_crtc_in_impl (MetaKmsDevice *device,
                                                 uint32_t       crtc_id);

MetaKmsConnector * meta_kms_device_find_connector_in_impl (MetaKmsDevice *device,
                                                           uint32_t       connector_id);

void meta_kms_device_set_needs_flush (MetaKmsDevice *device,
                                      MetaKmsCrtc   *crtc);
