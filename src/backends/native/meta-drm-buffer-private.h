/*
 * Copyright (C) 2018 Canonical Ltd.
 * Copyright (C) 2019-2020 Red Hat Inc.
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

#ifndef META_DRM_BUFFER_PRIVATE_H
#define META_DRM_BUFFER_PRIVATE_H

#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-kms-device.h"

typedef struct _MetaDrmFbArgs
{
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t handles[4];
  uint32_t offsets[4];
  uint32_t strides[4];
  uint64_t modifiers[4];
} MetaDrmFbArgs;

struct _MetaDrmBufferClass
{
  GObjectClass parent_class;

  int (* get_width) (MetaDrmBuffer *buffer);
  int (* get_height) (MetaDrmBuffer *buffer);
  int (* get_stride) (MetaDrmBuffer *buffer);
  uint32_t (* get_format) (MetaDrmBuffer *buffer);
};

MetaKmsDevice * meta_drm_buffer_get_device (MetaDrmBuffer *buffer);

gboolean meta_drm_buffer_ensure_fb_id (MetaDrmBuffer        *buffer,
                                       gboolean              use_modifiers,
                                       const MetaDrmFbArgs  *fb_args,
                                       GError              **error);

gboolean meta_drm_buffer_ensure_fb_in_impl (MetaDrmBuffer        *buffer,
                                            gboolean              use_modifiers,
                                            const MetaDrmFbArgs  *fb_args,
                                            GError              **error);

#endif /* META_DRM_BUFFER_PRIVATE_H */
