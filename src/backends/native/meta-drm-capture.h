/* SPDX-License-Identifier: MIT */
#pragma once

#include <drm.h>

/* Experimental capture interface, shared with the development kernel. */
#define DRM_CAP_CAPTURE_GRANT 0x17

struct drm_capture_grant_files
{
  __s32 capture_fd;
  __s32 control_fd;
};

struct drm_mode_create_capture_grant
{
  __u32 crtc_id;
  __u32 connector_id;
  __u64 files;
  __u32 flags;
  __u32 reserved[3];
};

#define DRM_IOCTL_MODE_CREATE_CAPTURE_GRANT \
  DRM_IOW (0xD4, struct drm_mode_create_capture_grant)
