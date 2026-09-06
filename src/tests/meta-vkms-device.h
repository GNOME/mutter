/*
 * Copyright (C) 2026 Red Hat Inc.
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

#include <glib.h>

#include "meta/meta-base.h"

#define META_VKMS_MAX_ITEMS 16

typedef enum _MetaVkmsPlaneType
{
  META_VKMS_PLANE_TYPE_PRIMARY,
  META_VKMS_PLANE_TYPE_CURSOR,
  META_VKMS_PLANE_TYPE_OVERLAY,
} MetaVkmsPlaneType;

typedef struct _MetaVkmsPlaneConfig
{
  const char *name;
  MetaVkmsPlaneType type;
  const char *possible_crtcs[META_VKMS_MAX_ITEMS];
} MetaVkmsPlaneConfig;

typedef struct _MetaVkmsCrtcConfig
{
  const char *name;
  gboolean writeback;
} MetaVkmsCrtcConfig;

typedef struct _MetaVkmsEncoderConfig
{
  const char *name;
  const char *possible_crtcs[META_VKMS_MAX_ITEMS];
} MetaVkmsEncoderConfig;

typedef struct _MetaVkmsConnectorConfig
{
  const char *name;
  const char *possible_encoders[META_VKMS_MAX_ITEMS];
} MetaVkmsConnectorConfig;

typedef struct _MetaVkmsDeviceConfig
{
  const char *name;
  MetaVkmsPlaneConfig planes[META_VKMS_MAX_ITEMS];
  MetaVkmsCrtcConfig crtcs[META_VKMS_MAX_ITEMS];
  MetaVkmsEncoderConfig encoders[META_VKMS_MAX_ITEMS];
  MetaVkmsConnectorConfig connectors[META_VKMS_MAX_ITEMS];
} MetaVkmsDeviceConfig;

META_EXPORT const MetaVkmsDeviceConfig meta_vkms_default_device_config;

typedef struct _MetaVkmsDevice MetaVkmsDevice;

META_EXPORT
MetaVkmsDevice * meta_vkms_device_new (const MetaVkmsDeviceConfig *config);

META_EXPORT
gboolean meta_vkms_device_enable (MetaVkmsDevice  *device,
                                  GError         **error);

META_EXPORT
gboolean meta_vkms_device_disable (MetaVkmsDevice  *device,
                                   GError         **error);

META_EXPORT
void meta_vkms_device_free (MetaVkmsDevice *device);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaVkmsDevice, meta_vkms_device_free)
