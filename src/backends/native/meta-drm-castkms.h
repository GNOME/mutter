/* SPDX-License-Identifier: MIT */
#pragma once

#include <drm.h>

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
