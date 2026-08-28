/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */

#pragma once

/*
 * Minimal userspace copy of the CastKMS grant UAPI, with the audio right
 * added in 0.12. Non-audio grants still require only 0.10. Keep every copied
 * definition ABI-compatible with include/uapi/drm/castkms_drm.h in the
 * CastKMS kernel source. Mutter intentionally has no build-time dependency on
 * a sibling kernel-module checkout or on private kernel headers.
 */

#include <drm/drm.h>

#define DRM_CASTKMS_CAPTURE_UAPI_MAJOR 0
#define DRM_CASTKMS_CAPTURE_UAPI_MINOR 10

#define DRM_CASTKMS_CAPTURE_CAP_GRANT_FD (1ULL << 3)

/*
 * CREATE_GRANT also returns a close-on-exec control descriptor. Closing the
 * control descriptor revokes the grant. Its poll state permanently includes
 * POLLHUP after the final reference to the grant descriptor is closed or the
 * grant otherwise becomes terminal.
 */
#define DRM_CASTKMS_CAPTURE_CAP_GRANT_CONTROL_FD (1ULL << 4)

#define DRM_CASTKMS_GRANT_CAPTURE_PIXELS (1U << 0)
#define DRM_CASTKMS_GRANT_MANAGE_ATTACHMENT (1U << 1)
#define DRM_CASTKMS_GRANT_UPDATE_EDID (1U << 2)
#define DRM_CASTKMS_GRANT_READ_CURSOR (1U << 3)
#define DRM_CASTKMS_GRANT_MANAGE_CEC (1U << 4)
#define DRM_CASTKMS_GRANT_CAPTURE_AUDIO (1U << 5)

/*
 * grant_id is descriptive identity used by grant events. control_fd is the
 * grantor's revocation capability and must be -1 on input. reserved must be
 * zero.
 */
struct drm_castkms_create_grant
{
  __u32 connector_id;
  __u32 rights;
  __u32 flags;
  __s32 fd;
  __u32 grant_id;
  __u32 fd_flags;
  __s32 control_fd;
  __u32 reserved;
};

#define DRM_CASTKMS_GRANT_STATE_PENDING 0
#define DRM_CASTKMS_GRANT_STATE_ACTIVE 1
#define DRM_CASTKMS_GRANT_STATE_SUSPENDED_NO_MASTER 2
#define DRM_CASTKMS_GRANT_STATE_SUSPENDED_OTHER_MASTER 3
#define DRM_CASTKMS_GRANT_STATE_SUSPENDED_FOREIGN_CONTENT 4
#define DRM_CASTKMS_GRANT_STATE_REVOKED 5

struct drm_castkms_get_grant
{
  __u32 grant_id;
  __u32 connector_id;
  __u32 rights;
  __u32 state;
  __u32 flags;
  __u32 output_index;
  __u64 reserved;
};

struct drm_castkms_capture_query_caps
{
  __u32 uapi_major;
  __u32 uapi_minor;
  __u32 crtc_id;
  __u32 format_count;
  __u64 flags;
  __u64 formats_ptr;
  __u32 max_registered_buffers;
  __u32 reserved;
};

#define DRM_CASTKMS_CAPTURE_QUERY_CAPS 0x00
#define DRM_CASTKMS_CREATE_GRANT 0x11
#define DRM_CASTKMS_GET_GRANT 0x13

#define DRM_IOCTL_CASTKMS_CAPTURE_QUERY_CAPS \
  DRM_IOWR (DRM_COMMAND_BASE + DRM_CASTKMS_CAPTURE_QUERY_CAPS, \
            struct drm_castkms_capture_query_caps)
#define DRM_IOCTL_CASTKMS_CREATE_GRANT \
  DRM_IOWR (DRM_COMMAND_BASE + DRM_CASTKMS_CREATE_GRANT, \
            struct drm_castkms_create_grant)
#define DRM_IOCTL_CASTKMS_GET_GRANT \
  DRM_IOWR (DRM_COMMAND_BASE + DRM_CASTKMS_GET_GRANT, \
            struct drm_castkms_get_grant)
