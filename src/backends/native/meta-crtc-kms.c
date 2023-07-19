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

#include "backends/native/meta-crtc-kms.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/native/meta-crtc-mode-kms.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-output-kms.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-mode.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-monitor-manager-native.h"

enum
{
  GAMMA_LUT_CHANGED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

#define ALL_TRANSFORMS_MASK ((1 << META_MONITOR_N_TRANSFORMS) - 1)

struct _MetaCrtcKms
{
  MetaCrtcNative parent;

  MetaKmsCrtc *kms_crtc;

  MetaKmsPlane *primary_plane;
};

static GQuark kms_crtc_crtc_kms_quark;

G_DEFINE_TYPE (MetaCrtcKms, meta_crtc_kms, META_TYPE_CRTC_NATIVE)

static MetaMonitorManagerNative *
monitor_manager_from_crtc (MetaCrtc *crtc)
{
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  return META_MONITOR_MANAGER_NATIVE (monitor_manager);
}

static size_t
meta_crtc_kms_get_gamma_lut_size (MetaCrtc *crtc)
{
  MetaKmsCrtc *kms_crtc;
  const MetaKmsCrtcState *crtc_state;

  kms_crtc = meta_crtc_kms_get_kms_crtc (META_CRTC_KMS (crtc));
  crtc_state = meta_kms_crtc_get_current_state (kms_crtc);

  return crtc_state->gamma.size;
}

const MetaGammaLut *
meta_crtc_kms_peek_gamma_lut (MetaCrtcKms *crtc_kms)
{
  MetaMonitorManagerNative *monitor_manager_native =
    monitor_manager_from_crtc (META_CRTC (crtc_kms));

  return meta_monitor_manager_native_get_cached_crtc_gamma (monitor_manager_native,
                                                            crtc_kms);
}

static MetaGammaLut *
meta_crtc_kms_get_gamma_lut (MetaCrtc *crtc)
{
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaMonitorManagerNative *monitor_manager_native =
    monitor_manager_from_crtc (crtc);
  const MetaKmsCrtcState *crtc_state;
  MetaGammaLut *gamma;

  gamma =
    meta_monitor_manager_native_get_cached_crtc_gamma (monitor_manager_native,
                                                       crtc_kms);
  if (!gamma)
    {
      crtc_state = meta_kms_crtc_get_current_state (kms_crtc);
      gamma = crtc_state->gamma.value;
    }

  if (!gamma)
    return meta_gamma_lut_new (0, NULL, NULL, NULL);

  return meta_gamma_lut_copy (gamma);
}

static char *
generate_gamma_ramp_string (const MetaGammaLut *lut)
{
  GString *string;
  int color;

  string = g_string_new ("[");
  for (color = 0; color < 3; color++)
    {
      uint16_t * const *color_ptr = NULL;
      char color_char;
      size_t i;

      switch (color)
        {
        case 0:
          color_ptr = &lut->red;
          color_char = 'r';
          break;
        case 1:
          color_ptr = &lut->green;
          color_char = 'g';
          break;
        case 2:
          color_ptr = &lut->blue;
          color_char = 'b';
          break;
        }

      g_assert (color_ptr);
      g_string_append_printf (string, " %c: ", color_char);
      for (i = 0; i < MIN (4, lut->size); i++)
        {
          int j;

          if (lut->size > 4)
            {
              if (i == 2)
                g_string_append (string, ",...");

              if (i >= 2)
                j = i + (lut->size - 4);
              else
                j = i;
            }
          else
            {
              j = i;
            }
          g_string_append_printf (string, "%s%hu",
                                  j == 0 ? "" : ",",
                                  (*color_ptr)[i]);
        }
    }

  g_string_append (string, " ]");

  return g_string_free (string, FALSE);
}

static void
meta_crtc_kms_set_gamma_lut (MetaCrtc           *crtc,
                             const MetaGammaLut *lut)
{
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc);
  MetaBackend *backend = meta_gpu_get_backend (meta_crtc_get_gpu (crtc));
  MetaMonitorManagerNative *monitor_manager_native =
    monitor_manager_from_crtc (crtc);
  ClutterActor *stage = meta_backend_get_stage (backend);
  g_autofree char *gamma_ramp_string = NULL;
  MetaGammaLut *new_gamma;

  gamma_ramp_string = generate_gamma_ramp_string (lut);
  meta_topic (META_DEBUG_COLOR,
              "Setting CRTC (%" G_GUINT64_FORMAT ") gamma to %s",
              meta_crtc_get_id (crtc), gamma_ramp_string);

  new_gamma = meta_gamma_lut_copy (lut);
  if (!new_gamma)
    new_gamma = meta_gamma_lut_new (0, NULL, NULL, NULL);

  meta_monitor_manager_native_update_cached_crtc_gamma (monitor_manager_native,
                                                        crtc_kms,
                                                        new_gamma);

  g_signal_emit (crtc_kms, signals[GAMMA_LUT_CHANGED], 0);
  clutter_stage_schedule_update (CLUTTER_STAGE (stage));
}

static gboolean
is_transform_handled (MetaCrtcKms          *crtc_kms,
                      MetaMonitorTransform  transform)
{
  if (!crtc_kms->primary_plane)
    return FALSE;

  return meta_kms_plane_is_transform_handled (crtc_kms->primary_plane,
                                              transform);
}

static gboolean
meta_crtc_kms_is_transform_handled (MetaCrtcNative       *crtc_native,
                                    MetaMonitorTransform  transform)
{
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc_native);

  return is_transform_handled (crtc_kms, transform);
}

static gboolean
meta_crtc_kms_is_hw_cursor_supported (MetaCrtcNative *crtc_native)
{
  MetaCrtcKms *crtc_kms = META_CRTC_KMS (crtc_native);
  MetaKmsCrtc *kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);

  return !!meta_kms_device_get_cursor_plane_for (kms_device, kms_crtc);
}

void
meta_crtc_kms_apply_transform (MetaCrtcKms            *crtc_kms,
                               MetaKmsPlaneAssignment *kms_plane_assignment)
{
  MetaCrtc *crtc = META_CRTC (crtc_kms);
  const MetaCrtcConfig *crtc_config;
  MetaMonitorTransform hw_transform;

  crtc_config = meta_crtc_get_config (crtc);

  hw_transform = crtc_config->transform;
  if (!is_transform_handled (crtc_kms, hw_transform))
    hw_transform = META_MONITOR_TRANSFORM_NORMAL;
  if (!is_transform_handled (crtc_kms, hw_transform))
    return;

  meta_kms_plane_update_set_rotation (crtc_kms->primary_plane,
                                      kms_plane_assignment,
                                      hw_transform);
}

MetaKmsPlaneAssignment *
meta_crtc_kms_assign_primary_plane (MetaCrtcKms   *crtc_kms,
                                    MetaDrmBuffer *buffer,
                                    MetaKmsUpdate *kms_update)
{
  MetaCrtc *crtc = META_CRTC (crtc_kms);
  const MetaCrtcConfig *crtc_config;
  const MetaCrtcModeInfo *crtc_mode_info;
  MetaFixed16Rectangle src_rect;
  MtkRectangle dst_rect;
  MetaKmsAssignPlaneFlag flags;
  MetaKmsCrtc *kms_crtc;
  MetaKmsDevice *kms_device;
  MetaKmsPlane *primary_kms_plane;
  MetaKmsPlaneAssignment *plane_assignment;

  crtc_config = meta_crtc_get_config (crtc);
  crtc_mode_info = meta_crtc_mode_get_info (crtc_config->mode);

  src_rect = (MetaFixed16Rectangle) {
    .x = meta_fixed_16_from_int (0),
    .y = meta_fixed_16_from_int (0),
    .width = meta_fixed_16_from_int (crtc_mode_info->width),
    .height = meta_fixed_16_from_int (crtc_mode_info->height),
  };
  dst_rect = (MtkRectangle) {
    .x = 0,
    .y = 0,
    .width = crtc_mode_info->width,
    .height = crtc_mode_info->height,
  };

  flags = META_KMS_ASSIGN_PLANE_FLAG_NONE;

  kms_crtc = meta_crtc_kms_get_kms_crtc (crtc_kms);
  kms_device = meta_kms_crtc_get_device (kms_crtc);
  primary_kms_plane = meta_kms_device_get_primary_plane_for (kms_device,
                                                             kms_crtc);
  plane_assignment = meta_kms_update_assign_plane (kms_update,
                                                   kms_crtc,
                                                   primary_kms_plane,
                                                   buffer,
                                                   src_rect,
                                                   dst_rect,
                                                   flags);
  meta_crtc_kms_apply_transform (crtc_kms, plane_assignment);

  return plane_assignment;
}

static GList *
generate_crtc_connector_list (MetaGpu  *gpu,
                              MetaCrtc *crtc)
{
  GList *connectors = NULL;
  GList *l;

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;
      MetaCrtc *assigned_crtc;

      assigned_crtc = meta_output_get_assigned_crtc (output);
      if (assigned_crtc == crtc)
        {
          MetaKmsConnector *kms_connector =
            meta_output_kms_get_kms_connector (META_OUTPUT_KMS (output));

          connectors = g_list_prepend (connectors, kms_connector);
        }
    }

  return connectors;
}

void
meta_crtc_kms_set_mode (MetaCrtcKms   *crtc_kms,
                        MetaKmsUpdate *kms_update)
{
  MetaCrtc *crtc = META_CRTC (crtc_kms);
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  GList *connectors;
  MetaKmsMode *kms_mode;

  connectors = generate_crtc_connector_list (gpu, crtc);

  if (connectors)
    {
      const MetaCrtcConfig *crtc_config = meta_crtc_get_config (crtc);
      MetaCrtcModeKms *crtc_mode_kms = META_CRTC_MODE_KMS (crtc_config->mode);

      kms_mode = meta_crtc_mode_kms_get_kms_mode (crtc_mode_kms);

      meta_topic (META_DEBUG_KMS,
                  "Setting CRTC (%" G_GUINT64_FORMAT ") mode to %s",
                  meta_crtc_get_id (crtc), meta_kms_mode_get_name (kms_mode));
    }
  else
    {
      kms_mode = NULL;

      meta_topic (META_DEBUG_KMS,
                  "Unsetting CRTC (%" G_GUINT64_FORMAT ") mode",
                  meta_crtc_get_id (crtc));
    }

  meta_kms_update_mode_set (kms_update,
                            meta_crtc_kms_get_kms_crtc (crtc_kms),
                            g_steal_pointer (&connectors),
                            kms_mode);
}

MetaKmsCrtc *
meta_crtc_kms_get_kms_crtc (MetaCrtcKms *crtc_kms)
{
  return crtc_kms->kms_crtc;
}

/**
 * meta_crtc_kms_get_modifiers:
 * @crtc_kms: a #MetaCrtc object that has to be a #MetaCrtcKms
 * @format: a DRM pixel format
 *
 * Returns a pointer to a #GArray containing all the supported
 * modifiers for the given DRM pixel format on the CRTC's primary
 * plane. The array element type is uint64_t.
 *
 * The caller must not modify or destroy the array or its contents.
 *
 * Returns NULL if the modifiers are not known or the format is not
 * supported.
 */
GArray *
meta_crtc_kms_get_modifiers (MetaCrtcKms *crtc_kms,
                             uint32_t     format)
{
  return meta_kms_plane_get_modifiers_for_format (crtc_kms->primary_plane,
                                                  format);
}

/**
 * meta_crtc_kms_copy_drm_format_list:
 * @crtc_kms: a #MetaCrtc object that has to be a #MetaCrtcKms
 *
 * Returns a new #GArray that the caller must destroy. The array
 * contains all the DRM pixel formats the CRTC supports on
 * its primary plane. The array element type is uint32_t.
 */
GArray *
meta_crtc_kms_copy_drm_format_list (MetaCrtcKms *crtc_kms)
{
  return meta_kms_plane_copy_drm_format_list (crtc_kms->primary_plane);
}

/**
 * meta_crtc_kms_supports_format:
 * @crtc_kms: a #MetaCrtcKms
 * @drm_format: a DRM pixel format
 *
 * Returns true if the CRTC supports the format on its primary plane.
 */
gboolean
meta_crtc_kms_supports_format (MetaCrtcKms *crtc_kms,
                               uint32_t     drm_format)
{
  return meta_kms_plane_is_format_supported (crtc_kms->primary_plane,
                                             drm_format);
}

MetaCrtcKms *
meta_crtc_kms_from_kms_crtc (MetaKmsCrtc *kms_crtc)
{
  return g_object_get_qdata (G_OBJECT (kms_crtc), kms_crtc_crtc_kms_quark);
}

MetaCrtcKms *
meta_crtc_kms_new (MetaGpuKms  *gpu_kms,
                   MetaKmsCrtc *kms_crtc)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaKmsDevice *kms_device;
  MetaCrtcKms *crtc_kms;
  MetaKmsPlane *primary_plane;

  kms_device = meta_gpu_kms_get_kms_device (gpu_kms);
  primary_plane = meta_kms_device_get_primary_plane_for (kms_device,
                                                         kms_crtc);
  crtc_kms = g_object_new (META_TYPE_CRTC_KMS,
                           "id", (uint64_t) meta_kms_crtc_get_id (kms_crtc),
                           "backend", meta_gpu_get_backend (gpu),
                           "gpu", gpu,
                           NULL);

  crtc_kms->kms_crtc = kms_crtc;
  crtc_kms->primary_plane = primary_plane;

  if (!kms_crtc_crtc_kms_quark)
    {
      kms_crtc_crtc_kms_quark =
        g_quark_from_static_string ("meta-kms-crtc-crtc-kms-quark");
    }

  g_object_set_qdata (G_OBJECT (kms_crtc), kms_crtc_crtc_kms_quark, crtc_kms);

  return crtc_kms;
}

static void
meta_crtc_kms_init (MetaCrtcKms *crtc_kms)
{
}

static void
meta_crtc_kms_class_init (MetaCrtcKmsClass *klass)
{
  MetaCrtcClass *crtc_class = META_CRTC_CLASS (klass);
  MetaCrtcNativeClass *crtc_native_class = META_CRTC_NATIVE_CLASS (klass);

  crtc_class->get_gamma_lut_size = meta_crtc_kms_get_gamma_lut_size;
  crtc_class->get_gamma_lut = meta_crtc_kms_get_gamma_lut;
  crtc_class->set_gamma_lut = meta_crtc_kms_set_gamma_lut;

  crtc_native_class->is_transform_handled = meta_crtc_kms_is_transform_handled;
  crtc_native_class->is_hw_cursor_supported = meta_crtc_kms_is_hw_cursor_supported;

  signals[GAMMA_LUT_CHANGED] =
    g_signal_new ("gamma-lut-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}
