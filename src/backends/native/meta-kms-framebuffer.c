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

#include "backends/native/meta-kms-framebuffer.h"

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <errno.h>

#define INVALID_FB_ID 0U

struct _MetaKmsFramebuffer
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

G_DEFINE_TYPE (MetaKmsFramebuffer, meta_kms_framebuffer, G_TYPE_OBJECT)

static gboolean
meta_kms_framebuffer_acquire_swapped_buffer (MetaKmsFramebuffer *kms_framebuffer,
                                             gboolean            use_modifiers,
                                             GError            **error)
{
  uint32_t handles[4] = {0, 0, 0, 0};
  uint32_t strides[4] = {0, 0, 0, 0};
  uint32_t offsets[4] = {0, 0, 0, 0};
  uint64_t modifiers[4] = {0, 0, 0, 0};
  uint32_t width, height, format;
  struct gbm_bo *bo;
  int i;
  int drm_fd;

  g_return_val_if_fail (META_IS_KMS_FRAMEBUFFER (kms_framebuffer), FALSE);
  g_return_val_if_fail (kms_framebuffer->gbm_bo == NULL, FALSE);
  g_return_val_if_fail (kms_framebuffer->gbm_surface != NULL, FALSE);
  g_return_val_if_fail (kms_framebuffer->gpu_kms != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  drm_fd = meta_gpu_kms_get_fd (kms_framebuffer->gpu_kms);
  g_return_val_if_fail (drm_fd >= 0, FALSE);

  bo = gbm_surface_lock_front_buffer (kms_framebuffer->gbm_surface);
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
                                      &kms_framebuffer->fb_id,
                                      DRM_MODE_FB_MODIFIERS))
        {
          int e = errno;

          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (e),
                       "drmModeAddFB2WithModifiers failed: %s",
                       g_strerror (e));
          gbm_surface_release_buffer (kms_framebuffer->gbm_surface, bo);
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
                          &kms_framebuffer->fb_id,
                          0))
    {
      if (drmModeAddFB (drm_fd,
                        width,
                        height,
                        24,
                        32,
                        strides[0],
                        handles[0],
                        &kms_framebuffer->fb_id))
        {
          int e = errno;

          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (e),
                       "drmModeAddFB failed: %s",
                       g_strerror (e));
          gbm_surface_release_buffer (kms_framebuffer->gbm_surface, bo);
          return FALSE;
        }
    }

  kms_framebuffer->gbm_bo = bo;

  return TRUE;
}

MetaKmsFramebuffer*
meta_kms_framebuffer_new_from_gbm (MetaGpuKms         *gpu_kms,
                                   struct gbm_surface *gbm_surface,
                                   gboolean            use_modifiers,
                                   GError            **error)
{
  MetaKmsFramebuffer *kms_fb;

  g_return_val_if_fail (META_IS_GPU_KMS (gpu_kms), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  kms_fb = g_object_new (META_TYPE_KMS_FRAMEBUFFER, NULL);
  kms_fb->gpu_kms = META_GPU_KMS (g_object_ref (gpu_kms));
  kms_fb->gbm_surface = gbm_surface;
  if (!meta_kms_framebuffer_acquire_swapped_buffer (kms_fb,
                                                    use_modifiers,
                                                    error))
    g_clear_object (&kms_fb);

  return kms_fb;
}

MetaKmsFramebuffer*
meta_kms_framebuffer_new_from_dumb (MetaGpuKms *gpu_kms,
                                    uint32_t    dumb_fb_id)
{
  MetaKmsFramebuffer *kms_fb;

  g_return_val_if_fail (META_IS_GPU_KMS (gpu_kms), NULL);

  kms_fb = g_object_new (META_TYPE_KMS_FRAMEBUFFER, NULL);
  kms_fb->gpu_kms = META_GPU_KMS (g_object_ref (gpu_kms));
  kms_fb->fb_id = dumb_fb_id;

  return kms_fb;
}

uint32_t
meta_kms_framebuffer_get_fb_id (const MetaKmsFramebuffer *kms_framebuffer)
{
  g_return_val_if_fail (kms_framebuffer != NULL, INVALID_FB_ID);

  return kms_framebuffer->fb_id;
}

struct gbm_bo *
meta_kms_framebuffer_get_bo (const MetaKmsFramebuffer *kms_framebuffer)
{
  g_return_val_if_fail (kms_framebuffer != NULL, NULL);

  return kms_framebuffer->gbm_bo;
}

static void
meta_kms_framebuffer_init (MetaKmsFramebuffer *kms_framebuffer)
{
  kms_framebuffer->fb_id = INVALID_FB_ID;
}

static void
meta_kms_framebuffer_dispose (GObject *object)
{
  MetaKmsFramebuffer *kms_framebuffer = META_KMS_FRAMEBUFFER (object);
  int drm_fd = kms_framebuffer->gpu_kms ?
               meta_gpu_kms_get_fd (kms_framebuffer->gpu_kms) :
               -1;

  if (drm_fd >= 0 &&
      kms_framebuffer->fb_id != INVALID_FB_ID &&
      kms_framebuffer->gbm_bo)  /* We don't own dumb buffers, for now... */
    {
      drmModeRmFB (drm_fd, kms_framebuffer->fb_id);
      kms_framebuffer->fb_id = INVALID_FB_ID;
    }

  g_clear_object (&kms_framebuffer->gpu_kms);

  G_OBJECT_CLASS (meta_kms_framebuffer_parent_class)->dispose (object);
}

static void
meta_kms_framebuffer_finalize (GObject *object)
{
  MetaKmsFramebuffer *kms_framebuffer = META_KMS_FRAMEBUFFER (object);

  if (kms_framebuffer->gbm_surface && kms_framebuffer->gbm_bo)
    gbm_surface_release_buffer (kms_framebuffer->gbm_surface,
                                kms_framebuffer->gbm_bo);

  kms_framebuffer->fb_id = INVALID_FB_ID;
  kms_framebuffer->gbm_bo = NULL;

  /* We don't own these: */
  kms_framebuffer->gbm_surface = NULL;

  G_OBJECT_CLASS (meta_kms_framebuffer_parent_class)->finalize (object);
}

static void
meta_kms_framebuffer_class_init (MetaKmsFramebufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_kms_framebuffer_dispose;
  object_class->finalize = meta_kms_framebuffer_finalize;
}
