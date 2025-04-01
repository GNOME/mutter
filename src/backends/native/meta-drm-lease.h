/*
 * Copyright (C) 2023 Red Hat
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

#include "backends/native/meta-backend-native.h"

#define META_TYPE_DRM_LEASE (meta_drm_lease_get_type ())
G_DECLARE_FINAL_TYPE (MetaDrmLease, meta_drm_lease,
                      META, DRM_LEASE, GObject)

#define META_TYPE_DRM_LEASE_MANAGER (meta_drm_lease_manager_get_type ())
G_DECLARE_FINAL_TYPE (MetaDrmLeaseManager, meta_drm_lease_manager,
                      META, DRM_LEASE_MANAGER, GObject)

void meta_drm_lease_manager_pause (MetaDrmLeaseManager *lease_manager);

void meta_drm_lease_manager_resume (MetaDrmLeaseManager *lease_manager);

uint32_t meta_drm_lease_get_id (MetaDrmLease *lease);

int meta_drm_lease_steal_fd (MetaDrmLease *lease);

gboolean meta_drm_lease_is_active (MetaDrmLease *lease);

void meta_drm_lease_revoke (MetaDrmLease *lease);

MetaDrmLease * meta_drm_lease_manager_lease_connectors (MetaDrmLeaseManager  *lease_manager,
                                                        MetaKmsDevice        *kms_device,
                                                        GList                *connectors,
                                                        GError              **error);

GList * meta_drm_lease_manager_get_devices (MetaDrmLeaseManager *lease_manager);

GList * meta_drm_lease_manager_get_connectors (MetaDrmLeaseManager *lease_manager,
                                               MetaKmsDevice       *kms_device);

MetaDrmLease * meta_drm_lease_manager_get_lease_from_connector (MetaDrmLeaseManager *lease_manager,
                                                                MetaKmsConnector    *kms_connector);

MetaDrmLease * meta_drm_lease_manager_get_lease_from_id (MetaDrmLeaseManager *lease_manager,
                                                         uint32_t             lessee_id);
