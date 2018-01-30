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

#include "backends/native/meta-framebuffer-kms.h"
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define INVALID_FB_ID 0U

struct _MetaFramebufferKms
{
  GObject parent;

  /* Contextual information we don't own (and assume lives longer than us since
     there's no refcount on these) */
  int drm_fd;
  struct gbm_surface *gbm_surface;

  /* Members we own and will destroy when refcount reaches zero */
  struct gbm_bo *gbm_bo;
  uint32_t fb_id;
};

G_DEFINE_TYPE (MetaFramebufferKms, meta_framebuffer_kms, G_TYPE_OBJECT)

static void
meta_framebuffer_kms_init (MetaFramebufferKms *framebuffer_kms)
{
  framebuffer_kms->drm_fd = -1;
  framebuffer_kms->fb_id = INVALID_FB_ID;
}

static void
meta_framebuffer_kms_finalize (GObject *object)
{
  MetaFramebufferKms *framebuffer_kms = META_FRAMEBUFFER_KMS (object);

  meta_framebuffer_kms_release_buffer (framebuffer_kms);

  /* We don't own these: */
  framebuffer_kms->gbm_surface = NULL;
  framebuffer_kms->drm_fd = -1;

  G_OBJECT_CLASS (meta_framebuffer_kms_parent_class)->finalize (object);
}

static void
meta_framebuffer_kms_class_init (MetaFramebufferKmsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_framebuffer_kms_finalize;
}

void
meta_framebuffer_kms_set_drm_fd (MetaFramebufferKms *framebuffer_kms,
                                 int                 drm_fd)
{
  g_return_if_fail (framebuffer_kms != NULL);
  g_return_if_fail (framebuffer_kms->gbm_bo == NULL);
  g_return_if_fail (framebuffer_kms->gbm_surface == NULL);

  framebuffer_kms->drm_fd = drm_fd;
}

void
meta_framebuffer_kms_set_gbm_surface (MetaFramebufferKms *framebuffer_kms,
                                      struct gbm_surface *gbm_surface)
{
  g_return_if_fail (framebuffer_kms != NULL);
  g_return_if_fail (framebuffer_kms->gbm_bo == NULL);
  g_return_if_fail (framebuffer_kms->drm_fd >= 0);

  framebuffer_kms->gbm_surface = gbm_surface;
}

gboolean
meta_framebuffer_kms_acquire_swapped_buffer (MetaFramebufferKms *framebuffer_kms,
                                             gboolean use_modifiers)
{
  uint32_t handles[4] = {0, 0, 0, 0};
  uint32_t strides[4] = {0, 0, 0, 0};
  uint32_t offsets[4] = {0, 0, 0, 0};
  uint64_t modifiers[4] = {0, 0, 0, 0};
  uint32_t width, height, format;
  struct gbm_bo *bo;
  int i;

  g_return_val_if_fail (framebuffer_kms != NULL, FALSE);
  g_return_val_if_fail (framebuffer_kms->gbm_bo == NULL, FALSE);
  g_return_val_if_fail (framebuffer_kms->gbm_surface != NULL, FALSE);
  g_return_val_if_fail (framebuffer_kms->drm_fd >= 0, FALSE);

  bo = gbm_surface_lock_front_buffer (framebuffer_kms->gbm_surface);
  if (bo == NULL)
    {
      g_warning ("gbm_surface_lock_front_buffer failed");
      return FALSE;
    }

  for (i = 0; i < gbm_bo_get_plane_count (bo); ++i)
    {
      strides[i] = gbm_bo_get_stride_for_plane (bo, i);
      handles[i] = gbm_bo_get_handle_for_plane (bo, i).u32;
      offsets[i] = gbm_bo_get_offset (bo, i);
      modifiers[i] = gbm_bo_get_modifier (bo);
    }

  width = gbm_bo_get_width (bo);
  height = gbm_bo_get_height (bo);
  format = gbm_bo_get_format (bo);

  if (use_modifiers && modifiers[0] != DRM_FORMAT_MOD_INVALID)
    {
      if (drmModeAddFB2WithModifiers (framebuffer_kms->drm_fd,
                                      width,
                                      height,
                                      format,
                                      handles,
                                      strides,
                                      offsets,
                                      modifiers,
                                      &framebuffer_kms->fb_id,
                                      DRM_MODE_FB_MODIFIERS))
        {
          g_warning ("drmModeAddFB2WithModifiers failed: %m");
          gbm_surface_release_buffer (framebuffer_kms->gbm_surface, bo);
          return FALSE;
        }
    }
  else if (drmModeAddFB2 (framebuffer_kms->drm_fd,
                          width,
                          height,
                          format,
                          handles,
                          strides,
                          offsets,
                          &framebuffer_kms->fb_id,
                          0))
    {
      if (drmModeAddFB (framebuffer_kms->drm_fd,
                        width,
                        height,
                        24,
                        32,
                        strides[0],
                        handles[0],
                        &framebuffer_kms->fb_id))
        {
          g_warning ("drmModeAddFB failed: %m");
          gbm_surface_release_buffer (framebuffer_kms->gbm_surface, bo);
          return FALSE;
        }
    }

  framebuffer_kms->gbm_bo = bo;
  return TRUE;
}

void
meta_framebuffer_kms_borrow_dumb_buffer (MetaFramebufferKms *framebuffer_kms,
                                         uint32_t            dumb_fb_id)
{
  g_return_if_fail (framebuffer_kms != NULL);
  g_return_if_fail (framebuffer_kms->fb_id == INVALID_FB_ID);

  framebuffer_kms->fb_id = dumb_fb_id;
}

uint32_t
meta_framebuffer_kms_get_fb_id (const MetaFramebufferKms *framebuffer_kms)
{
  g_return_val_if_fail (framebuffer_kms != NULL, INVALID_FB_ID);

  return framebuffer_kms->fb_id;
}

struct gbm_bo *
meta_framebuffer_kms_get_bo (const MetaFramebufferKms *framebuffer_kms)
{
  g_return_val_if_fail (framebuffer_kms != NULL, NULL);

  return framebuffer_kms->gbm_bo;
}

void
meta_framebuffer_kms_release_buffer (MetaFramebufferKms *framebuffer_kms)
{
  g_return_if_fail (framebuffer_kms != NULL);

  if (framebuffer_kms->drm_fd >= 0 &&
      framebuffer_kms->fb_id != INVALID_FB_ID &&
      framebuffer_kms->gbm_bo)  /* We don't own dumb buffers, for now... */
    drmModeRmFB (framebuffer_kms->drm_fd, framebuffer_kms->fb_id);

  if (framebuffer_kms->gbm_surface != NULL && framebuffer_kms->gbm_bo)
    gbm_surface_release_buffer (framebuffer_kms->gbm_surface,
                                framebuffer_kms->gbm_bo);

  framebuffer_kms->fb_id = INVALID_FB_ID;
  framebuffer_kms->gbm_bo = NULL;
}
