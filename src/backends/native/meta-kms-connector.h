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

#ifndef META_KMS_CONNECTOR_H
#define META_KMS_CONNECTOR_H

#include <glib-object.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-types.h"
#include "backends/meta-output.h"

#define META_TYPE_KMS_CONNECTOR (meta_kms_connector_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsConnector, meta_kms_connector,
                      META, KMS_CONNECTOR, GObject)

MetaKmsDevice * meta_kms_connector_get_device (MetaKmsConnector *connector);

MetaConnectorType meta_kms_connector_get_connector_type (MetaKmsConnector *connector);

uint32_t meta_kms_connector_get_id (MetaKmsConnector *connector);

const char * meta_kms_connector_get_name (MetaKmsConnector *connector);

#endif /* META_KMS_CONNECTOR_H */
