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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "backends/native/meta-output-kms.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "backends/meta-crtc.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-crtc-mode-kms.h"

#include "meta-default-modes.h"

#define SYNC_TOLERANCE 0.01    /* 1 percent */

typedef struct _MetaOutputKms
{
  MetaOutput parent;

  MetaKmsConnector *kms_connector;
} MetaOutputKms;

G_DEFINE_TYPE (MetaOutputKms, meta_output_kms, META_TYPE_OUTPUT)

MetaKmsConnector *
meta_output_kms_get_kms_connector (MetaOutputKms *output_kms)
{
  return output_kms->kms_connector;
}

void
meta_output_kms_set_underscan (MetaOutputKms *output_kms,
                               MetaKmsUpdate *kms_update)
{
  MetaOutput *output = META_OUTPUT (output_kms);
  const MetaOutputInfo *output_info = meta_output_get_info (output);

  if (!output_info->supports_underscanning)
    return;

  if (meta_output_is_underscanning (output))
    {
      MetaCrtc *crtc;
      const MetaCrtcConfig *crtc_config;
      const MetaCrtcModeInfo *crtc_mode_info;
      uint64_t hborder, vborder;

      crtc = meta_output_get_assigned_crtc (output);
      crtc_config = meta_crtc_get_config (crtc);
      crtc_mode_info = meta_crtc_mode_get_info (crtc_config->mode);

      hborder = MIN (128, (uint64_t) round (crtc_mode_info->width * 0.05));
      vborder = MIN (128, (uint64_t) round (crtc_mode_info->height * 0.05));

      g_debug ("Setting underscan of connector %s to %" G_GUINT64_FORMAT " x %" G_GUINT64_FORMAT,
               meta_kms_connector_get_name (output_kms->kms_connector),
               hborder, vborder);

      meta_kms_connector_set_underscanning (output_kms->kms_connector,
                                            kms_update,
                                            hborder,
                                            vborder);
    }
  else
    {
      g_debug ("Unsetting underscan of connector %s",
               meta_kms_connector_get_name (output_kms->kms_connector));

      meta_kms_connector_unset_underscanning (output_kms->kms_connector,
                                              kms_update);
    }
}

uint32_t
meta_output_kms_get_connector_id (MetaOutputKms *output_kms)
{
  return meta_kms_connector_get_id (output_kms->kms_connector);
}

void
meta_output_kms_set_power_save_mode (MetaOutputKms *output_kms,
                                     uint64_t       dpms_state,
                                     MetaKmsUpdate *kms_update)
{
  g_debug ("Setting DPMS state of connector %s to %" G_GUINT64_FORMAT,
           meta_kms_connector_get_name (output_kms->kms_connector),
           dpms_state);

  meta_kms_connector_update_set_dpms_state (output_kms->kms_connector,
                                            kms_update,
                                            dpms_state);
}

gboolean
meta_output_kms_can_clone (MetaOutputKms *output_kms,
                           MetaOutputKms *other_output_kms)
{
  return meta_kms_connector_can_clone (output_kms->kms_connector,
                                       other_output_kms->kms_connector);
}

GBytes *
meta_output_kms_read_edid (MetaOutputKms *output_kms)
{
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
  const drmModeModeInfo *drm_mode;
  MetaCrtcMode *crtc_mode;
  GPtrArray *array;
  float refresh_rate;
  unsigned i;
  unsigned max_hdisplay = 0;
  unsigned max_vdisplay = 0;
  float max_refresh_rate = 0.0;

  for (i = 0; i < output_info->n_modes; i++)
    {
      MetaCrtcMode *crtc_mode = output_info->modes[i];
      MetaCrtcModeKms *crtc_mode_kms = META_CRTC_MODE_KMS (crtc_mode);

      drm_mode = meta_crtc_mode_kms_get_drm_mode (crtc_mode_kms);
      refresh_rate = meta_calculate_drm_mode_refresh_rate (drm_mode);
      max_hdisplay = MAX (max_hdisplay, drm_mode->hdisplay);
      max_vdisplay = MAX (max_vdisplay, drm_mode->vdisplay);
      max_refresh_rate = MAX (max_refresh_rate, refresh_rate);
    }

  max_refresh_rate = MAX (max_refresh_rate, 60.0);
  max_refresh_rate *= (1 + SYNC_TOLERANCE);

  array = g_ptr_array_new ();
  if (max_hdisplay > max_vdisplay)
    {
      for (i = 0; i < G_N_ELEMENTS (meta_default_landscape_drm_mode_infos); i++)
        {
          drm_mode = &meta_default_landscape_drm_mode_infos[i];
          refresh_rate = meta_calculate_drm_mode_refresh_rate (drm_mode);
          if (drm_mode->hdisplay > max_hdisplay ||
              drm_mode->vdisplay > max_vdisplay ||
              refresh_rate > max_refresh_rate)
            continue;

          crtc_mode = meta_gpu_kms_get_mode_from_drm_mode (gpu_kms,
                                                           drm_mode);
          g_ptr_array_add (array, crtc_mode);
        }
    }
  else
    {
      for (i = 0; i < G_N_ELEMENTS (meta_default_portrait_drm_mode_infos); i++)
        {
          drm_mode = &meta_default_portrait_drm_mode_infos[i];
          refresh_rate = meta_calculate_drm_mode_refresh_rate (drm_mode);
          if (drm_mode->hdisplay > max_hdisplay ||
              drm_mode->vdisplay > max_vdisplay ||
              refresh_rate > max_refresh_rate)
            continue;

          crtc_mode = meta_gpu_kms_get_mode_from_drm_mode (gpu_kms,
                                                           drm_mode);
          g_ptr_array_add (array, crtc_mode);
        }
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
init_output_modes (MetaOutputInfo    *output_info,
                   MetaGpuKms        *gpu_kms,
                   MetaKmsConnector  *kms_connector,
                   GError           **error)
{
  const MetaKmsConnectorState *connector_state;
  int i;

  connector_state = meta_kms_connector_get_current_state (kms_connector);

  output_info->preferred_mode = NULL;

  output_info->n_modes = connector_state->n_modes;
  output_info->modes = g_new0 (MetaCrtcMode *, output_info->n_modes);
  for (i = 0; i < connector_state->n_modes; i++)
    {
      drmModeModeInfo *drm_mode = &connector_state->modes[i];
      MetaCrtcMode *crtc_mode;

      crtc_mode = meta_gpu_kms_get_mode_from_drm_mode (gpu_kms, drm_mode);
      output_info->modes[i] = crtc_mode;
      if (drm_mode->type & DRM_MODE_TYPE_PREFERRED)
        output_info->preferred_mode = output_info->modes[i];
    }

  /* Presume that if the output supports scaling, then we have
   * a panel fitter capable of adjusting any mode to suit.
   */
  if (connector_state->has_scaling)
    add_common_modes (output_info, gpu_kms);

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
  const MetaKmsConnectorState *connector_state;
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

  meta_output_info_parse_edid (output_info, connector_state->edid_data);

  output_info->connector_type = meta_kms_connector_get_connector_type (kms_connector);

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
}
