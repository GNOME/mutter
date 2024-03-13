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

#include "config.h"

#include "backends/meta-output.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-connector-private.h"

#include <errno.h>

#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-mode-private.h"
#include "backends/native/meta-kms-update-private.h"

/* CTA-861.3 HDR Static Metadata Extension, Table 3,
 * Electro-Optical Transfer Function */
typedef enum
{
  HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR = 0,
  HDR_METADATA_EOTF_TRADITIONAL_GAMMA_HDR = 1,
  HDR_METADATA_EOTF_PERCEPTUAL_QUANTIZER = 2,
  HDR_METADATA_EOTF_HYBRID_LOG_GAMMA = 3,
} HdrMetadataEotf;

/* CTA-861.3 HDR Static Metadata Extension, Table 4,
 * Static_Metadata_Descriptor_ID */
typedef enum
{
  HDR_STATIC_METADATA_TYPE_1 = 0,
} HdrStaticMetadataType;

typedef struct _MetaKmsConnectorPropTable
{
  MetaKmsProp props[META_KMS_CONNECTOR_N_PROPS];
  MetaKmsEnum dpms_enum[META_KMS_CONNECTOR_DPMS_N_PROPS];
  MetaKmsEnum underscan_enum[META_KMS_CONNECTOR_UNDERSCAN_N_PROPS];
  MetaKmsEnum privacy_screen_sw_enum[META_KMS_CONNECTOR_PRIVACY_SCREEN_N_PROPS];
  MetaKmsEnum privacy_screen_hw_enum[META_KMS_CONNECTOR_PRIVACY_SCREEN_N_PROPS];
  MetaKmsEnum scaling_mode_enum[META_KMS_CONNECTOR_SCALING_MODE_N_PROPS];
  MetaKmsEnum panel_orientation_enum[META_KMS_CONNECTOR_PANEL_ORIENTATION_N_PROPS];
  MetaKmsEnum colorspace_enum[META_KMS_CONNECTOR_COLORSPACE_N_PROPS];
  MetaKmsEnum broadcast_rgb_enum[META_KMS_CONNECTOR_BROADCAST_RGB_N_PROPS];
} MetaKmsConnectorPropTable;

struct _MetaKmsConnector
{
  GObject parent;

  MetaKmsImplDevice *impl_device;

  uint32_t id;
  uint32_t type;
  uint32_t type_id;
  char *name;

  drmModeConnection connection;
  MetaKmsConnectorState *current_state;

  MetaKmsConnectorPropTable prop_table;

  uint32_t edid_blob_id;
  uint32_t tile_blob_id;

  gboolean fd_held;
};

G_DEFINE_TYPE (MetaKmsConnector, meta_kms_connector, G_TYPE_OBJECT)

typedef enum _MetaKmsPrivacyScreenHwState
{
  META_KMS_PRIVACY_SCREEN_HW_STATE_DISABLED,
  META_KMS_PRIVACY_SCREEN_HW_STATE_ENABLED,
  META_KMS_PRIVACY_SCREEN_HW_STATE_DISABLED_LOCKED,
  META_KMS_PRIVACY_SCREEN_HW_STATE_ENABLED_LOCKED,
} MetaKmsPrivacyScreenHwState;

MetaKmsDevice *
meta_kms_connector_get_device (MetaKmsConnector *connector)
{
  return meta_kms_impl_device_get_device (connector->impl_device);
}

uint32_t
meta_kms_connector_get_prop_id (MetaKmsConnector     *connector,
                                MetaKmsConnectorProp  prop)
{
  return connector->prop_table.props[prop].prop_id;
}

const char *
meta_kms_connector_get_prop_name (MetaKmsConnector     *connector,
                                  MetaKmsConnectorProp  prop)
{
  return connector->prop_table.props[prop].name;
}

uint64_t
meta_kms_connector_get_prop_drm_value (MetaKmsConnector     *connector,
                                       MetaKmsConnectorProp  property,
                                       uint64_t              value)
{
  MetaKmsProp *prop = &connector->prop_table.props[property];
  return meta_kms_prop_convert_value (prop, value);
}

uint32_t
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

MetaKmsMode *
meta_kms_connector_get_preferred_mode (MetaKmsConnector *connector)
{
  const MetaKmsConnectorState *state;
  GList *l;

  state = meta_kms_connector_get_current_state (connector);
  for (l = state->modes; l; l = l->next)
    {
      MetaKmsMode *mode = l->data;
      const drmModeModeInfo *drm_mode;

      drm_mode = meta_kms_mode_get_drm_mode (mode);
      if (drm_mode->type & DRM_MODE_TYPE_PREFERRED)
        return mode;
    }

  return NULL;
}

const MetaKmsConnectorState *
meta_kms_connector_get_current_state (MetaKmsConnector *connector)
{
  return connector->current_state;
}

static gboolean
has_privacy_screen_software_toggle (MetaKmsConnector *connector)
{
  return meta_kms_connector_get_prop_id (connector,
    META_KMS_CONNECTOR_PROP_PRIVACY_SCREEN_SW_STATE) != 0;
}

static void
sync_fd_held (MetaKmsConnector  *connector,
              MetaKmsImplDevice *impl_device)
{
  gboolean should_hold_fd;

  should_hold_fd =
    connector->current_state &&
    connector->current_state->current_crtc_id != 0;

  if (connector->fd_held == should_hold_fd)
    return;

  if (should_hold_fd)
    meta_kms_impl_device_hold_fd (impl_device);
  else
    meta_kms_impl_device_unhold_fd (impl_device);

  connector->fd_held = should_hold_fd;
}

static void
set_panel_orientation (MetaKmsConnectorState *state,
                       MetaKmsProp           *panel_orientation)
{
  MetaMonitorTransform transform;
  MetaKmsConnectorPanelOrientation orientation = panel_orientation->value;

  switch (orientation)
    {
    case META_KMS_CONNECTOR_PANEL_ORIENTATION_UPSIDE_DOWN:
      transform = META_MONITOR_TRANSFORM_180;
      break;
    case META_KMS_CONNECTOR_PANEL_ORIENTATION_LEFT_SIDE_UP:
      transform = META_MONITOR_TRANSFORM_90;
      break;
    case META_KMS_CONNECTOR_PANEL_ORIENTATION_RIGHT_SIDE_UP:
      transform = META_MONITOR_TRANSFORM_270;
      break;
    default:
      transform = META_MONITOR_TRANSFORM_NORMAL;
      break;
    }

  state->panel_orientation_transform = transform;
}

static MetaPrivacyScreenState
privacy_screen_state_hw (MetaKmsConnectorPrivacyScreen privacy_screen)
{
  switch (privacy_screen)
    {
    case META_KMS_PRIVACY_SCREEN_HW_STATE_DISABLED:
      return META_PRIVACY_SCREEN_DISABLED;
    case META_KMS_PRIVACY_SCREEN_HW_STATE_DISABLED_LOCKED:
      return META_PRIVACY_SCREEN_DISABLED | META_PRIVACY_SCREEN_LOCKED;
    case META_KMS_PRIVACY_SCREEN_HW_STATE_ENABLED:
      return META_PRIVACY_SCREEN_ENABLED;
    case META_KMS_PRIVACY_SCREEN_HW_STATE_ENABLED_LOCKED:
      return META_PRIVACY_SCREEN_ENABLED | META_PRIVACY_SCREEN_LOCKED;
    default:
      g_warning ("Unknown privacy screen state: %u", privacy_screen);
      return META_PRIVACY_SCREEN_DISABLED;
    }
}

static MetaOutputColorspace
drm_color_spaces_to_output_color_spaces (uint64_t drm_color_space)
{
  switch (drm_color_space)
    {
    case META_KMS_CONNECTOR_COLORSPACE_DEFAULT:
      return META_OUTPUT_COLORSPACE_DEFAULT;
    case META_KMS_CONNECTOR_COLORSPACE_BT2020_RGB:
      return META_OUTPUT_COLORSPACE_BT2020;
    default:
      return META_OUTPUT_COLORSPACE_UNKNOWN;
    }
}

static uint64_t
supported_drm_color_spaces_to_output_color_spaces (uint64_t drm_support)
{
  uint64_t supported = 0;

  if (drm_support & (1 << META_KMS_CONNECTOR_COLORSPACE_DEFAULT))
    supported |= (1 << META_OUTPUT_COLORSPACE_DEFAULT);
  if (drm_support & (1 << META_KMS_CONNECTOR_COLORSPACE_BT2020_RGB))
    supported |= (1 << META_OUTPUT_COLORSPACE_BT2020);

  return supported;
}

uint64_t
meta_output_color_space_to_drm_color_space (MetaOutputColorspace color_space)
{
  switch (color_space)
    {
    case META_OUTPUT_COLORSPACE_BT2020:
      return META_KMS_CONNECTOR_COLORSPACE_BT2020_RGB;
    case META_OUTPUT_COLORSPACE_UNKNOWN:
    case META_OUTPUT_COLORSPACE_DEFAULT:
    default:
      return META_KMS_CONNECTOR_COLORSPACE_DEFAULT;
    }
}

static MetaOutputRGBRange
drm_broadcast_rgb_to_output_rgb_range (uint64_t drm_broadcast_rgb)
{
  switch (drm_broadcast_rgb)
    {
    case META_KMS_CONNECTOR_BROADCAST_RGB_AUTOMATIC:
      return META_OUTPUT_RGB_RANGE_AUTO;
    case META_KMS_CONNECTOR_BROADCAST_RGB_FULL:
      return META_OUTPUT_RGB_RANGE_FULL;
    case META_KMS_CONNECTOR_BROADCAST_RGB_LIMITED_16_235:
      return META_OUTPUT_RGB_RANGE_LIMITED;
    default:
      return META_OUTPUT_RGB_RANGE_UNKNOWN;
    }
}

static uint64_t
supported_drm_broadcast_rgb_to_output_rgb_range (uint64_t drm_support)
{
  uint64_t supported = 0;

  if (drm_support & (1 << META_KMS_CONNECTOR_BROADCAST_RGB_AUTOMATIC))
    supported |= (1 << META_OUTPUT_RGB_RANGE_AUTO);
  if (drm_support & (1 << META_KMS_CONNECTOR_BROADCAST_RGB_FULL))
    supported |= (1 << META_OUTPUT_RGB_RANGE_FULL);
  if (drm_support & (1 << META_KMS_CONNECTOR_BROADCAST_RGB_LIMITED_16_235))
    supported |= (1 << META_OUTPUT_RGB_RANGE_LIMITED);

  return supported;
}

uint64_t
meta_output_rgb_range_to_drm_broadcast_rgb (MetaOutputRGBRange rgb_range)
{
  switch (rgb_range)
    {
    case META_OUTPUT_RGB_RANGE_FULL:
      return META_KMS_CONNECTOR_BROADCAST_RGB_FULL;
    case META_OUTPUT_RGB_RANGE_LIMITED:
      return META_KMS_CONNECTOR_BROADCAST_RGB_LIMITED_16_235;
    case META_OUTPUT_RGB_RANGE_UNKNOWN:
    case META_OUTPUT_RGB_RANGE_AUTO:
    default:
      return META_KMS_CONNECTOR_BROADCAST_RGB_AUTOMATIC;
    }
}

static void
state_set_properties (MetaKmsConnectorState *state,
                      MetaKmsImplDevice     *impl_device,
                      MetaKmsConnector      *connector,
                      drmModeConnector      *drm_connector)
{
  MetaKmsProp *props = connector->prop_table.props;
  MetaKmsProp *prop;

  prop = &props[META_KMS_CONNECTOR_PROP_SUGGESTED_X];
  if (prop->prop_id)
    state->suggested_x = prop->value;

  prop = &props[META_KMS_CONNECTOR_PROP_SUGGESTED_Y];
  if (prop->prop_id)
    state->suggested_y = prop->value;

  prop = &props[META_KMS_CONNECTOR_PROP_HOTPLUG_MODE_UPDATE];
  if (prop->prop_id)
    state->hotplug_mode_update = prop->value;

  prop = &props[META_KMS_CONNECTOR_PROP_SCALING_MODE];
  if (prop->prop_id)
    state->has_scaling = TRUE;

  prop = &props[META_KMS_CONNECTOR_PROP_PANEL_ORIENTATION];
  if (prop->prop_id)
    set_panel_orientation (state, prop);

  prop = &props[META_KMS_CONNECTOR_PROP_NON_DESKTOP];
  if (prop->prop_id)
    state->non_desktop = prop->value;

  prop = &props[META_KMS_CONNECTOR_PROP_PRIVACY_SCREEN_HW_STATE];
  if (prop->prop_id)
    {
      state->privacy_screen_state = privacy_screen_state_hw (prop->value);

      if (!has_privacy_screen_software_toggle (connector))
        state->privacy_screen_state |= META_PRIVACY_SCREEN_LOCKED;
    }

  prop = &props[META_KMS_CONNECTOR_PROP_MAX_BPC];
  if (prop->prop_id)
    {
      state->max_bpc.supported = TRUE;
      state->max_bpc.value = prop->value;
      state->max_bpc.min_value = prop->range_min;
      state->max_bpc.max_value = prop->range_max;
    }

  prop = &props[META_KMS_CONNECTOR_PROP_COLORSPACE];
  if (prop->prop_id)
    {
      state->colorspace.value =
        drm_color_spaces_to_output_color_spaces (prop->value);
      state->colorspace.supported =
        supported_drm_color_spaces_to_output_color_spaces (prop->supported_variants);
    }

  prop = &props[META_KMS_CONNECTOR_PROP_BROADCAST_RGB];
  if (prop->prop_id)
    {
      state->broadcast_rgb.value =
        drm_broadcast_rgb_to_output_rgb_range (prop->value);
      state->broadcast_rgb.supported =
        supported_drm_broadcast_rgb_to_output_rgb_range (prop->supported_variants);
    }

  prop = &props[META_KMS_CONNECTOR_PROP_UNDERSCAN];
  if (prop->prop_id)
    state->underscan.supported = TRUE;

  prop = &props[META_KMS_CONNECTOR_PROP_VRR_CAPABLE];
  if (prop->prop_id)
    state->vrr_capable = !!prop->value;
}

static CoglSubpixelOrder
drm_subpixel_order_to_cogl_subpixel_order (drmModeSubPixel subpixel)
{
  switch (subpixel)
    {
    case DRM_MODE_SUBPIXEL_NONE:
      return COGL_SUBPIXEL_ORDER_NONE;
      break;
    case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
      return COGL_SUBPIXEL_ORDER_HORIZONTAL_RGB;
      break;
    case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
      return COGL_SUBPIXEL_ORDER_HORIZONTAL_BGR;
      break;
    case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
      return COGL_SUBPIXEL_ORDER_VERTICAL_RGB;
      break;
    case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
      return COGL_SUBPIXEL_ORDER_VERTICAL_BGR;
      break;
    case DRM_MODE_SUBPIXEL_UNKNOWN:
      return COGL_SUBPIXEL_ORDER_UNKNOWN;
    }
  return COGL_SUBPIXEL_ORDER_UNKNOWN;
}

static void
state_set_edid (MetaKmsConnectorState *state,
                MetaKmsConnector      *connector,
                MetaKmsImplDevice     *impl_device,
                uint32_t               blob_id)
{
  int fd;
  drmModePropertyBlobPtr edid_blob;
  GBytes *edid_data;

  fd = meta_kms_impl_device_get_fd (impl_device);
  edid_blob = drmModeGetPropertyBlob (fd, blob_id);
  if (!edid_blob)
    {
      g_warning ("Failed to read EDID of connector %s: %s",
                 connector->name, g_strerror (errno));
      return;
    }

   edid_data = g_bytes_new (edid_blob->data, edid_blob->length);
   drmModeFreePropertyBlob (edid_blob);

   state->edid_data = edid_data;
}

static void
state_set_tile_info (MetaKmsConnectorState *state,
                     MetaKmsConnector      *connector,
                     MetaKmsImplDevice     *impl_device,
                     uint32_t               blob_id)
{
  int fd;
  drmModePropertyBlobPtr tile_blob;

  state->tile_info = (MetaTileInfo) { 0 };

  fd = meta_kms_impl_device_get_fd (impl_device);
  tile_blob = drmModeGetPropertyBlob (fd, blob_id);
  if (!tile_blob)
    {
      g_warning ("Failed to read TILE of connector %s: %s",
                 connector->name, strerror (errno));
      return;
    }

  if (tile_blob->length > 0)
    {
      if (sscanf ((char *) tile_blob->data, "%d:%d:%d:%d:%d:%d:%d:%d",
                  &state->tile_info.group_id,
                  &state->tile_info.flags,
                  &state->tile_info.max_h_tiles,
                  &state->tile_info.max_v_tiles,
                  &state->tile_info.loc_h_tile,
                  &state->tile_info.loc_v_tile,
                  &state->tile_info.tile_w,
                  &state->tile_info.tile_h) != 8)
        {
          g_warning ("Couldn't understand TILE property blob of connector %s",
                     connector->name);
          state->tile_info = (MetaTileInfo) { 0 };
        }
    }

  drmModeFreePropertyBlob (tile_blob);
}

static double
decode_u16_chromaticity (uint16_t value)
{
  /* CTA-861.3 HDR Static Metadata Extension, 3.2.1 Static Metadata Type 1 */
  return MIN (value * 0.00002, 1.0);
}

static double
decode_u16_min_luminance (uint16_t value)
{
  /* CTA-861.3 HDR Static Metadata Extension, 3.2.1 Static Metadata Type 1 */
  return value * 0.0001;
}

gboolean
set_output_hdr_metadata (struct hdr_output_metadata *drm_metadata,
                         MetaOutputHdrMetadata      *metadata)
{
  struct hdr_metadata_infoframe *infoframe;

  if (drm_metadata->metadata_type != HDR_STATIC_METADATA_TYPE_1)
    return FALSE;

  infoframe = &drm_metadata->hdmi_metadata_type1;

  if (infoframe->metadata_type != HDR_STATIC_METADATA_TYPE_1)
    return FALSE;

  switch (infoframe->eotf)
    {
    case HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR:
      metadata->eotf = META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR;
      break;
    case HDR_METADATA_EOTF_TRADITIONAL_GAMMA_HDR:
      metadata->eotf = META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_HDR;
      break;
    case HDR_METADATA_EOTF_PERCEPTUAL_QUANTIZER:
      metadata->eotf = META_OUTPUT_HDR_METADATA_EOTF_PQ;
      break;
    case HDR_METADATA_EOTF_HYBRID_LOG_GAMMA:
      metadata->eotf = META_OUTPUT_HDR_METADATA_EOTF_HLG;
      break;
    }

  /* CTA-861.3 HDR Static Metadata Extension, 3.2.1 Static Metadata Type 1 */
  metadata->mastering_display_primaries[0].x =
    decode_u16_chromaticity (infoframe->display_primaries[0].x);
  metadata->mastering_display_primaries[0].y =
    decode_u16_chromaticity (infoframe->display_primaries[0].y);
  metadata->mastering_display_primaries[1].x =
    decode_u16_chromaticity (infoframe->display_primaries[1].x);
  metadata->mastering_display_primaries[1].y =
    decode_u16_chromaticity (infoframe->display_primaries[1].y);
  metadata->mastering_display_primaries[2].x =
    decode_u16_chromaticity (infoframe->display_primaries[2].x);
  metadata->mastering_display_primaries[2].y =
    decode_u16_chromaticity (infoframe->display_primaries[2].y);
  metadata->mastering_display_white_point.x =
    decode_u16_chromaticity (infoframe->white_point.x);
  metadata->mastering_display_white_point.y =
    decode_u16_chromaticity (infoframe->white_point.y);

  metadata->mastering_display_max_luminance =
    infoframe->max_display_mastering_luminance;
  metadata->mastering_display_min_luminance =
    decode_u16_min_luminance (infoframe->min_display_mastering_luminance);

  metadata->max_cll = infoframe->max_cll;
  metadata->max_fall = infoframe->max_fall;

  return TRUE;
}

static uint16_t
encode_u16_chromaticity (double value)
{
  /* CTA-861.3 HDR Static Metadata Extension, 3.2.1 Static Metadata Type 1 */
  value = MAX (MIN (value, 1.0), 0.0);
  return round (value / 0.00002);
}

static uint16_t
encode_u16_max_luminance (double value)
{
  /* CTA-861.3 HDR Static Metadata Extension, 3.2.1 Static Metadata Type 1 */
  return round (MAX (MIN (value, 65535.0), 0.0));
}

static uint16_t
encode_u16_min_luminance (double value)
{
  /* CTA-861.3 HDR Static Metadata Extension, 3.2.1 Static Metadata Type 1 */
  value = MAX (MIN (value, 6.5535), 0.0);
  return round (value / 0.0001);
}

static uint16_t
encode_u16_max_cll (double value)
{
  /* CTA-861.3 HDR Static Metadata Extension, 3.2.1 Static Metadata Type 1 */
  return round (MAX (MIN (value, 65535.0), 0.0));
}

static uint16_t
encode_u16_max_fall (double value)
{
  /* CTA-861.3 HDR Static Metadata Extension, 3.2.1 Static Metadata Type 1 */
  return round (MAX (MIN (value, 65535.0), 0.0));
}

void
meta_set_drm_hdr_metadata (MetaOutputHdrMetadata      *metadata,
                           struct hdr_output_metadata *drm_metadata)
{
  struct hdr_metadata_infoframe *infoframe = &drm_metadata->hdmi_metadata_type1;

  drm_metadata->metadata_type = HDR_STATIC_METADATA_TYPE_1;
  infoframe->metadata_type = HDR_STATIC_METADATA_TYPE_1;

  switch (metadata->eotf)
    {
    case META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR:
      infoframe->eotf = HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR;
      break;
    case META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_HDR:
      infoframe->eotf = HDR_METADATA_EOTF_TRADITIONAL_GAMMA_HDR;
      break;
    case META_OUTPUT_HDR_METADATA_EOTF_PQ:
      infoframe->eotf = HDR_METADATA_EOTF_PERCEPTUAL_QUANTIZER;
      break;
    case META_OUTPUT_HDR_METADATA_EOTF_HLG:
      infoframe->eotf = HDR_METADATA_EOTF_HYBRID_LOG_GAMMA;
      break;
    }

  infoframe->display_primaries[0].x =
    encode_u16_chromaticity (metadata->mastering_display_primaries[0].x);
  infoframe->display_primaries[0].y =
    encode_u16_chromaticity (metadata->mastering_display_primaries[0].y);
  infoframe->display_primaries[1].x =
    encode_u16_chromaticity (metadata->mastering_display_primaries[1].x);
  infoframe->display_primaries[1].y =
    encode_u16_chromaticity (metadata->mastering_display_primaries[1].y);
  infoframe->display_primaries[2].x =
    encode_u16_chromaticity (metadata->mastering_display_primaries[2].x);
  infoframe->display_primaries[2].y =
    encode_u16_chromaticity (metadata->mastering_display_primaries[2].y);
  infoframe->white_point.x =
    encode_u16_chromaticity (metadata->mastering_display_white_point.x);
  infoframe->white_point.y =
    encode_u16_chromaticity (metadata->mastering_display_white_point.y);

  infoframe->max_display_mastering_luminance =
    encode_u16_max_luminance (metadata->mastering_display_max_luminance);
  infoframe->min_display_mastering_luminance =
    encode_u16_min_luminance (metadata->mastering_display_min_luminance);

  infoframe->max_cll = encode_u16_max_cll (metadata->max_cll);
  infoframe->max_fall = encode_u16_max_fall (metadata->max_fall);
}

static void
state_set_hdr_output_metadata (MetaKmsConnectorState *state,
                               MetaKmsConnector      *connector,
                               MetaKmsImplDevice     *impl_device,
                               uint32_t               blob_id)
{
  int fd;
  drmModePropertyBlobPtr hdr_blob;
  MetaOutputHdrMetadata *metadata = &state->hdr.value;
  struct hdr_output_metadata *drm_metadata;

  state->hdr.supported = TRUE;
  state->hdr.unknown = FALSE;
  metadata->active = TRUE;

  if (!blob_id)
    {
      metadata->active = FALSE;
      return;
    }

  fd = meta_kms_impl_device_get_fd (impl_device);
  hdr_blob = drmModeGetPropertyBlob (fd, blob_id);
  if (!hdr_blob)
    {
      metadata->active = FALSE;
      return;
    }

  if (hdr_blob->length < sizeof (*drm_metadata))
    {
      g_warning ("HDR_OUTPUT_METADATA smaller than expected for type 1");
      state->hdr.unknown = TRUE;
      goto out;
    }

  drm_metadata = hdr_blob->data;
  if (!set_output_hdr_metadata (drm_metadata, metadata))
    {
      state->hdr.unknown = TRUE;
      goto out;
    }

out:
  drmModeFreePropertyBlob (hdr_blob);
}

static void
state_set_blobs (MetaKmsConnectorState *state,
                 MetaKmsConnector      *connector,
                 MetaKmsImplDevice     *impl_device,
                 drmModeConnector      *drm_connector)
{
  MetaKmsProp *props = connector->prop_table.props;
  MetaKmsProp *prop;

  prop = &props[META_KMS_CONNECTOR_PROP_EDID];
  if (prop->prop_id && prop->value)
    state_set_edid (state, connector, impl_device, prop->value);

  prop = &props[META_KMS_CONNECTOR_PROP_TILE];
  if (prop->prop_id && prop->value)
    state_set_tile_info (state, connector, impl_device, prop->value);

  prop = &props[META_KMS_CONNECTOR_PROP_HDR_OUTPUT_METADATA];
  if (prop->prop_id)
    state_set_hdr_output_metadata (state, connector, impl_device, prop->value);
}

static void
state_set_physical_dimensions (MetaKmsConnectorState *state,
                               drmModeConnector      *drm_connector)
{
  state->width_mm = drm_connector->mmWidth;
  state->height_mm = drm_connector->mmHeight;
}

static void
state_set_modes (MetaKmsConnectorState *state,
                 MetaKmsImplDevice     *impl_device,
                 drmModeConnector      *drm_connector)
{
  int i;

  for (i = 0; i < drm_connector->count_modes; i++)
    {
      MetaKmsMode *mode;

      mode = meta_kms_mode_new (impl_device, &drm_connector->modes[i],
                                META_KMS_MODE_FLAG_NONE);
      state->modes = g_list_prepend (state->modes, mode);
    }
  state->modes = g_list_reverse (state->modes);
}

static void
set_encoder_device_idx_bit (uint32_t          *encoder_device_idxs,
                            uint32_t           encoder_id,
                            MetaKmsImplDevice *impl_device,
                            drmModeRes        *drm_resources)
{
  int fd;
  int i;

  fd = meta_kms_impl_device_get_fd (impl_device);

  for (i = 0; i < drm_resources->count_encoders; i++)
    {
      drmModeEncoder *drm_encoder;

      drm_encoder = drmModeGetEncoder (fd, drm_resources->encoders[i]);
      if (!drm_encoder)
        continue;

      if (drm_encoder->encoder_id == encoder_id)
        {
          *encoder_device_idxs |= (1 << i);
          drmModeFreeEncoder (drm_encoder);
          break;
        }

      drmModeFreeEncoder (drm_encoder);
    }
}

static void
state_set_crtc_state (MetaKmsConnectorState *state,
                      drmModeConnector      *drm_connector,
                      MetaKmsImplDevice     *impl_device,
                      drmModeRes            *drm_resources)
{
  int fd;
  int i;
  uint32_t common_possible_crtcs;
  uint32_t common_possible_clones;
  uint32_t encoder_device_idxs;

  fd = meta_kms_impl_device_get_fd (impl_device);

  common_possible_crtcs = UINT32_MAX;
  common_possible_clones = UINT32_MAX;
  encoder_device_idxs = 0;
  for (i = 0; i < drm_connector->count_encoders; i++)
    {
      drmModeEncoder *drm_encoder;

      drm_encoder = drmModeGetEncoder (fd, drm_connector->encoders[i]);
      if (!drm_encoder)
        continue;

      common_possible_crtcs &= drm_encoder->possible_crtcs;
      common_possible_clones &= drm_encoder->possible_clones;

      set_encoder_device_idx_bit (&encoder_device_idxs,
                                  drm_encoder->encoder_id,
                                  impl_device,
                                  drm_resources);

      if (drm_connector->encoder_id == drm_encoder->encoder_id)
        state->current_crtc_id = drm_encoder->crtc_id;

      drmModeFreeEncoder (drm_encoder);
    }

  state->common_possible_crtcs = common_possible_crtcs;
  state->common_possible_clones = common_possible_clones;
  state->encoder_device_idxs = encoder_device_idxs;
}

static MetaKmsConnectorState *
meta_kms_connector_state_new (void)
{
  MetaKmsConnectorState *state;

  state = g_new0 (MetaKmsConnectorState, 1);
  state->suggested_x = -1;
  state->suggested_y = -1;
  state->vrr_capable = FALSE;

  return state;
}

static void
meta_kms_connector_state_free (MetaKmsConnectorState *state)
{
  g_clear_pointer (&state->edid_data, g_bytes_unref);
  g_list_free_full (state->modes, (GDestroyNotify) meta_kms_mode_free);
  g_free (state);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaKmsConnectorState,
                               meta_kms_connector_state_free);

static gboolean
kms_modes_equal (GList *modes,
                 GList *other_modes)
{
  GList *l;

  if (g_list_length (modes) != g_list_length (other_modes))
    return FALSE;

  for (l = modes; l; l = l->next)
    {
      GList *k;
      MetaKmsMode *mode = l->data;

      for (k = other_modes; k; k = k->next)
        {
          MetaKmsMode *other_mode = k->data;

          if (!meta_kms_mode_equal (mode, other_mode))
            return FALSE;
        }
    }

  return TRUE;
}

static gboolean
hdr_primaries_equal (double x1, double x2)
{
  return fabs (x1 - x2) < (0.00002 - DBL_EPSILON);
}

static gboolean
hdr_nits_equal (double x1, double x2)
{
  return fabs (x1 - x2) < (1.0 - DBL_EPSILON);
}

static gboolean
hdr_min_luminance_equal (double x1, double x2)
{
  return fabs (x1 - x2) < (0.0001 - DBL_EPSILON);
}

gboolean
hdr_metadata_equal (MetaOutputHdrMetadata *metadata,
                    MetaOutputHdrMetadata *other_metadata)
{
  if (!metadata->active && !other_metadata->active)
    return TRUE;

  if (metadata->active != other_metadata->active)
    return FALSE;

  if (metadata->eotf != other_metadata->eotf)
      return FALSE;

  if (!hdr_primaries_equal (metadata->mastering_display_primaries[0].x,
                            other_metadata->mastering_display_primaries[0].x) ||
      !hdr_primaries_equal (metadata->mastering_display_primaries[0].y,
                            other_metadata->mastering_display_primaries[0].y) ||
      !hdr_primaries_equal (metadata->mastering_display_primaries[1].x,
                            other_metadata->mastering_display_primaries[1].x) ||
      !hdr_primaries_equal (metadata->mastering_display_primaries[1].y,
                            other_metadata->mastering_display_primaries[1].y) ||
      !hdr_primaries_equal (metadata->mastering_display_primaries[2].x,
                            other_metadata->mastering_display_primaries[2].x) ||
      !hdr_primaries_equal (metadata->mastering_display_primaries[2].y,
                            other_metadata->mastering_display_primaries[2].y) ||
      !hdr_primaries_equal (metadata->mastering_display_white_point.x,
                            other_metadata->mastering_display_white_point.x) ||
      !hdr_primaries_equal (metadata->mastering_display_white_point.y,
                            other_metadata->mastering_display_white_point.y))
    return FALSE;

  if (!hdr_nits_equal (metadata->mastering_display_max_luminance,
                       other_metadata->mastering_display_max_luminance))
    return FALSE;

  if (!hdr_min_luminance_equal (metadata->mastering_display_min_luminance,
                                other_metadata->mastering_display_min_luminance))
    return FALSE;

  if (!hdr_nits_equal (metadata->max_cll, other_metadata->max_cll) ||
      !hdr_nits_equal (metadata->max_fall, other_metadata->max_fall))
    return FALSE;

  return TRUE;
}

static MetaKmsResourceChanges
meta_kms_connector_state_changes (MetaKmsConnectorState *state,
                                  MetaKmsConnectorState *new_state)
{
  if (state->current_crtc_id != new_state->current_crtc_id)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->common_possible_crtcs != new_state->common_possible_crtcs)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->common_possible_clones != new_state->common_possible_clones)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->encoder_device_idxs != new_state->encoder_device_idxs)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->width_mm != new_state->width_mm)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->height_mm != new_state->height_mm)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->has_scaling != new_state->has_scaling)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->non_desktop != new_state->non_desktop)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->subpixel_order != new_state->subpixel_order)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->suggested_x != new_state->suggested_x)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->suggested_y != new_state->suggested_y)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->hotplug_mode_update != new_state->hotplug_mode_update)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->panel_orientation_transform !=
      new_state->panel_orientation_transform)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (!meta_tile_info_equal (&state->tile_info, &new_state->tile_info))
    return META_KMS_RESOURCE_CHANGE_FULL;

  if ((state->edid_data && !new_state->edid_data) || !state->edid_data ||
      !g_bytes_equal (state->edid_data, new_state->edid_data))
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (!kms_modes_equal (state->modes, new_state->modes))
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->max_bpc.supported != new_state->max_bpc.supported ||
      state->max_bpc.value != new_state->max_bpc.value ||
      state->max_bpc.min_value != new_state->max_bpc.min_value ||
      state->max_bpc.max_value != new_state->max_bpc.max_value)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->colorspace.value != new_state->colorspace.value ||
      state->colorspace.supported != new_state->colorspace.supported)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->hdr.supported != new_state->hdr.supported ||
      state->hdr.unknown != new_state->hdr.unknown ||
      !hdr_metadata_equal (&state->hdr.value, &new_state->hdr.value))
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->broadcast_rgb.value != new_state->broadcast_rgb.value ||
      state->broadcast_rgb.supported != new_state->broadcast_rgb.supported)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->vrr_capable != new_state->vrr_capable)
    return META_KMS_RESOURCE_CHANGE_FULL;

  if (state->privacy_screen_state != new_state->privacy_screen_state)
    return META_KMS_RESOURCE_CHANGE_PRIVACY_SCREEN;

  return META_KMS_RESOURCE_CHANGE_NONE;
}

static void
meta_kms_connector_update_state_changes (MetaKmsConnector       *connector,
                                         MetaKmsResourceChanges  changes,
                                         MetaKmsConnectorState  *new_state)
{
  MetaKmsConnectorState *current_state = connector->current_state;

  g_return_if_fail (changes != META_KMS_RESOURCE_CHANGE_FULL);

  if (changes & META_KMS_RESOURCE_CHANGE_PRIVACY_SCREEN)
    current_state->privacy_screen_state = new_state->privacy_screen_state;
}

static MetaKmsResourceChanges
meta_kms_connector_read_state (MetaKmsConnector  *connector,
                               MetaKmsImplDevice *impl_device,
                               drmModeConnector  *drm_connector,
                               drmModeRes        *drm_resources)
{
  g_autoptr (MetaKmsConnectorState) state = NULL;
  g_autoptr (MetaKmsConnectorState) current_state = NULL;
  MetaKmsResourceChanges connector_changes;
  MetaKmsResourceChanges changes;

  current_state = g_steal_pointer (&connector->current_state);
  changes = META_KMS_RESOURCE_CHANGE_NONE;

  meta_kms_impl_device_update_prop_table (impl_device,
                                          drm_connector->props,
                                          drm_connector->prop_values,
                                          drm_connector->count_props,
                                          connector->prop_table.props,
                                          META_KMS_CONNECTOR_N_PROPS);

  if (!drm_connector)
    {
      if (current_state)
        changes = META_KMS_RESOURCE_CHANGE_FULL;
      goto out;
    }

  if (drm_connector->connection != DRM_MODE_CONNECTED)
    {
      if (drm_connector->connection != connector->connection)
        {
          connector->connection = drm_connector->connection;
          changes |= META_KMS_RESOURCE_CHANGE_FULL;
        }

      goto out;
    }

  state = meta_kms_connector_state_new ();

  state_set_blobs (state, connector, impl_device, drm_connector);

  state_set_properties (state, impl_device, connector, drm_connector);

  state->subpixel_order =
    drm_subpixel_order_to_cogl_subpixel_order (drm_connector->subpixel);

  state_set_physical_dimensions (state, drm_connector);

  state_set_modes (state, impl_device, drm_connector);

  state_set_crtc_state (state, drm_connector, impl_device, drm_resources);

  if (drm_connector->connection != connector->connection)
    {
      connector->connection = drm_connector->connection;
      changes |= META_KMS_RESOURCE_CHANGE_FULL;
    }

  if (!current_state)
    connector_changes = META_KMS_RESOURCE_CHANGE_FULL;
  else
    connector_changes = meta_kms_connector_state_changes (current_state, state);

  changes |= connector_changes;

  if (!(changes & META_KMS_RESOURCE_CHANGE_FULL))
    {
      meta_kms_connector_update_state_changes (connector,
                                               connector_changes,
                                               state);
      connector->current_state = g_steal_pointer (&current_state);
    }
  else
    {
      connector->current_state = g_steal_pointer (&state);
    }

out:
  sync_fd_held (connector, impl_device);

  return changes;
}

MetaKmsResourceChanges
meta_kms_connector_update_state_in_impl (MetaKmsConnector *connector,
                                         drmModeRes       *drm_resources,
                                         drmModeConnector *drm_connector)
{
  return meta_kms_connector_read_state (connector,
                                        connector->impl_device,
                                        drm_connector,
                                        drm_resources);
}

void
meta_kms_connector_disable_in_impl (MetaKmsConnector *connector)
{
  MetaKmsConnectorState *current_state;

  current_state = connector->current_state;
  if (!current_state)
    return;

  current_state->current_crtc_id = 0;
}

MetaKmsResourceChanges
meta_kms_connector_predict_state_in_impl (MetaKmsConnector *connector,
                                          MetaKmsUpdate    *update)
{
  MetaKmsConnectorState *current_state;
  GList *mode_sets;
  GList *l;
  MetaKmsResourceChanges changes = META_KMS_RESOURCE_CHANGE_NONE;
  GList *connector_updates;

  current_state = connector->current_state;
  if (!current_state)
    return META_KMS_RESOURCE_CHANGE_NONE;

  mode_sets = meta_kms_update_get_mode_sets (update);
  for (l = mode_sets; l; l = l->next)
    {
      MetaKmsModeSet *mode_set = l->data;
      MetaKmsCrtc *crtc = mode_set->crtc;

      if (current_state->current_crtc_id == meta_kms_crtc_get_id (crtc))
        {
          if (g_list_find (mode_set->connectors, connector))
            break;
          else
            current_state->current_crtc_id = 0;
        }
      else
        {
          if (g_list_find (mode_set->connectors, connector))
            {
              current_state->current_crtc_id = meta_kms_crtc_get_id (crtc);
              break;
            }
        }
    }

  connector_updates = meta_kms_update_get_connector_updates (update);
  for (l = connector_updates; l; l = l->next)
    {
      MetaKmsConnectorUpdate *connector_update = l->data;

      if (connector_update->connector != connector)
        continue;

      if (has_privacy_screen_software_toggle (connector) &&
          connector_update->privacy_screen.has_update &&
          !(current_state->privacy_screen_state &
            META_PRIVACY_SCREEN_LOCKED))
        {
          if (connector_update->privacy_screen.is_enabled)
            {
              if (current_state->privacy_screen_state !=
                  META_PRIVACY_SCREEN_ENABLED)
                changes |= META_KMS_RESOURCE_CHANGE_PRIVACY_SCREEN;

              current_state->privacy_screen_state =
                META_PRIVACY_SCREEN_ENABLED;
            }
          else
            {
              if (current_state->privacy_screen_state !=
                  META_PRIVACY_SCREEN_DISABLED)
                changes |= META_KMS_RESOURCE_CHANGE_PRIVACY_SCREEN;

              current_state->privacy_screen_state =
                META_PRIVACY_SCREEN_DISABLED;
            }
        }

      if (connector_update->colorspace.has_update)
        {
          g_warn_if_fail (current_state->colorspace.supported &
                          (1 << connector_update->colorspace.value));
          current_state->colorspace.value = connector_update->colorspace.value;
        }

      if (connector_update->hdr.has_update)
        {
          g_warn_if_fail (current_state->hdr.supported);
          current_state->hdr.value = connector_update->hdr.value;
        }

      if (connector_update->broadcast_rgb.has_update)
        {
          g_warn_if_fail (current_state->broadcast_rgb.supported &
                          (1 << connector_update->broadcast_rgb.value));
          current_state->broadcast_rgb.value = connector_update->broadcast_rgb.value;
        }
    }

  sync_fd_held (connector, connector->impl_device);

  return changes;
}

static void
init_properties (MetaKmsConnector  *connector,
                 MetaKmsImplDevice *impl_device,
                 drmModeConnector  *drm_connector)
{
  MetaKmsConnectorPropTable *prop_table = &connector->prop_table;

  *prop_table = (MetaKmsConnectorPropTable) {
    .props = {
      [META_KMS_CONNECTOR_PROP_CRTC_ID] =
        {
          .name = "CRTC_ID",
          .type = DRM_MODE_PROP_OBJECT,
        },
      [META_KMS_CONNECTOR_PROP_DPMS] =
        {
          .name = "DPMS",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->dpms_enum,
          .num_enum_values = META_KMS_CONNECTOR_DPMS_N_PROPS,
        },
      [META_KMS_CONNECTOR_PROP_UNDERSCAN] =
        {
          .name = "underscan",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->underscan_enum,
          .num_enum_values = META_KMS_CONNECTOR_UNDERSCAN_N_PROPS,
          .default_value = META_KMS_CONNECTOR_UNDERSCAN_UNKNOWN,
        },
      [META_KMS_CONNECTOR_PROP_UNDERSCAN_HBORDER] =
        {
          .name = "underscan hborder",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CONNECTOR_PROP_UNDERSCAN_VBORDER] =
        {
          .name = "underscan vborder",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CONNECTOR_PROP_PRIVACY_SCREEN_SW_STATE] =
        {
          .name = "privacy-screen sw-state",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->privacy_screen_sw_enum,
          .num_enum_values = META_KMS_CONNECTOR_PRIVACY_SCREEN_N_PROPS,
          .default_value = META_KMS_CONNECTOR_PRIVACY_SCREEN_UNKNOWN,
        },
      [META_KMS_CONNECTOR_PROP_PRIVACY_SCREEN_HW_STATE] =
        {
          .name = "privacy-screen hw-state",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->privacy_screen_hw_enum,
          .num_enum_values = META_KMS_CONNECTOR_PRIVACY_SCREEN_N_PROPS,
          .default_value = META_KMS_CONNECTOR_PRIVACY_SCREEN_UNKNOWN,
        },
      [META_KMS_CONNECTOR_PROP_EDID] =
        {
          .name = "EDID",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_CONNECTOR_PROP_TILE] =
        {
          .name = "TILE",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_CONNECTOR_PROP_SUGGESTED_X] =
        {
          .name = "suggested X",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CONNECTOR_PROP_SUGGESTED_Y] =
        {
          .name = "suggested Y",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CONNECTOR_PROP_HOTPLUG_MODE_UPDATE] =
        {
          .name = "hotplug_mode_update",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CONNECTOR_PROP_SCALING_MODE] =
        {
          .name = "scaling mode",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->scaling_mode_enum,
          .num_enum_values = META_KMS_CONNECTOR_SCALING_MODE_N_PROPS,
          .default_value = META_KMS_CONNECTOR_SCALING_MODE_UNKNOWN,
        },
      [META_KMS_CONNECTOR_PROP_PANEL_ORIENTATION] =
        {
          .name = "panel orientation",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->panel_orientation_enum,
          .num_enum_values = META_KMS_CONNECTOR_PANEL_ORIENTATION_N_PROPS,
          .default_value = META_KMS_CONNECTOR_PANEL_ORIENTATION_UNKNOWN,
        },
      [META_KMS_CONNECTOR_PROP_NON_DESKTOP] =
        {
          .name = "non-desktop",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CONNECTOR_PROP_MAX_BPC] =
        {
          .name = "max bpc",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_CONNECTOR_PROP_COLORSPACE] =
        {
          .name = "Colorspace",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->colorspace_enum,
          .num_enum_values = META_KMS_CONNECTOR_COLORSPACE_N_PROPS,
          .default_value = META_KMS_CONNECTOR_COLORSPACE_UNKNOWN,
        },
      [META_KMS_CONNECTOR_PROP_HDR_OUTPUT_METADATA] =
        {
          .name = "HDR_OUTPUT_METADATA",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_CONNECTOR_PROP_BROADCAST_RGB] =
        {
          .name = "Broadcast RGB",
          .type = DRM_MODE_PROP_ENUM,
          .enum_values = prop_table->broadcast_rgb_enum,
          .num_enum_values = META_KMS_CONNECTOR_BROADCAST_RGB_N_PROPS,
          .default_value = META_KMS_CONNECTOR_BROADCAST_RGB_UNKNOWN,
        },
      [META_KMS_CONNECTOR_PROP_VRR_CAPABLE] =
        {
          .name = "vrr_capable",
          .type = DRM_MODE_PROP_RANGE,
        },
    },
    .dpms_enum = {
      [META_KMS_CONNECTOR_DPMS_ON] =
        {
          .name = "On",
        },
      [META_KMS_CONNECTOR_DPMS_STANDBY] =
        {
          .name = "Standby",
        },
      [META_KMS_CONNECTOR_DPMS_SUSPEND] =
        {
          .name = "Suspend",
        },
      [META_KMS_CONNECTOR_DPMS_OFF] =
        {
          .name = "Off",
        },
    },
    .underscan_enum = {
      [META_KMS_CONNECTOR_UNDERSCAN_OFF] =
        {
          .name = "off",
        },
      [META_KMS_CONNECTOR_UNDERSCAN_ON] =
        {
          .name = "on",
        },
      [META_KMS_CONNECTOR_UNDERSCAN_AUTO] =
        {
          .name = "auto",
        },
    },
    .privacy_screen_sw_enum = {
      [META_KMS_CONNECTOR_PRIVACY_SCREEN_ENABLED] =
        {
          .name = "Enabled",
        },
      [META_KMS_CONNECTOR_PRIVACY_SCREEN_DISABLED] =
        {
          .name = "Disabled",
        },
      [META_KMS_CONNECTOR_PRIVACY_SCREEN_ENABLED_LOCKED] =
        {
          .name = "Enabled-locked",
        },
      [META_KMS_CONNECTOR_PRIVACY_SCREEN_DISABLED_LOCKED] =
        {
          .name = "Disabled-locked",
        },
    },
    .privacy_screen_hw_enum = {
      [META_KMS_CONNECTOR_PRIVACY_SCREEN_ENABLED] =
        {
          .name = "Enabled",
        },
      [META_KMS_CONNECTOR_PRIVACY_SCREEN_DISABLED] =
        {
          .name = "Disabled",
        },
      [META_KMS_CONNECTOR_PRIVACY_SCREEN_ENABLED_LOCKED] =
        {
          .name = "Enabled-locked",
        },
      [META_KMS_CONNECTOR_PRIVACY_SCREEN_DISABLED_LOCKED] =
        {
          .name = "Disabled-locked",
        },
    },
    .scaling_mode_enum = {
      [META_KMS_CONNECTOR_SCALING_MODE_NONE] =
        {
          .name = "None",
        },
      [META_KMS_CONNECTOR_SCALING_MODE_FULL] =
        {
          .name = "Full",
        },
      [META_KMS_CONNECTOR_SCALING_MODE_CENTER] =
        {
          .name = "Center",
        },
      [META_KMS_CONNECTOR_SCALING_MODE_FULL_ASPECT] =
        {
          .name = "Full aspect",
        },
    },
    .panel_orientation_enum = {
      [META_KMS_CONNECTOR_PANEL_ORIENTATION_NORMAL] =
        {
          .name = "Normal",
        },
      [META_KMS_CONNECTOR_PANEL_ORIENTATION_UPSIDE_DOWN] =
        {
          .name = "Upside Down",
        },
      [META_KMS_CONNECTOR_PANEL_ORIENTATION_LEFT_SIDE_UP] =
        {
          .name = "Left Side Up",
        },
      [META_KMS_CONNECTOR_PANEL_ORIENTATION_RIGHT_SIDE_UP] =
        {
          .name = "Right Side Up",
        },
    },
    .colorspace_enum = {
      [META_KMS_CONNECTOR_COLORSPACE_DEFAULT] =
        {
          .name = "Default",
        },
      [META_KMS_CONNECTOR_COLORSPACE_RGB_WIDE_GAMUT_FIXED_POINT] =
        {
          .name = "RGB_Wide_Gamut_Fixed_Point",
        },
      [META_KMS_CONNECTOR_COLORSPACE_RGB_WIDE_GAMUT_FLOATING_POINT] =
        {
          .name = "RGB_Wide_Gamut_Floating_Point",
        },
      [META_KMS_CONNECTOR_COLORSPACE_RGB_OPRGB] =
        {
          .name = "opRGB",
        },
      [META_KMS_CONNECTOR_COLORSPACE_RGB_DCI_P3_RGB_D65] =
        {
          .name = "DCI-P3_RGB_D65",
        },
      [META_KMS_CONNECTOR_COLORSPACE_BT2020_RGB] =
        {
          .name = "BT2020_RGB",
        },
      [META_KMS_CONNECTOR_COLORSPACE_BT601_YCC] =
        {
          .name = "BT601_YCC",
        },
      [META_KMS_CONNECTOR_COLORSPACE_BT709_YCC] =
        {
          .name = "BT709_YCC",
        },
      [META_KMS_CONNECTOR_COLORSPACE_XVYCC_601] =
        {
          .name = "XVYCC_601",
        },
      [META_KMS_CONNECTOR_COLORSPACE_XVYCC_709] =
        {
          .name = "XVYCC_709",
        },
      [META_KMS_CONNECTOR_COLORSPACE_SYCC_601] =
        {
          .name = "SYCC_601",
        },
      [META_KMS_CONNECTOR_COLORSPACE_OPYCC_601] =
        {
          .name = "opYCC_601",
        },
      [META_KMS_CONNECTOR_COLORSPACE_BT2020_CYCC] =
        {
          .name = "BT2020_CYCC",
        },
      [META_KMS_CONNECTOR_COLORSPACE_BT2020_YCC] =
        {
          .name = "BT2020_YCC",
        },
      [META_KMS_CONNECTOR_COLORSPACE_SMPTE_170M_YCC] =
        {
          .name = "SMPTE_170M_YCC",
        },
      [META_KMS_CONNECTOR_COLORSPACE_DCI_P3_RGB_THEATER] =
        {
          .name = "DCI-P3_RGB_Theater",
        },
    },
    .broadcast_rgb_enum = {
      [META_KMS_CONNECTOR_BROADCAST_RGB_AUTOMATIC] =
        {
          .name = "Automatic",
        },
      [META_KMS_CONNECTOR_BROADCAST_RGB_FULL] =
        {
          .name = "Full",
        },
      [META_KMS_CONNECTOR_BROADCAST_RGB_LIMITED_16_235] =
        {
          .name = "Limited 16:235",
        }
    },
  };
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
    "DPI",
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

gboolean
meta_kms_connector_is_same_as (MetaKmsConnector *connector,
                               drmModeConnector *drm_connector)
{
  return (connector->id == drm_connector->connector_id &&
          connector->type == drm_connector->connector_type &&
          connector->type_id == drm_connector->connector_type_id);
}

MetaKmsConnector *
meta_kms_connector_new (MetaKmsImplDevice *impl_device,
                        drmModeConnector  *drm_connector,
                        drmModeRes        *drm_resources)
{
  MetaKmsConnector *connector;

  g_assert (drm_connector);
  connector = g_object_new (META_TYPE_KMS_CONNECTOR, NULL);
  connector->impl_device = impl_device;
  connector->id = drm_connector->connector_id;
  connector->type = drm_connector->connector_type;
  connector->type_id = drm_connector->connector_type_id;
  connector->name = make_connector_name (drm_connector);

  init_properties (connector, impl_device, drm_connector);

  meta_kms_connector_read_state (connector, impl_device,
                                 drm_connector,
                                 drm_resources);

  return connector;
}

static void
meta_kms_connector_finalize (GObject *object)
{
  MetaKmsConnector *connector = META_KMS_CONNECTOR (object);

  if (connector->fd_held)
    meta_kms_impl_device_unhold_fd (connector->impl_device);

  g_clear_pointer (&connector->current_state, meta_kms_connector_state_free);
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
