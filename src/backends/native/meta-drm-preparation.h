/* SPDX-License-Identifier: MIT */
#pragma once

#include <drm.h>

/* Experimental preparation interface, shared with the development kernel. */
#define DRM_CAP_ATOMIC_PREPARATION 0x16
#define DRM_CLIENT_CAP_ATOMIC_PREPARATION 8

struct drm_mode_prepare_replace
{
  __u64 crtc_ids;
  __u32 count_crtcs;
  __u32 flags;
  __u64 reserved[2];
};

#define DRM_IOCTL_MODE_PREPARE_REPLACE DRM_IOW (0xD3, struct drm_mode_prepare_replace)

#define DRM_PREPARE_PENDING 0
#define DRM_PREPARE_READY 1
#define DRM_PREPARE_CONSUMED 2
#define DRM_PREPARE_CANCELED 3
#define DRM_PREPARE_FAILED 4

struct drm_prepare_query
{
  __u32 status;
  __u32 reserved[3];
};

#define DRM_IOCTL_PREPARE_QUERY DRM_IOR (0x00, struct drm_prepare_query)
