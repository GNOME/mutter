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

static gboolean
meta_output_kms_is_color_space_supported (MetaOutput           *output,
                                          MetaOutputColorspace  color_space)
{
  MetaOutputKms *output_kms = META_OUTPUT_KMS (output);
  const MetaKmsConnectorState *connector_state;
  const MetaOutputInfo *output_info;

  output_info = meta_output_get_info (output);

  if (!meta_output_info_is_color_space_supported (output_info, color_space))
    {
      meta_topic (META_DEBUG_COLOR,
                  "MetaOutput: Output %s signals that it doesn't support "
                  "Colorspace %s",
                  meta_output_get_name (output),
                  meta_output_colorspace_get_name (color_space));
      return FALSE;
    }

  connector_state =
    meta_kms_connector_get_current_state (output_kms->kms_connector);

  if (!(connector_state->colorspace.supported & (1 << color_space)))
    {
      meta_topic (META_DEBUG_COLOR,
                  "MetaOutput: KMS Connector for output %s doesn't support "
                  "Colorspace %s",
                  meta_output_get_name (output),
                  meta_output_colorspace_get_name (color_space));
      return FALSE;
    }

  return TRUE;
}

static gboolean
meta_output_kms_is_hdr_metadata_supported (MetaOutput *output)
{
  MetaOutputKms *output_kms = META_OUTPUT_KMS (output);
  const MetaKmsConnectorState *connector_state;

  connector_state =
    meta_kms_connector_get_current_state (output_kms->kms_connector);

  return connector_state->hdr.supported;
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
  return meta_kms_connector_can_clone (output_kms->kms_connector,
                                       other_output_kms->kms_connector);
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
                  MetaGpuKms     *gpu_kms)
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

      crtc_mode = meta_gpu_kms_get_mode_from_kms_mode (gpu_kms, fallback_mode);
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
                          MetaKmsConnector            *kms_connector)
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
  add_common_modes (output_info, gpu_kms);
}

static gboolean
init_output_modes (MetaOutputInfo    *output_info,
                   MetaGpuKms        *gpu_kms,
                   MetaKmsConnector  *kms_connector,
                   GError           **error)
{
  const MetaKmsConnectorState *connector_state;
  MetaKmsMode *kms_preferred_mode;
  GList *l;
  int i;

  connector_state = meta_kms_connector_get_current_state (kms_connector);
  kms_preferred_mode = meta_kms_connector_get_preferred_mode (kms_connector);

  output_info->preferred_mode = NULL;

  output_info->n_modes = g_list_length (connector_state->modes);
  output_info->modes = g_new0 (MetaCrtcMode *, output_info->n_modes);
  for (l = connector_state->modes, i = 0; l; l = l->next, i++)
    {
      MetaKmsMode *kms_mode = l->data;
      MetaCrtcMode *crtc_mode;

      crtc_mode = meta_gpu_kms_get_mode_from_kms_mode (gpu_kms, kms_mode);
      output_info->modes[i] = crtc_mode;
      if (kms_mode == kms_preferred_mode)
        output_info->preferred_mode = output_info->modes[i];
    }

  maybe_add_fallback_modes (connector_state, output_info, gpu_kms, kms_connector);
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
  GArray *crtcs;
  GList *l;
  const MetaKmsRange *max_bpc_range;

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

  if (!init_output_modes (output_info, gpu_kms, kms_connector, error))
    return NULL;

  crtcs = g_array_new (FALSE, FALSE, sizeof (MetaCrtc *));

  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtcKms *crtc_kms = META_CRTC_KMS (l->data);
      MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
      uint32_t crtc_idx;

      crtc_idx = meta_kms_crtc_get_idx (kms_crtc);
      if (connector_state->common_possible_crtcs & (1 << crtc_idx))
        g_array_append_val (crtcs, crtc_kms);
    }

  output_info->n_possible_crtcs = crtcs->len;
  output_info->possible_crtcs = (MetaCrtc **) g_array_free (crtcs, FALSE);

  output_info->suggested_x = connector_state->suggested_x;
  output_info->suggested_y = connector_state->suggested_y;
  output_info->hotplug_mode_update = connector_state->hotplug_mode_update;
  output_info->supports_underscanning =
    meta_kms_connector_is_underscanning_supported (kms_connector);

  max_bpc_range = meta_kms_connector_get_max_bpc (kms_connector);
  if (max_bpc_range)
    {
      output_info->max_bpc_min = max_bpc_range->min_value;
      output_info->max_bpc_max = max_bpc_range->max_value;
    }

  if (connector_state->edid_data)
    meta_output_info_parse_edid (output_info, connector_state->edid_data);

  output_info->tile_info = connector_state->tile_info;

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
  output_class->is_color_space_supported =
    meta_output_kms_is_color_space_supported;
  output_class->is_hdr_metadata_supported =
    meta_output_kms_is_hdr_metadata_supported;

  output_native_class->read_edid = meta_output_kms_read_edid;
}
