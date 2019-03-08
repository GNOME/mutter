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

#include "config.h"

#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-connector-private.h"

#include <errno.h>

#include "backends/native/meta-kms-impl-device.h"

struct _MetaKmsConnector
{
  GObject parent;

  MetaKmsDevice *device;

  uint32_t id;
  MetaConnectorType type;
  char *name;
};

G_DEFINE_TYPE (MetaKmsConnector, meta_kms_connector, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_connector_get_device (MetaKmsConnector *connector)
{
  return connector->device;
}

MetaConnectorType
meta_kms_connector_get_connector_type (MetaKmsConnector *connector)
{
  return connector->type;
}

uint32_t
meta_kms_connector_get_id (MetaKmsConnector *connector)
{
  return connector->id;
}

const char *
meta_kms_connector_get_name (MetaKmsConnector *connector)
{
  return connector->name;
}

static char *
make_connector_name (drmModeConnector *drm_connector)
{
  static const char * const connector_type_names[] = {
    "None",
    "VGA",
    "DVI-I",
    "DVI-D",
    "DVI-A",
    "Composite",
    "SVIDEO",
    "LVDS",
    "Component",
    "DIN",
    "DP",
    "HDMI",
    "HDMI-B",
    "TV",
    "eDP",
    "Virtual",
    "DSI",
  };

  if (drm_connector->connector_type < G_N_ELEMENTS (connector_type_names))
    return g_strdup_printf ("%s-%d",
                            connector_type_names[drm_connector->connector_type],
                            drm_connector->connector_type_id);
  else
    return g_strdup_printf ("Unknown%d-%d",
                            drm_connector->connector_type,
                            drm_connector->connector_type_id);
}

MetaKmsConnector *
meta_kms_connector_new (MetaKmsImplDevice *impl_device,
                        drmModeConnector  *drm_connector,
                        drmModeRes        *drm_resources)
{
  MetaKmsConnector *connector;

  connector = g_object_new (META_TYPE_KMS_CONNECTOR, NULL);
  connector->device = meta_kms_impl_device_get_device (impl_device);
  connector->id = drm_connector->connector_id;
  connector->type = (MetaConnectorType) drm_connector->connector_type;
  connector->name = make_connector_name (drm_connector);

  return connector;
}

static void
meta_kms_connector_finalize (GObject *object)
{
  MetaKmsConnector *connector = META_KMS_CONNECTOR (object);

  g_free (connector->name);

  G_OBJECT_CLASS (meta_kms_connector_parent_class)->finalize (object);
}

static void
meta_kms_connector_init (MetaKmsConnector *connector)
{
}

static void
meta_kms_connector_class_init (MetaKmsConnectorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_connector_finalize;
}
