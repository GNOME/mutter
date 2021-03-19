/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
 * Copyright (c) 2018 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-gpu-kms.h"

#include <drm.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/meta-crtc.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-output.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-crtc-mode-kms.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-mode.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-launcher.h"
#include "backends/native/meta-output-kms.h"

struct _MetaGpuKms
{
  MetaGpu parent;

  MetaKmsDevice *kms_device;

  uint32_t id;
  int fd;

  clockid_t clock_id;

  gboolean resources_init_failed_before;
};

G_DEFINE_TYPE (MetaGpuKms, meta_gpu_kms, META_TYPE_GPU)

gboolean
meta_gpu_kms_is_crtc_active (MetaGpuKms *gpu_kms,
                             MetaCrtc   *crtc)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaBackend *backend = meta_gpu_get_backend (gpu);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *l;
  gboolean connected_crtc_found;

  g_assert (meta_crtc_get_gpu (crtc) == META_GPU (gpu_kms));

  if (meta_monitor_manager_get_power_save_mode (monitor_manager))
    return FALSE;

  connected_crtc_found = FALSE;
  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;
      MetaCrtc *assigned_crtc;

      assigned_crtc = meta_output_get_assigned_crtc (output);
      if (assigned_crtc == crtc)
        {
          connected_crtc_found = TRUE;
          break;
        }
    }

  if (!connected_crtc_found)
    return FALSE;

  return TRUE;
}

MetaKmsDevice *
meta_gpu_kms_get_kms_device (MetaGpuKms *gpu_kms)
{
  return gpu_kms->kms_device;
}

int
meta_gpu_kms_get_fd (MetaGpuKms *gpu_kms)
{
  return gpu_kms->fd;
}

uint32_t
meta_gpu_kms_get_id (MetaGpuKms *gpu_kms)
{
  return gpu_kms->id;
}

const char *
meta_gpu_kms_get_file_path (MetaGpuKms *gpu_kms)
{
  return meta_kms_device_get_path (gpu_kms->kms_device);
}

gboolean
meta_gpu_kms_is_clock_monotonic (MetaGpuKms *gpu_kms)
{
  return gpu_kms->clock_id == CLOCK_MONOTONIC;
}

gboolean
meta_gpu_kms_is_boot_vga (MetaGpuKms *gpu_kms)
{
  MetaKmsDeviceFlag flags;

  flags = meta_kms_device_get_flags (gpu_kms->kms_device);
  return !!(flags & META_KMS_DEVICE_FLAG_BOOT_VGA);
}

gboolean
meta_gpu_kms_is_platform_device (MetaGpuKms *gpu_kms)
{
  MetaKmsDeviceFlag flags;

  flags = meta_kms_device_get_flags (gpu_kms->kms_device);
  return !!(flags & META_KMS_DEVICE_FLAG_PLATFORM_DEVICE);
}

gboolean
meta_gpu_kms_disable_modifiers (MetaGpuKms *gpu_kms)
{
  MetaKmsDeviceFlag flags;

  flags = meta_kms_device_get_flags (gpu_kms->kms_device);
  return !!(flags & META_KMS_DEVICE_FLAG_DISABLE_MODIFIERS);
}

static int
compare_outputs (gconstpointer one,
                 gconstpointer two)
{
  MetaOutput *o_one = (MetaOutput *) one;
  MetaOutput *o_two = (MetaOutput *) two;
  const MetaOutputInfo *output_info_one = meta_output_get_info (o_one);
  const MetaOutputInfo *output_info_two = meta_output_get_info (o_two);

  return strcmp (output_info_one->name, output_info_two->name);
}

MetaCrtcMode *
meta_gpu_kms_get_mode_from_kms_mode (MetaGpuKms  *gpu_kms,
                                     MetaKmsMode *kms_mode)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  GList *l;

  for (l = meta_gpu_get_modes (gpu); l; l = l->next)
    {
      MetaCrtcModeKms *crtc_mode_kms = l->data;

      if (meta_kms_mode_equal (kms_mode,
                               meta_crtc_mode_kms_get_kms_mode (crtc_mode_kms)))
        return META_CRTC_MODE (crtc_mode_kms);
    }

  g_assert_not_reached ();
  return NULL;
}

static MetaOutput *
find_output_by_connector_id (GList    *outputs,
                             uint32_t  connector_id)
{
  GList *l;

  for (l = outputs; l; l = l->next)
    {
      MetaOutput *output = l->data;

      if (meta_output_kms_get_connector_id (META_OUTPUT_KMS (output)) ==
          connector_id)
        return output;
    }

  return NULL;
}

static void
setup_output_clones (MetaGpu *gpu)
{
  GList *l;

  for (l = meta_gpu_get_outputs (gpu); l; l = l->next)
    {
      MetaOutput *output = l->data;
      GList *k;

      for (k = meta_gpu_get_outputs (gpu); k; k = k->next)
        {
          MetaOutput *other_output = k->data;

          if (other_output == output)
            continue;

          if (meta_output_kms_can_clone (META_OUTPUT_KMS (output),
                                         META_OUTPUT_KMS (other_output)))
            meta_output_add_possible_clone (output, other_output);
        }
    }
}

static void
init_modes (MetaGpuKms *gpu_kms)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaKmsDevice *kms_device = gpu_kms->kms_device;
  GHashTable *modes_table;
  GList *l;
  GList *modes;
  GHashTableIter iter;
  gpointer value;
  uint64_t mode_id;

  /*
   * Gather all modes on all connected connectors.
   */
  modes_table = g_hash_table_new ((GHashFunc) meta_kms_mode_hash,
                                  (GEqualFunc) meta_kms_mode_equal);
  for (l = meta_kms_device_get_connectors (kms_device); l; l = l->next)
    {
      MetaKmsConnector *kms_connector = l->data;
      const MetaKmsConnectorState *state;
      GList *l_mode;

      state = meta_kms_connector_get_current_state (kms_connector);
      if (!state)
        continue;

      for (l_mode = state->modes; l_mode; l_mode = l_mode->next)
        {
          MetaKmsMode *kms_mode = l_mode->data;

          g_hash_table_add (modes_table, kms_mode);
        }
    }

  for (l = meta_kms_device_get_fallback_modes (kms_device); l; l = l->next)
    {
      MetaKmsMode *fallback_mode = l->data;

      g_hash_table_add (modes_table, fallback_mode);
    }

  modes = NULL;

  g_hash_table_iter_init (&iter, modes_table);
  mode_id = 0;
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      MetaKmsMode *kms_mode = value;
      MetaCrtcModeKms *mode;

      mode = meta_crtc_mode_kms_new (kms_mode, mode_id);
      modes = g_list_append (modes, mode);

      mode_id++;
    }

  g_hash_table_destroy (modes_table);

  meta_gpu_take_modes (gpu, modes);
}

static void
init_crtcs (MetaGpuKms *gpu_kms)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaKmsDevice *kms_device = gpu_kms->kms_device;
  GList *l;
  GList *crtcs;

  crtcs = NULL;

  for (l = meta_kms_device_get_crtcs (kms_device); l; l = l->next)
    {
      MetaKmsCrtc *kms_crtc = l->data;
      MetaCrtcKms *crtc_kms;

      crtc_kms = meta_crtc_kms_new (gpu_kms, kms_crtc);

      crtcs = g_list_append (crtcs, crtc_kms);
    }

  meta_gpu_take_crtcs (gpu, crtcs);
}

static void
init_frame_clock (MetaGpuKms *gpu_kms)
{
  uint64_t uses_monotonic;

  if (drmGetCap (gpu_kms->fd, DRM_CAP_TIMESTAMP_MONOTONIC, &uses_monotonic) != 0)
    uses_monotonic = 0;

  gpu_kms->clock_id = uses_monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME;
}

static void
init_outputs (MetaGpuKms *gpu_kms)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  GList *old_outputs;
  GList *outputs;
  GList *l;

  old_outputs = meta_gpu_get_outputs (gpu);

  outputs = NULL;

  for (l = meta_kms_device_get_connectors (gpu_kms->kms_device); l; l = l->next)
    {
      MetaKmsConnector *kms_connector = l->data;
      const MetaKmsConnectorState *connector_state;
      MetaOutputKms *output_kms;
      MetaOutput *old_output;
      GError *error = NULL;

      connector_state = meta_kms_connector_get_current_state (kms_connector);
      if (!connector_state || connector_state->non_desktop)
        continue;

      old_output =
        find_output_by_connector_id (old_outputs,
                                     meta_kms_connector_get_id (kms_connector));
      output_kms = meta_output_kms_new (gpu_kms,
                                        kms_connector,
                                        old_output,
                                        &error);
      if (!output_kms)
        {
          g_warning ("Failed to create KMS output: %s", error->message);
          g_error_free (error);
        }
      else
        {
          outputs = g_list_prepend (outputs, output_kms);
        }
    }


  /* Sort the outputs for easier handling in MetaMonitorConfig */
  outputs = g_list_sort (outputs, compare_outputs);
  meta_gpu_take_outputs (gpu, outputs);

  setup_output_clones (gpu);
}

static gboolean
meta_gpu_kms_read_current (MetaGpu  *gpu,
                           GError  **error)
{
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);

  /* Note: we must not free the public structures (output, crtc, monitor
     mode and monitor info) here, they must be kept alive until the API
     users are done with them after we emit monitors-changed, and thus
     are freed by the platform-independent layer. */

  init_modes (gpu_kms);
  init_crtcs (gpu_kms);
  init_outputs (gpu_kms);
  init_frame_clock (gpu_kms);

  return TRUE;
}

gboolean
meta_gpu_kms_can_have_outputs (MetaGpuKms *gpu_kms)
{
  GList *l;
  int n_connected_connectors = 0;

  for (l = meta_kms_device_get_connectors (gpu_kms->kms_device); l; l = l->next)
    {
      MetaKmsConnector *kms_connector = l->data;

      if (meta_kms_connector_get_current_state (kms_connector))
        n_connected_connectors++;
    }

  return n_connected_connectors > 0;
}

MetaGpuKms *
meta_gpu_kms_new (MetaBackendNative  *backend_native,
                  MetaKmsDevice      *kms_device,
                  GError            **error)
{
  MetaGpuKms *gpu_kms;
  int kms_fd;

  kms_fd = meta_kms_device_leak_fd (kms_device);

  gpu_kms = g_object_new (META_TYPE_GPU_KMS,
                          "backend", backend_native,
                          NULL);

  gpu_kms->kms_device = kms_device;
  gpu_kms->fd = kms_fd;

  meta_gpu_kms_read_current (META_GPU (gpu_kms), NULL);

  return gpu_kms;
}

static void
meta_gpu_kms_init (MetaGpuKms *gpu_kms)
{
  static uint32_t id = 0;

  gpu_kms->fd = -1;
  gpu_kms->id = ++id;
}

static void
meta_gpu_kms_class_init (MetaGpuKmsClass *klass)
{
  MetaGpuClass *gpu_class = META_GPU_CLASS (klass);

  gpu_class->read_current = meta_gpu_kms_read_current;
}
