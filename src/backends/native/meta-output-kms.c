/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013-2017 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-output-kms.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "backends/meta-crtc.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-mode.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-crtc-mode-kms.h"

#define SYNC_TOLERANCE_HZ 0.001

struct _MetaOutputKms
{
  MetaOutputNative parent;

  MetaKmsConnector *kms_connector;
};

G_DEFINE_TYPE (MetaOutputKms, meta_output_kms, META_TYPE_OUTPUT_NATIVE)

MetaKmsConnector *
meta_output_kms_get_kms_connector (MetaOutputKms *output_kms)
{
  return output_kms->kms_connector;
}

static MetaPrivacyScreenState
meta_output_kms_get_privacy_screen_state (MetaOutput *output)
{
  MetaOutputKms *output_kms = META_OUTPUT_KMS (output);
  const MetaKmsConnectorState *connector_state;

  connector_state =
    meta_kms_connector_get_current_state (output_kms->kms_connector);

  return connector_state->privacy_screen_state;
}

uint32_t
meta_output_kms_get_connector_id (MetaOutputKms *output_kms)
{
  return meta_kms_connector_get_id (output_kms->kms_connector);
}

gboolean
meta_output_kms_can_clone (MetaOutputKms *output_kms,
                           MetaOutputKms *other_output_kms)
{
  const MetaKmsConnectorState *state =
    meta_kms_connector_get_current_state (output_kms->kms_connector);
  const MetaKmsConnectorState *other_state =
    meta_kms_connector_get_current_state (other_output_kms->kms_connector);

  if (state->common_possible_clones == 0 ||
      other_state->common_possible_clones == 0)
    return FALSE;

  if (state->encoder_device_idxs != other_state->encoder_device_idxs)
    return FALSE;

  return TRUE;
}

static GBytes *
meta_output_kms_read_edid (MetaOutputNative *output_native)
{
  MetaOutputKms *output_kms = META_OUTPUT_KMS (output_native);
  const MetaKmsConnectorState *connector_state;
  GBytes *edid_data;

  connector_state =
    meta_kms_connector_get_current_state (output_kms->kms_connector);
  edid_data = connector_state->edid_data;
  if (!edid_data)
    return NULL;

  return g_bytes_new_from_bytes (edid_data, 0, g_bytes_get_size (edid_data));
}

static void
add_common_modes (MetaOutputInfo *output_info,
                  MetaGpuKms     *gpu_kms,
                  gboolean        add_vrr_modes)
{
  MetaCrtcMode *crtc_mode;
  GPtrArray *array;
  unsigned i;
  unsigned max_hdisplay = 0;
  unsigned max_vdisplay = 0;
  float max_refresh_rate = 0.0;
  uint32_t max_pixel_clock = 0;
  MetaKmsDevice *kms_device;
  MetaKmsModeFlag flag_filter;
  GList *l;

  for (i = 0; i < output_info->n_modes; i++)
    {
      const MetaCrtcModeInfo *crtc_mode_info =
        meta_crtc_mode_get_info (output_info->modes[i]);

      max_hdisplay = MAX (max_hdisplay, crtc_mode_info->width);
      max_vdisplay = MAX (max_vdisplay, crtc_mode_info->height);
      max_refresh_rate = MAX (max_refresh_rate, crtc_mode_info->refresh_rate);
      max_pixel_clock = MAX (max_pixel_clock, crtc_mode_info->pixel_clock_khz);
    }

  max_refresh_rate = MAX (max_refresh_rate, 60.0);
  max_refresh_rate += SYNC_TOLERANCE_HZ;

  kms_device = meta_gpu_kms_get_kms_device (gpu_kms);

  array = g_ptr_array_new ();

  if (max_hdisplay > max_vdisplay)
    flag_filter = META_KMS_MODE_FLAG_FALLBACK_LANDSCAPE;
  else
    flag_filter = META_KMS_MODE_FLAG_FALLBACK_PORTRAIT;

  for (l = meta_kms_device_get_fallback_modes (kms_device); l; l = l->next)
    {
      MetaKmsMode *fallback_mode = l->data;
      const drmModeModeInfo *drm_mode;
      float refresh_rate;
      gboolean is_duplicate = FALSE;

      if (!(meta_kms_mode_get_flags (fallback_mode) & flag_filter))
        continue;

      drm_mode = meta_kms_mode_get_drm_mode (fallback_mode);
      refresh_rate = meta_calculate_drm_mode_refresh_rate (drm_mode);
      if (drm_mode->hdisplay > max_hdisplay ||
          drm_mode->vdisplay > max_vdisplay ||
          refresh_rate > max_refresh_rate ||
          drm_mode->clock > max_pixel_clock)
        continue;

      for (i = 0; i < output_info->n_modes; i++)
        {
          const MetaCrtcModeInfo *crtc_mode_info =
            meta_crtc_mode_get_info (output_info->modes[i]);

          if (drm_mode->hdisplay == crtc_mode_info->width &&
              drm_mode->vdisplay == crtc_mode_info->height &&
              (fabs (refresh_rate - crtc_mode_info->refresh_rate) <
               SYNC_TOLERANCE_HZ))
            {
              is_duplicate = TRUE;
              break;
            }
        }
      if (is_duplicate)
        continue;

      if (add_vrr_modes)
        {
          crtc_mode = meta_gpu_kms_get_mode_from_kms_mode (gpu_kms,
                                                           fallback_mode,
                                                           META_CRTC_REFRESH_RATE_MODE_VARIABLE);
          g_ptr_array_add (array, crtc_mode);
        }

      crtc_mode = meta_gpu_kms_get_mode_from_kms_mode (gpu_kms,
                                                       fallback_mode,
                                                       META_CRTC_REFRESH_RATE_MODE_FIXED);
      g_ptr_array_add (array, crtc_mode);
    }

  output_info->modes = g_renew (MetaCrtcMode *, output_info->modes,
                                output_info->n_modes + array->len);
  memcpy (output_info->modes + output_info->n_modes, array->pdata,
          array->len * sizeof (MetaCrtcMode *));
  output_info->n_modes += array->len;

  g_ptr_array_free (array, TRUE);
}

static int
compare_modes (const void *one,
               const void *two)
{
  MetaCrtcMode *crtc_mode_one = *(MetaCrtcMode **) one;
  MetaCrtcMode *crtc_mode_two = *(MetaCrtcMode **) two;
  const MetaCrtcModeInfo *crtc_mode_info_one =
    meta_crtc_mode_get_info (crtc_mode_one);
  const MetaCrtcModeInfo *crtc_mode_info_two =
    meta_crtc_mode_get_info (crtc_mode_two);

  if (crtc_mode_info_one->width != crtc_mode_info_two->width)
    return crtc_mode_info_one->width > crtc_mode_info_two->width ? -1 : 1;
  if (crtc_mode_info_one->height != crtc_mode_info_two->height)
    return crtc_mode_info_one->height > crtc_mode_info_two->height ? -1 : 1;
  if (crtc_mode_info_one->refresh_rate != crtc_mode_info_two->refresh_rate)
    return (crtc_mode_info_one->refresh_rate > crtc_mode_info_two->refresh_rate
            ? -1 : 1);
  if (crtc_mode_info_one->refresh_rate_mode != crtc_mode_info_two->refresh_rate_mode)
    return (crtc_mode_info_one->refresh_rate_mode >
            crtc_mode_info_two->refresh_rate_mode) ? -1 : 1;

  return g_strcmp0 (meta_crtc_mode_get_name (crtc_mode_one),
                    meta_crtc_mode_get_name (crtc_mode_two));
}

static gboolean
are_all_modes_equally_sized (MetaOutputInfo *output_info)
{
  const MetaCrtcModeInfo *base =
    meta_crtc_mode_get_info (output_info->modes[0]);
  int i;

  for (i = 1; i < output_info->n_modes; i++)
    {
      const MetaCrtcModeInfo *mode_info =
        meta_crtc_mode_get_info (output_info->modes[i]);

      if (base->width != mode_info->width ||
          base->height != mode_info->height)
        return FALSE;
    }

  return TRUE;
}

static void
maybe_add_fallback_modes (const MetaKmsConnectorState *connector_state,
                          MetaOutputInfo              *output_info,
                          MetaGpuKms                  *gpu_kms,
                          MetaKmsConnector            *kms_connector,
                          gboolean                     add_vrr_modes)
{
  if (!connector_state->modes)
    return;

  if (!connector_state->has_scaling)
    return;

  if (output_info->connector_type == DRM_MODE_CONNECTOR_eDP &&
      !are_all_modes_equally_sized (output_info))
    return;

  meta_topic (META_DEBUG_KMS, "Adding common modes to connector %u on %s",
              meta_kms_connector_get_id (kms_connector),
              meta_gpu_kms_get_file_path (gpu_kms));
  add_common_modes (output_info, gpu_kms, add_vrr_modes);
}

static gboolean
init_output_modes (MetaOutputInfo    *output_info,
                   MetaGpuKms        *gpu_kms,
                   MetaKmsConnector  *kms_connector,
                   GError           **error)
{
  const MetaKmsConnectorState *connector_state;
  MetaKmsMode *kms_preferred_mode;
  gboolean add_vrr_modes;
  GList *l;
  int i;

  connector_state = meta_kms_connector_get_current_state (kms_connector);
  kms_preferred_mode = meta_kms_connector_get_preferred_mode (kms_connector);

  output_info->preferred_mode = NULL;

  add_vrr_modes = output_info->supports_vrr;

  if (add_vrr_modes)
    output_info->n_modes = g_list_length (connector_state->modes) * 2;
  else
    output_info->n_modes = g_list_length (connector_state->modes);

  output_info->modes = g_new0 (MetaCrtcMode *, output_info->n_modes);

  for (l = connector_state->modes, i = 0; l; l = l->next)
    {
      MetaKmsMode *kms_mode = l->data;
      MetaCrtcMode *crtc_mode;

      if (add_vrr_modes)
        {
          crtc_mode =
            meta_gpu_kms_get_mode_from_kms_mode (gpu_kms,
                                                 kms_mode,
                                                 META_CRTC_REFRESH_RATE_MODE_VARIABLE);
          output_info->modes[i++] = crtc_mode;
          if (!output_info->preferred_mode && kms_mode == kms_preferred_mode)
            output_info->preferred_mode = crtc_mode;
        }

      crtc_mode = meta_gpu_kms_get_mode_from_kms_mode (gpu_kms,
                                                       kms_mode,
                                                       META_CRTC_REFRESH_RATE_MODE_FIXED);
      output_info->modes[i++] = crtc_mode;
      if (!output_info->preferred_mode && kms_mode == kms_preferred_mode)
        output_info->preferred_mode = crtc_mode;
    }

  maybe_add_fallback_modes (connector_state,
                            output_info,
                            gpu_kms,
                            kms_connector,
                            add_vrr_modes);
  if (!output_info->modes)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No modes available");
      return FALSE;
    }

  qsort (output_info->modes, output_info->n_modes,
         sizeof (MetaCrtcMode *), compare_modes);

  if (!output_info->preferred_mode)
    output_info->preferred_mode = output_info->modes[0];

  return TRUE;
}

static MetaConnectorType
meta_kms_connector_type_from_drm (uint32_t drm_connector_type)
{
  g_warn_if_fail (drm_connector_type < META_CONNECTOR_TYPE_META);

  return (MetaConnectorType) drm_connector_type;
}

MetaOutputKms *
meta_output_kms_new (MetaGpuKms        *gpu_kms,
                     MetaKmsConnector  *kms_connector,
                     MetaOutput        *old_output,
                     GError           **error)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  uint32_t connector_id;
  uint32_t gpu_id;
  g_autoptr (MetaOutputInfo) output_info = NULL;
  MetaOutput *output;
  MetaOutputKms *output_kms;
  uint32_t drm_connector_type;
  const MetaKmsConnectorState *connector_state;
  const MetaKmsCrtcState *crtc_state;
  GArray *crtcs;
  GList *l;

  gpu_id = meta_gpu_kms_get_id (gpu_kms);
  connector_id = meta_kms_connector_get_id (kms_connector);

  output_info = meta_output_info_new ();
  output_info->name = g_strdup (meta_kms_connector_get_name (kms_connector));

  connector_state = meta_kms_connector_get_current_state (kms_connector);

  output_info->panel_orientation_transform =
    connector_state->panel_orientation_transform;
  if (meta_monitor_transform_is_rotated (output_info->panel_orientation_transform))
    {
      output_info->width_mm = connector_state->height_mm;
      output_info->height_mm = connector_state->width_mm;
    }
  else
    {
      output_info->width_mm = connector_state->width_mm;
      output_info->height_mm = connector_state->height_mm;
    }

  drm_connector_type = meta_kms_connector_get_connector_type (kms_connector);
  output_info->connector_type =
    meta_kms_connector_type_from_drm (drm_connector_type);

  output_info->supports_vrr = connector_state->vrr_capable &&
                              !meta_gpu_kms_disable_vrr (gpu_kms);

  crtcs = g_array_new (FALSE, FALSE, sizeof (MetaCrtc *));

  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtcKms *crtc_kms = META_CRTC_KMS (l->data);
      MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
      uint32_t crtc_idx;

      crtc_idx = meta_kms_crtc_get_idx (kms_crtc);
      if (connector_state->common_possible_crtcs & (1 << crtc_idx))
        {
          g_array_append_val (crtcs, crtc_kms);

          crtc_state = meta_kms_crtc_get_current_state (kms_crtc);
          if (!crtc_state->vrr.supported)
            {
              meta_topic (META_DEBUG_KMS,
                          "Output is VRR capable, but a possible CRTC for the "
                          "output does not support VRR. Disabling support for "
                          "VRR on the output.");
              output_info->supports_vrr = FALSE;
            }
        }
    }

  if (!init_output_modes (output_info, gpu_kms, kms_connector, error))
    return NULL;

  output_info->n_possible_crtcs = crtcs->len;
  output_info->possible_crtcs = (MetaCrtc **) g_array_free (crtcs, FALSE);

  output_info->suggested_x = connector_state->suggested_x;
  output_info->suggested_y = connector_state->suggested_y;
  output_info->hotplug_mode_update = connector_state->hotplug_mode_update;
  output_info->supports_underscanning = connector_state->underscan.supported;

  if (connector_state->max_bpc.supported)
    {
      output_info->max_bpc_min = connector_state->max_bpc.min_value;
      output_info->max_bpc_max = connector_state->max_bpc.max_value;
    }

  if (connector_state->edid_data)
    meta_output_info_parse_edid (output_info, connector_state->edid_data);

  output_info->tile_info = connector_state->tile_info;

  if (output_info->edid_info)
    {
      MetaEdidColorimetry edid_colorimetry =
        output_info->edid_info->colorimetry;
      uint64_t connector_colorimetry = connector_state->colorspace.supported;

      if (connector_colorimetry & (1 << META_OUTPUT_COLORSPACE_DEFAULT))
        output_info->supported_color_spaces |= (1 << META_OUTPUT_COLORSPACE_DEFAULT);

      if ((edid_colorimetry & META_EDID_COLORIMETRY_BT2020RGB) &&
          (connector_colorimetry & (1 << META_OUTPUT_COLORSPACE_BT2020)))
        output_info->supported_color_spaces |= (1 << META_OUTPUT_COLORSPACE_BT2020);
    }

  if (connector_state->hdr.supported &&
      output_info->edid_info &&
      (output_info->edid_info->hdr_static_metadata.sm &
       META_EDID_STATIC_METADATA_TYPE1))
    {
      MetaEdidTransferFunction edid_tf =
        output_info->edid_info->hdr_static_metadata.tf;

      if (edid_tf & META_EDID_TF_TRADITIONAL_GAMMA_SDR)
        {
          output_info->supported_hdr_eotfs |=
            (1 << META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR);
        }
      if (edid_tf & META_EDID_TF_TRADITIONAL_GAMMA_HDR)
        {
          output_info->supported_hdr_eotfs |=
            (1 << META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_HDR);
        }
      if (edid_tf & META_EDID_TF_PQ)
        {
          output_info->supported_hdr_eotfs |=
            (1 << META_OUTPUT_HDR_METADATA_EOTF_PQ);
        }
      if (edid_tf & META_EDID_TF_HLG)
        {
          output_info->supported_hdr_eotfs |=
            (1 << META_OUTPUT_HDR_METADATA_EOTF_HLG);
        }
    }

  output_info->supports_privacy_screen =
    (connector_state->privacy_screen_state != META_PRIVACY_SCREEN_UNAVAILABLE);

  output_info->supported_rgb_ranges = connector_state->broadcast_rgb.supported;

  output = g_object_new (META_TYPE_OUTPUT_KMS,
                         "id", ((uint64_t) gpu_id << 32) | connector_id,
                         "gpu", gpu,
                         "info", output_info,
                         NULL);
  output_kms = META_OUTPUT_KMS (output);
  output_kms->kms_connector = kms_connector;

  if (connector_state->current_crtc_id)
    {
      for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
        {
          MetaCrtc *crtc = l->data;

          if (meta_crtc_get_id (crtc) == connector_state->current_crtc_id)
            {
              MetaOutputAssignment output_assignment;

              if (old_output)
                {
                  output_assignment = (MetaOutputAssignment) {
                    .is_primary = meta_output_is_primary (old_output),
                    .is_presentation = meta_output_is_presentation (old_output),
                  };
                }
              else
                {
                  output_assignment = (MetaOutputAssignment) {
                    .is_primary = FALSE,
                    .is_presentation = FALSE,
                  };
                }
              meta_output_assign_crtc (output, crtc, &output_assignment);
              break;
            }
        }
    }
  else
    {
      meta_output_unassign_crtc (output);
    }

  return output_kms;
}

static void
meta_output_kms_init (MetaOutputKms *output_kms)
{
}

static void
meta_output_kms_class_init (MetaOutputKmsClass *klass)
{
  MetaOutputNativeClass *output_native_class = META_OUTPUT_NATIVE_CLASS (klass);
  MetaOutputClass *output_class = META_OUTPUT_CLASS (klass);

  output_class->get_privacy_screen_state =
    meta_output_kms_get_privacy_screen_state;

  output_native_class->read_edid = meta_output_kms_read_edid;
}
