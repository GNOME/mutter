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

#ifndef META_KMS_CONNECTOR_PRIVATE_H
#define META_KMS_CONNECTOR_PRIVATE_H

#include "backends/native/meta-kms-connector.h"

typedef enum _MetaKmsConnectorProp
{
  META_KMS_CONNECTOR_PROP_CRTC_ID = 0,
  META_KMS_CONNECTOR_PROP_DPMS,
  META_KMS_CONNECTOR_PROP_UNDERSCAN,
  META_KMS_CONNECTOR_PROP_UNDERSCAN_HBORDER,
  META_KMS_CONNECTOR_PROP_UNDERSCAN_VBORDER,
  META_KMS_CONNECTOR_N_PROPS
} MetaKmsConnectorProp;

uint32_t meta_kms_connector_get_prop_id (MetaKmsConnector     *connector,
                                         MetaKmsConnectorProp  prop);

const char * meta_kms_connector_get_prop_name (MetaKmsConnector     *connector,
                                               MetaKmsConnectorProp  prop);

void meta_kms_connector_update_state (MetaKmsConnector *connector,
                                      drmModeRes       *drm_resources);

void meta_kms_connector_predict_state (MetaKmsConnector *connector,
                                       MetaKmsUpdate    *update);

MetaKmsConnector * meta_kms_connector_new (MetaKmsImplDevice *impl_device,
                                           drmModeConnector  *drm_connector,
                                           drmModeRes        *drm_resources);

gboolean meta_kms_connector_is_same_as (MetaKmsConnector *connector,
                                        drmModeConnector *drm_connector);

#endif /* META_KMS_CONNECTOR_PRIVATE_H */
