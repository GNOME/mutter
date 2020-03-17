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

#include "backends/native/meta-kms-connector.h"

typedef enum _MetaKmsConnectorProp
{
  META_KMS_CONNECTOR_PROP_CRTC_ID = 0,
  META_KMS_CONNECTOR_PROP_DPMS,
  META_KMS_CONNECTOR_PROP_UNDERSCAN,
  META_KMS_CONNECTOR_PROP_UNDERSCAN_HBORDER,
  META_KMS_CONNECTOR_PROP_UNDERSCAN_VBORDER,
  META_KMS_CONNECTOR_PROP_PRIVACY_SCREEN_SW_STATE,
  META_KMS_CONNECTOR_PROP_PRIVACY_SCREEN_HW_STATE,
  META_KMS_CONNECTOR_PROP_EDID,
  META_KMS_CONNECTOR_PROP_TILE,
  META_KMS_CONNECTOR_PROP_SUGGESTED_X,
  META_KMS_CONNECTOR_PROP_SUGGESTED_Y,
  META_KMS_CONNECTOR_PROP_HOTPLUG_MODE_UPDATE,
  META_KMS_CONNECTOR_PROP_SCALING_MODE,
  META_KMS_CONNECTOR_PROP_PANEL_ORIENTATION,
  META_KMS_CONNECTOR_PROP_NON_DESKTOP,
  META_KMS_CONNECTOR_PROP_MAX_BPC,
  META_KMS_CONNECTOR_PROP_COLORSPACE,
  META_KMS_CONNECTOR_PROP_HDR_OUTPUT_METADATA,
  META_KMS_CONNECTOR_PROP_BROADCAST_RGB,
  META_KMS_CONNECTOR_PROP_VRR_CAPABLE,
  META_KMS_CONNECTOR_N_PROPS
} MetaKmsConnectorProp;

typedef enum _MetaKmsConnectorDpms
{
  META_KMS_CONNECTOR_DPMS_ON = 0,
  META_KMS_CONNECTOR_DPMS_STANDBY,
  META_KMS_CONNECTOR_DPMS_SUSPEND,
  META_KMS_CONNECTOR_DPMS_OFF,
  META_KMS_CONNECTOR_DPMS_N_PROPS,
  META_KMS_CONNECTOR_DPMS_UNKNOWN,
} MetaKmsConnectorDpms;

typedef enum _MetaKmsConnectorUnderscan
{
  META_KMS_CONNECTOR_UNDERSCAN_OFF = 0,
  META_KMS_CONNECTOR_UNDERSCAN_ON,
  META_KMS_CONNECTOR_UNDERSCAN_AUTO,
  META_KMS_CONNECTOR_UNDERSCAN_N_PROPS,
  META_KMS_CONNECTOR_UNDERSCAN_UNKNOWN,
} MetaKmsConnectorUnderscan;

typedef enum _MetaKmsConnectorPrivacyScreen
{
  META_KMS_CONNECTOR_PRIVACY_SCREEN_ENABLED = 0,
  META_KMS_CONNECTOR_PRIVACY_SCREEN_DISABLED,
  META_KMS_CONNECTOR_PRIVACY_SCREEN_ENABLED_LOCKED,
  META_KMS_CONNECTOR_PRIVACY_SCREEN_DISABLED_LOCKED,
  META_KMS_CONNECTOR_PRIVACY_SCREEN_N_PROPS,
  META_KMS_CONNECTOR_PRIVACY_SCREEN_UNKNOWN,
} MetaKmsConnectorPrivacyScreen;

typedef enum _MetaKmsConnectorScalingMode
{
  META_KMS_CONNECTOR_SCALING_MODE_NONE = 0,
  META_KMS_CONNECTOR_SCALING_MODE_FULL,
  META_KMS_CONNECTOR_SCALING_MODE_CENTER,
  META_KMS_CONNECTOR_SCALING_MODE_FULL_ASPECT,
  META_KMS_CONNECTOR_SCALING_MODE_N_PROPS,
  META_KMS_CONNECTOR_SCALING_MODE_UNKNOWN,
} MetaKmsConnectorScalingMode;

typedef enum _MetaKmsConnectorPanelOrientation
{
  META_KMS_CONNECTOR_PANEL_ORIENTATION_NORMAL = 0,
  META_KMS_CONNECTOR_PANEL_ORIENTATION_UPSIDE_DOWN,
  META_KMS_CONNECTOR_PANEL_ORIENTATION_LEFT_SIDE_UP,
  META_KMS_CONNECTOR_PANEL_ORIENTATION_RIGHT_SIDE_UP,
  META_KMS_CONNECTOR_PANEL_ORIENTATION_N_PROPS,
  META_KMS_CONNECTOR_PANEL_ORIENTATION_UNKNOWN,
} MetaKmsConnectorPanelOrientation;

typedef enum _MetaKmsConnectorColorspace
{
  META_KMS_CONNECTOR_COLORSPACE_DEFAULT = 0,
  META_KMS_CONNECTOR_COLORSPACE_RGB_WIDE_GAMUT_FIXED_POINT,
  META_KMS_CONNECTOR_COLORSPACE_RGB_WIDE_GAMUT_FLOATING_POINT,
  META_KMS_CONNECTOR_COLORSPACE_RGB_OPRGB,
  META_KMS_CONNECTOR_COLORSPACE_RGB_DCI_P3_RGB_D65,
  META_KMS_CONNECTOR_COLORSPACE_BT2020_RGB,
  META_KMS_CONNECTOR_COLORSPACE_BT601_YCC,
  META_KMS_CONNECTOR_COLORSPACE_BT709_YCC,
  META_KMS_CONNECTOR_COLORSPACE_XVYCC_601,
  META_KMS_CONNECTOR_COLORSPACE_XVYCC_709,
  META_KMS_CONNECTOR_COLORSPACE_SYCC_601,
  META_KMS_CONNECTOR_COLORSPACE_OPYCC_601,
  META_KMS_CONNECTOR_COLORSPACE_BT2020_CYCC,
  META_KMS_CONNECTOR_COLORSPACE_BT2020_YCC,
  META_KMS_CONNECTOR_COLORSPACE_SMPTE_170M_YCC,
  META_KMS_CONNECTOR_COLORSPACE_DCI_P3_RGB_THEATER,
  META_KMS_CONNECTOR_COLORSPACE_N_PROPS,
  META_KMS_CONNECTOR_COLORSPACE_UNKNOWN,
} MetaKmsConnectorColorspace;

typedef enum _MetaKmsConnectorBroadcastRGB
{
  META_KMS_CONNECTOR_BROADCAST_RGB_AUTOMATIC = 0,
  META_KMS_CONNECTOR_BROADCAST_RGB_FULL,
  META_KMS_CONNECTOR_BROADCAST_RGB_LIMITED_16_235,
  META_KMS_CONNECTOR_BROADCAST_RGB_N_PROPS,
  META_KMS_CONNECTOR_BROADCAST_RGB_UNKNOWN,
} MetaKmsConnectorBroadcastRGB;

uint32_t meta_kms_connector_get_prop_id (MetaKmsConnector     *connector,
                                         MetaKmsConnectorProp  prop);

const char * meta_kms_connector_get_prop_name (MetaKmsConnector     *connector,
                                               MetaKmsConnectorProp  prop);

uint64_t meta_kms_connector_get_prop_drm_value (MetaKmsConnector     *connector,
                                                MetaKmsConnectorProp  prop,
                                                uint64_t              value);

MetaKmsResourceChanges meta_kms_connector_update_state_in_impl (MetaKmsConnector *connector,
                                                                drmModeRes       *drm_resources,
                                                                drmModeConnector *drm_connector);

void meta_kms_connector_disable_in_impl (MetaKmsConnector *connector);

MetaKmsResourceChanges meta_kms_connector_predict_state_in_impl (MetaKmsConnector *connector,
                                                               MetaKmsUpdate    *update);

MetaKmsConnector * meta_kms_connector_new (MetaKmsImplDevice *impl_device,
                                           drmModeConnector  *drm_connector,
                                           drmModeRes        *drm_resources);

gboolean meta_kms_connector_is_same_as (MetaKmsConnector *connector,
                                        drmModeConnector *drm_connector);

uint64_t meta_output_color_space_to_drm_color_space (MetaOutputColorspace color_space);

uint64_t meta_output_rgb_range_to_drm_broadcast_rgb (MetaOutputRGBRange rgb_range);

META_EXPORT_TEST
void meta_set_drm_hdr_metadata (MetaOutputHdrMetadata      *metadata,
                                struct hdr_output_metadata *drm_metadata);

META_EXPORT_TEST
gboolean set_output_hdr_metadata (struct hdr_output_metadata *drm_metadata,
                                  MetaOutputHdrMetadata      *metadata);

META_EXPORT_TEST
gboolean hdr_metadata_equal (MetaOutputHdrMetadata *metadata,
                             MetaOutputHdrMetadata *other_metadata);
