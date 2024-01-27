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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/meta-gpu.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms-types.h"

#define META_TYPE_GPU_KMS (meta_gpu_kms_get_type ())
G_DECLARE_FINAL_TYPE (MetaGpuKms, meta_gpu_kms, META, GPU_KMS, MetaGpu)

typedef struct _MetaGpuKmsFlipClosureContainer MetaGpuKmsFlipClosureContainer;

MetaGpuKms * meta_gpu_kms_new (MetaBackendNative  *backend_native,
                               MetaKmsDevice      *kms_device,
                               GError            **error);

gboolean meta_gpu_kms_can_have_outputs (MetaGpuKms *gpu_kms);

gboolean meta_gpu_kms_is_crtc_active (MetaGpuKms *gpu_kms,
                                      MetaCrtc   *crtc);

gboolean meta_gpu_kms_is_boot_vga (MetaGpuKms *gpu_kms);
gboolean meta_gpu_kms_is_platform_device (MetaGpuKms *gpu_kms);
gboolean meta_gpu_kms_disable_vrr (MetaGpuKms *gpu_kms);

MetaKmsDevice * meta_gpu_kms_get_kms_device (MetaGpuKms *gpu_kms);

uint32_t meta_gpu_kms_get_id (MetaGpuKms *gpu_kms);

const char * meta_gpu_kms_get_file_path (MetaGpuKms *gpu_kms);

void meta_gpu_kms_set_power_save_mode (MetaGpuKms    *gpu_kms,
                                       uint64_t       state,
                                       MetaKmsUpdate *kms_update);

MetaCrtcMode * meta_gpu_kms_get_mode_from_kms_mode (MetaGpuKms              *gpu_kms,
                                                    MetaKmsMode             *kms_mode,
                                                    MetaCrtcRefreshRateMode  refresh_rate_mode);

MetaGpuKmsFlipClosureContainer * meta_gpu_kms_wrap_flip_closure (MetaGpuKms *gpu_kms,
                                                                 MetaCrtc   *crtc,
                                                                 GClosure   *flip_closure);

void meta_gpu_kms_flip_closure_container_free (MetaGpuKmsFlipClosureContainer *closure_container);
