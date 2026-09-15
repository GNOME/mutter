/* SPDX-License-Identifier: MIT */
#pragma once

#include <drm.h>

#define DRM_CASTKMS_MONITOR_CONTROL_VERSION 1
#define DRM_CASTKMS_MONITOR_MAX_EDID_SIZE (256U * 128U)
#define DRM_CASTKMS_RENDERER_VERSION 4

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

struct drm_castkms_renderer_files
{
  __s32 renderer_fd;
  __s32 revoke_fd;
};

struct drm_castkms_create_renderer_control
{
  __u32 crtc_id;
  __u32 connector_id;
  __u64 files;
  __u32 flags;
  __u32 reserved[3];
};

struct drm_castkms_renderer_query
{
  __u32 version;
  __u32 flags;
  __u32 profile;
  __u32 reserved;
  __u64 generation;
};

#define DRM_CASTKMS_CREATE_MONITOR_CONTROL 0x00
#define DRM_CASTKMS_CREATE_RENDERER_CONTROL 0x01
#define DRM_CASTKMS_MONITOR_QUERY 0x01
#define DRM_CASTKMS_RENDERER_QUERY 0x04

#define DRM_IOCTL_CASTKMS_CREATE_MONITOR_CONTROL \
  DRM_IOWR (DRM_COMMAND_BASE + DRM_CASTKMS_CREATE_MONITOR_CONTROL, \
            struct drm_castkms_create_monitor_control)
#define DRM_IOCTL_CASTKMS_CREATE_RENDERER_CONTROL \
  DRM_IOW (DRM_COMMAND_BASE + DRM_CASTKMS_CREATE_RENDERER_CONTROL, \
           struct drm_castkms_create_renderer_control)
#define DRM_IOCTL_CASTKMS_MONITOR_QUERY \
  DRM_IOR (DRM_COMMAND_BASE + DRM_CASTKMS_MONITOR_QUERY, \
           struct drm_castkms_monitor_query)
#define DRM_IOCTL_CASTKMS_RENDERER_QUERY \
  DRM_IOR (DRM_COMMAND_BASE + DRM_CASTKMS_RENDERER_QUERY, \
           struct drm_castkms_renderer_query)

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
