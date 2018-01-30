/*
 * Copyright © 2018 Canonical Ltd.
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
#include "backends/native/meta-kms-buffer.h"

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <errno.h>

#define INVALID_FB_ID 0U

struct _MetaKmsBuffer
{
  GObject parent;

  /* Contextual information we don't own (and assume lives longer than us since
     there's no refcount on these) */
  struct gbm_surface *gbm_surface;

  /* Members we share */
  MetaGpuKms *gpu_kms;

  struct gbm_bo *gbm_bo;
  uint32_t fb_id;
};

G_DEFINE_TYPE (MetaKmsBuffer, meta_kms_buffer, G_TYPE_OBJECT)

static gboolean
meta_kms_buffer_acquire_swapped_buffer (MetaKmsBuffer  *kms_buffer,
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
  int drm_fd;

  g_return_val_if_fail (META_IS_KMS_BUFFER (kms_buffer), FALSE);
  g_return_val_if_fail (kms_buffer->gbm_bo == NULL, FALSE);
  g_return_val_if_fail (kms_buffer->gbm_surface != NULL, FALSE);
  g_return_val_if_fail (kms_buffer->gpu_kms != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  drm_fd = meta_gpu_kms_get_fd (kms_buffer->gpu_kms);
  g_return_val_if_fail (drm_fd >= 0, FALSE);

  bo = gbm_surface_lock_front_buffer (kms_buffer->gbm_surface);
  if (bo == NULL)
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
      if (drmModeAddFB2WithModifiers (drm_fd,
                                      width,
                                      height,
                                      format,
                                      handles,
                                      strides,
                                      offsets,
                                      modifiers,
                                      &kms_buffer->fb_id,
                                      DRM_MODE_FB_MODIFIERS))
        {
          int e = errno;

          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (e),
                       "drmModeAddFB2WithModifiers failed: %s",
                       g_strerror (e));
          gbm_surface_release_buffer (kms_buffer->gbm_surface, bo);
          return FALSE;
        }
    }
  else if (drmModeAddFB2 (drm_fd,
                          width,
                          height,
                          format,
                          handles,
                          strides,
                          offsets,
                          &kms_buffer->fb_id,
                          0))
    {
      if (drmModeAddFB (drm_fd,
                        width,
                        height,
                        24,
                        32,
                        strides[0],
                        handles[0],
                        &kms_buffer->fb_id))
        {
          int e = errno;

          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (e),
                       "drmModeAddFB failed: %s",
                       g_strerror (e));
          gbm_surface_release_buffer (kms_buffer->gbm_surface, bo);
          return FALSE;
        }
    }

  kms_buffer->gbm_bo = bo;

  return TRUE;
}

MetaKmsBuffer *
meta_kms_buffer_new_from_gbm (MetaGpuKms          *gpu_kms,
                              struct gbm_surface  *gbm_surface,
                              gboolean             use_modifiers,
                              GError             **error)
{
  MetaKmsBuffer *kms_buffer;

  g_return_val_if_fail (META_IS_GPU_KMS (gpu_kms), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  kms_buffer = g_object_new (META_TYPE_KMS_BUFFER, NULL);
  kms_buffer->gpu_kms = META_GPU_KMS (g_object_ref (gpu_kms));
  kms_buffer->gbm_surface = gbm_surface;
  if (!meta_kms_buffer_acquire_swapped_buffer (kms_buffer,
                                               use_modifiers,
                                               error))
    {
      g_object_unref (kms_buffer);
      return NULL;
    }

  return kms_buffer;
}

MetaKmsBuffer *
meta_kms_buffer_new_from_dumb (MetaGpuKms *gpu_kms,
                               uint32_t    dumb_fb_id)
{
  MetaKmsBuffer *kms_buffer;

  g_return_val_if_fail (META_IS_GPU_KMS (gpu_kms), NULL);

  kms_buffer = g_object_new (META_TYPE_KMS_BUFFER, NULL);
  kms_buffer->gpu_kms = META_GPU_KMS (g_object_ref (gpu_kms));
  kms_buffer->fb_id = dumb_fb_id;

  return kms_buffer;
}

uint32_t
meta_kms_buffer_get_fb_id (const MetaKmsBuffer *kms_buffer)
{
  g_return_val_if_fail (kms_buffer != NULL, INVALID_FB_ID);

  return kms_buffer->fb_id;
}

struct gbm_bo *
meta_kms_buffer_get_bo (const MetaKmsBuffer *kms_buffer)
{
  g_return_val_if_fail (kms_buffer != NULL, NULL);

  return kms_buffer->gbm_bo;
}

static void
meta_kms_buffer_init (MetaKmsBuffer *kms_buffer)
{
  kms_buffer->fb_id = INVALID_FB_ID;
}

static void
meta_kms_buffer_dispose (GObject *object)
{
  MetaKmsBuffer *kms_buffer = META_KMS_BUFFER (object);
  int drm_fd = kms_buffer->gpu_kms ?
               meta_gpu_kms_get_fd (kms_buffer->gpu_kms) :
               -1;

  if (drm_fd >= 0 &&
      kms_buffer->fb_id != INVALID_FB_ID &&
      kms_buffer->gbm_bo)  /* We don't own dumb buffers, for now... */
    {
      drmModeRmFB (drm_fd, kms_buffer->fb_id);
      kms_buffer->fb_id = INVALID_FB_ID;
    }

  g_clear_object (&kms_buffer->gpu_kms);

  G_OBJECT_CLASS (meta_kms_buffer_parent_class)->dispose (object);
}

static void
meta_kms_buffer_finalize (GObject *object)
{
  MetaKmsBuffer *kms_buffer = META_KMS_BUFFER (object);

  if (kms_buffer->gbm_surface && kms_buffer->gbm_bo)
    gbm_surface_release_buffer (kms_buffer->gbm_surface,
                                kms_buffer->gbm_bo);

  G_OBJECT_CLASS (meta_kms_buffer_parent_class)->finalize (object);
}

static void
meta_kms_buffer_class_init (MetaKmsBufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_kms_buffer_dispose;
  object_class->finalize = meta_kms_buffer_finalize;
}
