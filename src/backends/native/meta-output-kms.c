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

#include "meta-default-modes.h"

#define SYNC_TOLERANCE 0.01    /* 1 percent */

typedef struct _MetaOutputKms
{
  MetaOutput parent;

  MetaKmsConnector *kms_connector;

  drmModeConnector *connector;

  uint32_t dpms_prop_id;

  uint32_t underscan_prop_id;
  uint32_t underscan_hborder_prop_id;
  uint32_t underscan_vborder_prop_id;
} MetaOutputKms;

void
meta_output_kms_set_underscan (MetaOutput *output)
{
  MetaOutputKms *output_kms = output->driver_private;
  MetaGpu *gpu = meta_output_get_gpu (output);
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);
  MetaCrtc *crtc;
  int kms_fd;
  uint32_t connector_id;

  if (!output_kms->underscan_prop_id)
    return;

  crtc = meta_output_get_assigned_crtc (output);
  kms_fd = meta_gpu_kms_get_fd (gpu_kms);
  connector_id = output_kms->connector->connector_id;

  if (output->is_underscanning && crtc && crtc->current_mode)
    {
      drmModeObjectSetProperty (kms_fd, connector_id,
                                DRM_MODE_OBJECT_CONNECTOR,
                                output_kms->underscan_prop_id,
                                (uint64_t) 1);

      if (output_kms->underscan_hborder_prop_id)
        {
          uint64_t value;

          value = MIN (128, crtc->current_mode->width * 0.05);
          drmModeObjectSetProperty (kms_fd, connector_id,
                                    DRM_MODE_OBJECT_CONNECTOR,
                                    output_kms->underscan_hborder_prop_id,
                                    value);
        }
      if (output_kms->underscan_vborder_prop_id)
        {
          uint64_t value;

          value = MIN (128, crtc->current_mode->height * 0.05);
          drmModeObjectSetProperty (kms_fd, connector_id,
                                    DRM_MODE_OBJECT_CONNECTOR,
                                    output_kms->underscan_vborder_prop_id,
                                    value);
        }
    }
  else
    {
      drmModeObjectSetProperty (kms_fd, connector_id,
                                DRM_MODE_OBJECT_CONNECTOR,
                                output_kms->underscan_prop_id,
                                (uint64_t) 0);
    }
}

uint32_t
meta_output_kms_get_connector_id (MetaOutput *output)
{
  MetaOutputKms *output_kms = output->driver_private;

  return meta_kms_connector_get_id (output_kms->kms_connector);
}

void
meta_output_kms_set_power_save_mode (MetaOutput *output,
                                     uint64_t    state)
{
  MetaOutputKms *output_kms = output->driver_private;
  MetaGpu *gpu = meta_output_get_gpu (output);
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);

  if (output_kms->dpms_prop_id != 0)
    {
      int fd;

      fd = meta_gpu_kms_get_fd (gpu_kms);
      if (drmModeObjectSetProperty (fd, output_kms->connector->connector_id,
                                    DRM_MODE_OBJECT_CONNECTOR,
                                    output_kms->dpms_prop_id, state) < 0)
        g_warning ("Failed to set power save mode for output %s: %s",
                   output->name, strerror (errno));
    }
}

gboolean
meta_output_kms_can_clone (MetaOutput *output,
                           MetaOutput *other_output)
{
  MetaOutputKms *output_kms = output->driver_private;
  MetaOutputKms *other_output_kms = other_output->driver_private;

  return meta_kms_connector_can_clone (output_kms->kms_connector,
                                       other_output_kms->kms_connector);
}

GBytes *
meta_output_kms_read_edid (MetaOutput *output)
{
  MetaOutputKms *output_kms = output->driver_private;
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
find_connector_properties (MetaGpuKms       *gpu_kms,
                           MetaOutput       *output,
                           drmModeConnector *connector)
{
  MetaOutputKms *output_kms = output->driver_private;
  int fd;
  int i;

  fd = meta_gpu_kms_get_fd (gpu_kms);

  for (i = 0; i < connector->count_props; i++)
    {
      drmModePropertyPtr prop = drmModeGetProperty (fd, connector->props[i]);
      if (!prop)
        continue;

      if ((prop->flags & DRM_MODE_PROP_ENUM) &&
          strcmp (prop->name, "DPMS") == 0)
        output_kms->dpms_prop_id = prop->prop_id;
      else if ((prop->flags & DRM_MODE_PROP_ENUM) &&
               strcmp (prop->name, "underscan") == 0)
        output_kms->underscan_prop_id = prop->prop_id;
      else if ((prop->flags & DRM_MODE_PROP_RANGE) &&
               strcmp (prop->name, "underscan hborder") == 0)
        output_kms->underscan_hborder_prop_id = prop->prop_id;
      else if ((prop->flags & DRM_MODE_PROP_RANGE) &&
               strcmp (prop->name, "underscan vborder") == 0)
        output_kms->underscan_vborder_prop_id = prop->prop_id;

      drmModeFreeProperty (prop);
    }
}

static void
meta_output_destroy_notify (MetaOutput *output)
{
  MetaOutputKms *output_kms;

  output_kms = output->driver_private;

  g_slice_free (MetaOutputKms, output_kms);
}

static void
add_common_modes (MetaOutput *output,
                  MetaGpuKms *gpu_kms)
{
  const drmModeModeInfo *drm_mode;
  MetaCrtcMode *crtc_mode;
  GPtrArray *array;
  float refresh_rate;
  unsigned i;
  unsigned max_hdisplay = 0;
  unsigned max_vdisplay = 0;
  float max_refresh_rate = 0.0;

  for (i = 0; i < output->n_modes; i++)
    {
      drm_mode = output->modes[i]->driver_private;
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

  output->modes = g_renew (MetaCrtcMode *, output->modes,
                           output->n_modes + array->len);
  memcpy (output->modes + output->n_modes, array->pdata,
          array->len * sizeof (MetaCrtcMode *));
  output->n_modes += array->len;

  g_ptr_array_free (array, TRUE);
}

static int
compare_modes (const void *one,
               const void *two)
{
  MetaCrtcMode *a = *(MetaCrtcMode **) one;
  MetaCrtcMode *b = *(MetaCrtcMode **) two;

  if (a->width != b->width)
    return a->width > b->width ? -1 : 1;
  if (a->height != b->height)
    return a->height > b->height ? -1 : 1;
  if (a->refresh_rate != b->refresh_rate)
    return a->refresh_rate > b->refresh_rate ? -1 : 1;

  return g_strcmp0 (b->name, a->name);
}

static gboolean
init_output_modes (MetaOutput  *output,
                   MetaGpuKms  *gpu_kms,
                   GError     **error)
{
  MetaOutputKms *output_kms = output->driver_private;
  const MetaKmsConnectorState *connector_state;
  int i;

  connector_state =
    meta_kms_connector_get_current_state (output_kms->kms_connector);

  output->preferred_mode = NULL;

  output->n_modes = connector_state->n_modes;
  output->modes = g_new0 (MetaCrtcMode *, output->n_modes);
  for (i = 0; i < connector_state->n_modes; i++)
    {
      drmModeModeInfo *drm_mode = &connector_state->modes[i];
      MetaCrtcMode *crtc_mode;

      crtc_mode = meta_gpu_kms_get_mode_from_drm_mode (gpu_kms, drm_mode);
      output->modes[i] = crtc_mode;
      if (drm_mode->type & DRM_MODE_TYPE_PREFERRED)
        output->preferred_mode = output->modes[i];
    }

  /* FIXME: MSC feature bit? */
  /* Presume that if the output supports scaling, then we have
   * a panel fitter capable of adjusting any mode to suit.
   */
  if (connector_state->has_scaling)
    add_common_modes (output, gpu_kms);

  if (!output->modes)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No modes available");
      return FALSE;
    }

  qsort (output->modes, output->n_modes,
         sizeof (MetaCrtcMode *), compare_modes);

  if (!output->preferred_mode)
    output->preferred_mode = output->modes[0];

  return TRUE;
}

MetaOutput *
meta_create_kms_output (MetaGpuKms        *gpu_kms,
                        MetaKmsConnector  *kms_connector,
                        drmModeConnector  *connector,
                        MetaOutput        *old_output,
                        GError           **error)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaOutput *output;
  MetaOutputKms *output_kms;
  const MetaKmsConnectorState *connector_state;
  MetaMonitorTransform panel_orientation_transform;
  uint32_t connector_id;
  GArray *crtcs;
  GList *l;
  uint32_t gpu_id;

  output = g_object_new (META_TYPE_OUTPUT, NULL);

  output_kms = g_slice_new0 (MetaOutputKms);
  output->driver_private = output_kms;
  output->driver_notify = (GDestroyNotify) meta_output_destroy_notify;

  output->gpu = gpu;
  output->name = g_strdup (meta_kms_connector_get_name (kms_connector));

  gpu_id = meta_gpu_kms_get_id (gpu_kms);
  connector_id = meta_kms_connector_get_id (kms_connector);
  output->winsys_id = ((uint64_t) gpu_id << 32) | connector_id;

  output_kms->kms_connector = kms_connector;

  find_connector_properties (gpu_kms, output, connector);

  connector_state = meta_kms_connector_get_current_state (kms_connector);

  panel_orientation_transform = connector_state->panel_orientation_transform;
  if (meta_monitor_transform_is_rotated (panel_orientation_transform))
    {
      output->width_mm = connector_state->height_mm;
      output->height_mm = connector_state->width_mm;
    }
  else
    {
      output->width_mm = connector_state->width_mm;
      output->height_mm = connector_state->height_mm;
    }

  if (!init_output_modes (output, gpu_kms, error))
    {
      g_object_unref (output);
      return NULL;
    }

  crtcs = g_array_new (FALSE, FALSE, sizeof (MetaCrtc *));

  for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
    {
      MetaCrtc *crtc = l->data;
      MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc);
      uint32_t crtc_idx;

      crtc_idx = meta_kms_crtc_get_idx (kms_crtc);
      if (connector_state->common_possible_crtcs & (1 << crtc_idx))
        g_array_append_val (crtcs, crtc);
    }

  output->n_possible_crtcs = crtcs->len;
  output->possible_crtcs = (MetaCrtc **) g_array_free (crtcs, FALSE);

  if (connector_state->current_crtc_id)
    {
      for (l = meta_gpu_get_crtcs (gpu); l; l = l->next)
        {
          MetaCrtc *crtc = l->data;

          if (crtc->crtc_id == connector_state->current_crtc_id)
            {
              meta_output_assign_crtc (output, crtc);
              break;
            }
        }
    }
  else
    {
      meta_output_unassign_crtc (output);
    }

  if (old_output)
    {
      output->is_primary = old_output->is_primary;
      output->is_presentation = old_output->is_presentation;
    }
  else
    {
      output->is_primary = FALSE;
      output->is_presentation = FALSE;
    }

  output->suggested_x = connector_state->suggested_x;
  output->suggested_y = connector_state->suggested_y;
  output->hotplug_mode_update = connector_state->hotplug_mode_update;
  output->supports_underscanning = output_kms->underscan_prop_id != 0;

  meta_output_parse_edid (output, connector_state->edid_data);

  output->connector_type = meta_kms_connector_get_connector_type (kms_connector);

  output->tile_info = connector_state->tile_info;

  /* FIXME: backlight is a very driver specific thing unfortunately,
     every DDX does its own thing, and the dumb KMS API does not include it.

     For example, xf86-video-intel has a list of paths to probe in /sys/class/backlight
     (one for each major HW maker, and then some).
     We can't do the same because we're not root.
     It might be best to leave backlight out of the story and rely on the setuid
     helper in gnome-settings-daemon.
     */
  output->backlight_min = 0;
  output->backlight_max = 0;
  output->backlight = -1;

  return output;
}
