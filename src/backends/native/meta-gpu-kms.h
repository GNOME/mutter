/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat
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

#ifndef META_GPU_KMS_H
#define META_GPU_KMS_H

#include <glib-object.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/meta-gpu.h"
#include "backends/native/meta-monitor-manager-kms.h"

#define META_TYPE_GPU_KMS (meta_gpu_kms_get_type ())
G_DECLARE_FINAL_TYPE (MetaGpuKms, meta_gpu_kms, META, GPU_KMS, MetaGpu)

typedef struct _MetaGpuKmsFlipClosureContainer MetaGpuKmsFlipClosureContainer;

typedef struct _MetaKmsResources
{
  drmModeRes *resources;
  drmModeEncoder **encoders;
  unsigned int n_encoders;
} MetaKmsResources;

typedef void (*MetaKmsFlipCallback) (void *user_data);

typedef enum _MetaGpuKmsFlag
{
  META_GPU_KMS_FLAG_NONE =                  0,
  META_GPU_KMS_FLAG_BOOT_VGA =        (1 << 0),
  META_GPU_KMS_FLAG_PLATFORM_DEVICE = (1 << 1),
} MetaGpuKmsFlag;

MetaGpuKms * meta_gpu_kms_new (MetaMonitorManagerKms  *monitor_manager_kms,
                               const char             *kms_file_path,
                               MetaGpuKmsFlag          flags,
                               GError                **error);

gboolean meta_gpu_kms_apply_crtc_mode (MetaGpuKms *gpu_kms,
                                       MetaCrtc   *crtc,
                                       int         x,
                                       int         y,
                                       uint32_t    fb_id);

gboolean meta_gpu_kms_can_have_outputs (MetaGpuKms *gpu_kms);

gboolean meta_gpu_kms_is_crtc_active (MetaGpuKms *gpu_kms,
                                      MetaCrtc   *crtc);

gboolean meta_gpu_kms_is_boot_vga (MetaGpuKms *gpu_kms);
gboolean meta_gpu_kms_is_platform_device (MetaGpuKms *gpu_kms);

gboolean meta_gpu_kms_flip_crtc (MetaGpuKms  *gpu_kms,
                                 MetaCrtc    *crtc,
                                 uint32_t     fb_id,
                                 GClosure    *flip_closure,
                                 GError     **error);

gboolean meta_gpu_kms_wait_for_flip (MetaGpuKms *gpu_kms,
                                     GError    **error);

int meta_gpu_kms_get_fd (MetaGpuKms *gpu_kms);

uint32_t meta_gpu_kms_get_id (MetaGpuKms *gpu_kms);

const char * meta_gpu_kms_get_file_path (MetaGpuKms *gpu_kms);

int64_t meta_gpu_kms_get_current_time_ns (MetaGpuKms *gpu_kms);

void meta_gpu_kms_get_max_buffer_size (MetaGpuKms *gpu_kms,
                                       int        *max_width,
                                       int        *max_height);

void meta_gpu_kms_set_power_save_mode (MetaGpuKms *gpu_kms,
                                       uint64_t    state);

MetaCrtcMode * meta_gpu_kms_get_mode_from_drm_mode (MetaGpuKms            *gpu_kms,
                                                    const drmModeModeInfo *drm_mode);

gboolean meta_drm_mode_equal (const drmModeModeInfo *one,
                              const drmModeModeInfo *two);

float meta_calculate_drm_mode_refresh_rate (const drmModeModeInfo *mode);

MetaGpuKmsFlipClosureContainer * meta_gpu_kms_wrap_flip_closure (MetaGpuKms *gpu_kms,
                                                                 MetaCrtc   *crtc,
                                                                 GClosure   *flip_closure);

void meta_gpu_kms_flip_closure_container_free (MetaGpuKmsFlipClosureContainer *closure_container);

#endif /* META_GPU_KMS_H */
