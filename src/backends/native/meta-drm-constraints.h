/* SPDX-License-Identifier: MIT */
#pragma once

#include <drm.h>

/* Experimental constraints interface shared with the development kernel. */
#define DRM_CONSTRAINTS_ID_PROPERTY "CONSTRAINTS_ID"

#define DRM_MODE_CONSTRAINTS_VERSION 1
#define DRM_MODE_CONSTRAINTS_MAX_BYTES (32U * 1024U * 1024U)
#define DRM_MODE_CONSTRAINTS_MAX_ENTRIES 64U
#define DRM_MODE_CONSTRAINTS_MAX_FORMATS 4096U
#define DRM_MODE_CONSTRAINTS_MAX_PROPERTIES 64U

#define DRM_MODE_CONSTRAINTS_SELECTABLE (1U << 0)
#define DRM_MODE_CONSTRAINTS_RECORD_REQUIRED (1U << 0)
#define DRM_MODE_CONSTRAINTS_RECORD_OUTPUT_SIZE 1U
#define DRM_MODE_CONSTRAINTS_RECORD_PLANE_FORMAT 2U
#define DRM_MODE_CONSTRAINTS_RECORD_PROPERTY 3U
#define DRM_MODE_CONSTRAINTS_LAYOUT_IMPLICIT (1U << 0)
#define DRM_MODE_CONSTRAINTS_FORMAT_STORAGE_NATIVE (1U << 0)
#define DRM_MODE_CONSTRAINTS_FORMAT_STORAGE_IMPORTED (1U << 1)

#define DRM_CLIENT_CAP_KMS_CONSTRAINTS 9

#define DRM_EVENT_KMS_CONSTRAINTS_LIST_CHANGED 0x04
#define DRM_KMS_CONSTRAINTS_LIST_CLOSED (1U << 0)

struct drm_event_kms_constraints_list_changed
{
  struct drm_event base;
  __u32 crtc_id;
  __u32 flags;
  __aligned_u64 generation;
  __aligned_u64 reserved;
};

struct drm_mode_list_constraints
{
  __u32 crtc_id;
  __u32 flags;
  __aligned_u64 generation;
  __aligned_u64 data;
  __u32 size;
  __u32 pad;
  __aligned_u64 reserved[2];
};

#define DRM_IOCTL_MODE_LIST_CONSTRAINTS \
  DRM_IOWR (0xD5, struct drm_mode_list_constraints)

struct drm_mode_constraints_list
{
  __u32 version;
  __u32 length;
  __aligned_u64 generation;
  __aligned_u64 selected_id;
  __aligned_u64 suggested_id;
  __u32 count_entries;
  __u32 entries_offset;
  __u32 entry_size;
  __u32 pad;
  __aligned_u64 reserved[2];
};

struct drm_mode_constraints
{
  __aligned_u64 id;
  __u32 flags;
  __u32 description_offset;
  __u32 description_length;
  __u32 pad;
  __aligned_u64 reserved[2];
};

struct drm_mode_constraints_description
{
  __u32 version;
  __u32 length;
  __u32 record_count;
  __u32 records_offset;
};

struct drm_mode_constraints_record
{
  __u32 type;
  __u32 flags;
  __u32 length;
  __u32 pad;
};

struct drm_mode_constraints_output_size
{
  struct drm_mode_constraints_record header;
  __u32 min_width;
  __u32 min_height;
  __u32 max_width;
  __u32 max_height;
};

struct drm_mode_constraints_plane_format
{
  struct drm_mode_constraints_record header;
  __u32 plane_id;
  __u32 format;
  __aligned_u64 modifier;
  __u32 min_width;
  __u32 min_height;
  __u32 max_width;
  __u32 max_height;
  __u32 layout_flags;
  __u32 storage_flags;
  __u32 plane_count;
  __u32 pitch_alignment;
  __u32 offset_alignment;
  __u32 max_pitch;
};

struct drm_mode_constraints_property
{
  struct drm_mode_constraints_record header;
  __u32 object_id;
  __u32 property_id;
  __u32 type;
  __u32 pad;
  __aligned_u64 minimum;
  __aligned_u64 maximum;
  __aligned_u64 mask;
};
