/* SPDX-License-Identifier: MIT */
#pragma once

#include <drm.h>

#define DRM_CASTKMS_MONITOR_CONTROL_VERSION 1
#define DRM_CASTKMS_MONITOR_MAX_EDID_SIZE (256U * 128U)

struct drm_castkms_create_monitor_control
{
  __u32 connector_id;
  __u32 flags;
  __s32 control_fd;
  __s32 revoke_fd;
  __u32 reserved;
};

struct drm_castkms_monitor_query
{
  __u32 version;
  __u32 flags;
  __u32 max_edid_size;
  __u32 reserved;
};

#define DRM_CASTKMS_CREATE_MONITOR_CONTROL 0x00
#define DRM_CASTKMS_MONITOR_QUERY 0x01

#define DRM_IOCTL_CASTKMS_CREATE_MONITOR_CONTROL \
  DRM_IOWR (DRM_COMMAND_BASE + DRM_CASTKMS_CREATE_MONITOR_CONTROL, \
            struct drm_castkms_create_monitor_control)
#define DRM_IOCTL_CASTKMS_MONITOR_QUERY \
  DRM_IOR (DRM_COMMAND_BASE + DRM_CASTKMS_MONITOR_QUERY, \
           struct drm_castkms_monitor_query)

#define DRM_CASTKMS_EXECUTION_PROPERTY "CASTKMS_EXECUTION"

/* Experimental connector description, shared with the development kernel. */
#define DRM_CASTKMS_EXECUTION_VERSION 1
#define DRM_CASTKMS_EXECUTION_HOST_V1 1

struct drm_castkms_execution
{
  __u32 version;
  __u32 profile;
  __u64 generation;
};
