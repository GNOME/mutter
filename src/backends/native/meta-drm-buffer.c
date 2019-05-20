/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
 * Copyright (C) 2018 Canonical Ltd.
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
 *
 * Author: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#include "config.h"

#include "backends/native/meta-drm-buffer.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define INVALID_FB_ID 0U

typedef enum _MetaDrmBufferType
{
  META_DRM_BUFFER_TYPE_GBM,
  META_DRM_BUFFER_TYPE_WRAPPED_DUMB
} MetaDrmBufferType;

struct _MetaDrmBuffer
{
  GObject parent;

  MetaDrmBufferType type;

  union
  {
    uint32_t fb_id;

    struct
    {
      uint32_t fb_id;
      struct gbm_surface *surface;
      struct gbm_bo *bo;
      MetaGpuKms *gpu_kms;
    } gbm;

    struct
    {
      uint32_t fb_id;
    } wrapped_dumb;
  };
};

G_DEFINE_TYPE (MetaDrmBuffer, meta_drm_buffer, G_TYPE_OBJECT)

static gboolean
meta_drm_buffer_acquire_swapped_buffer (MetaDrmBuffer  *buffer,
                                        gboolean        use_modifiers,
                                        GError        **error)
{
  uint32_t handles[4] = {0, 0, 0, 0};
  uint32_t strides[4] = {0, 0, 0, 0};
  uint32_t offsets[4] = {0, 0, 0, 0};
  uint64_t modifiers[4] = {0, 0, 0, 0};
  uint32_t width, height, format;
  struct gbm_bo *bo;
  int i;
  int kms_fd;

  g_assert (buffer->type == META_DRM_BUFFER_TYPE_GBM);
  g_assert (!buffer->gbm.bo);
  g_assert (buffer->gbm.surface);
  g_assert (buffer->gbm.gpu_kms);

  kms_fd = meta_gpu_kms_get_fd (buffer->gbm.gpu_kms);

  bo = gbm_surface_lock_front_buffer (buffer->gbm.surface);
  if (!bo)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "gbm_surface_lock_front_buffer failed");
      return FALSE;
    }

  if (gbm_bo_get_handle_for_plane (bo, 0).s32 == -1)
    {
      /* Failed to fetch handle to plane, falling back to old method */
      strides[0] = gbm_bo_get_stride (bo);
      handles[0] = gbm_bo_get_handle (bo).u32;
      offsets[0] = 0;
      modifiers[0] = DRM_FORMAT_MOD_INVALID;
    }
  else
    {
      for (i = 0; i < gbm_bo_get_plane_count (bo); i++)
        {
          strides[i] = gbm_bo_get_stride_for_plane (bo, i);
          handles[i] = gbm_bo_get_handle_for_plane (bo, i).u32;
          offsets[i] = gbm_bo_get_offset (bo, i);
          modifiers[i] = gbm_bo_get_modifier (bo);
        }
     }

  width = gbm_bo_get_width (bo);
  height = gbm_bo_get_height (bo);
  format = gbm_bo_get_format (bo);

  if (use_modifiers && modifiers[0] != DRM_FORMAT_MOD_INVALID)
    {
      if (drmModeAddFB2WithModifiers (kms_fd,
                                      width,
                                      height,
                                      format,
                                      handles,
                                      strides,
                                      offsets,
                                      modifiers,
                                      &buffer->fb_id,
                                      DRM_MODE_FB_MODIFIERS))
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "drmModeAddFB2WithModifiers failed: %s",
                       g_strerror (errno));
          gbm_surface_release_buffer (buffer->gbm.surface, bo);
          return FALSE;
        }
    }
  else if (drmModeAddFB2 (kms_fd,
                          width,
                          height,
                          format,
                          handles,
                          strides,
                          offsets,
                          &buffer->fb_id,
                          0))
    {
      if (format != DRM_FORMAT_XRGB8888)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       "drmModeAddFB does not support format 0x%x",
                       format);
          gbm_surface_release_buffer (buffer->gbm.surface, bo);
          return FALSE;
        }

      if (drmModeAddFB (kms_fd,
                        width,
                        height,
                        24,
                        32,
                        strides[0],
                        handles[0],
                        &buffer->fb_id))
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "drmModeAddFB failed: %s",
                       g_strerror (errno));
          gbm_surface_release_buffer (buffer->gbm.surface, bo);
          return FALSE;
        }
    }

  buffer->gbm.bo = bo;

  return TRUE;
}

static void
meta_drm_buffer_init (MetaDrmBuffer *buffer)
{
}

static void
meta_drm_buffer_finalize (GObject *object)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (object);

  if (buffer->type == META_DRM_BUFFER_TYPE_GBM)
    {
      if (buffer->gbm.gpu_kms != NULL &&
          buffer->gbm.fb_id != INVALID_FB_ID)
        {
          int kms_fd;

          kms_fd = meta_gpu_kms_get_fd (buffer->gbm.gpu_kms);
          drmModeRmFB (kms_fd, buffer->fb_id);
          buffer->fb_id = INVALID_FB_ID;
        }

      if (buffer->gbm.surface &&
          buffer->gbm.bo)
        {
          gbm_surface_release_buffer (buffer->gbm.surface,
                                      buffer->gbm.bo);
        }
    }

  G_OBJECT_CLASS (meta_drm_buffer_parent_class)->finalize (object);
}

static void
meta_drm_buffer_class_init (MetaDrmBufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_drm_buffer_finalize;
}

MetaDrmBuffer *
meta_drm_buffer_new_from_gbm (MetaGpuKms          *gpu_kms,
                              struct gbm_surface  *gbm_surface,
                              gboolean             use_modifiers,
                              GError             **error)
{
  MetaDrmBuffer *buffer;

  buffer = g_object_new (META_TYPE_DRM_BUFFER, NULL);
  buffer->type = META_DRM_BUFFER_TYPE_GBM;
  buffer->gbm.gpu_kms = gpu_kms;
  buffer->gbm.surface = gbm_surface;

  if (!meta_drm_buffer_acquire_swapped_buffer (buffer,
                                               use_modifiers,
                                               error))
    {
      g_object_unref (buffer);
      return NULL;
    }

  return buffer;
}

MetaDrmBuffer *
meta_drm_buffer_new_from_dumb (uint32_t dumb_fb_id)
{
  MetaDrmBuffer *buffer;

  buffer = g_object_new (META_TYPE_DRM_BUFFER, NULL);
  buffer->type = META_DRM_BUFFER_TYPE_WRAPPED_DUMB;
  buffer->wrapped_dumb.fb_id = dumb_fb_id;

  return buffer;
}

uint32_t
meta_drm_buffer_get_fb_id (const MetaDrmBuffer *buffer)
{
  return buffer->fb_id;
}

struct gbm_bo *
meta_drm_buffer_get_bo (const MetaDrmBuffer *buffer)
{
  g_return_val_if_fail (buffer->type == META_DRM_BUFFER_TYPE_GBM, NULL);

  return buffer->gbm.bo;
}
