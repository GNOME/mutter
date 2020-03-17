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

#include <glib-object.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "backends/meta-output.h"
#include "backends/native/meta-kms-types.h"

#define META_TYPE_KMS_CONNECTOR (meta_kms_connector_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaKmsConnector, meta_kms_connector,
                      META, KMS_CONNECTOR, GObject)

typedef struct _MetaKmsConnectorState
{
  uint32_t current_crtc_id;

  uint32_t common_possible_crtcs;
  uint32_t common_possible_clones;
  uint32_t encoder_device_idxs;

  GList *modes;

  uint32_t width_mm;
  uint32_t height_mm;

  MetaTileInfo tile_info;
  GBytes *edid_data;

  gboolean has_scaling;
  gboolean non_desktop;
  MetaPrivacyScreenState privacy_screen_state;

  CoglSubpixelOrder subpixel_order;

  int suggested_x;
  int suggested_y;
  gboolean hotplug_mode_update;

  MetaMonitorTransform panel_orientation_transform;

  struct {
    uint64_t value;
    uint64_t min_value;
    uint64_t max_value;
    gboolean supported;
  } max_bpc;

  struct {
    MetaOutputColorspace value;
    uint64_t supported;
  } colorspace;

  struct {
    MetaOutputHdrMetadata value;
    gboolean supported;
    gboolean unknown;
  } hdr;

  struct {
    MetaOutputRGBRange value;
    uint64_t supported;
  } broadcast_rgb;

  struct {
    gboolean supported;
  } underscan;

  gboolean vrr_capable;
} MetaKmsConnectorState;

META_EXPORT_TEST
MetaKmsDevice * meta_kms_connector_get_device (MetaKmsConnector *connector);

uint32_t meta_kms_connector_get_connector_type (MetaKmsConnector *connector);

uint32_t meta_kms_connector_get_id (MetaKmsConnector *connector);

const char * meta_kms_connector_get_name (MetaKmsConnector *connector);

META_EXPORT_TEST
MetaKmsMode * meta_kms_connector_get_preferred_mode (MetaKmsConnector *connector);

META_EXPORT_TEST
const MetaKmsConnectorState * meta_kms_connector_get_current_state (MetaKmsConnector *connector);
