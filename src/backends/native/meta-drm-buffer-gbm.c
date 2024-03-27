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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/native/meta-drm-buffer-gbm.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <gio/gio.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/meta-backend-private.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-drm-buffer-private.h"
#include "common/meta-cogl-drm-formats.h"

struct _MetaDrmBufferGbm
{
  MetaDrmBuffer parent;

  struct gbm_surface *surface;

  struct gbm_bo *bo;
};

static void
cogl_scanout_buffer_iface_init (CoglScanoutBufferInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaDrmBufferGbm, meta_drm_buffer_gbm, META_TYPE_DRM_BUFFER,
                         G_IMPLEMENT_INTERFACE (COGL_TYPE_SCANOUT_BUFFER,
                                                cogl_scanout_buffer_iface_init))

struct gbm_bo *
meta_drm_buffer_gbm_get_bo (MetaDrmBufferGbm *buffer_gbm)
{
  return buffer_gbm->bo;
}

static int
meta_drm_buffer_gbm_export_fd (MetaDrmBuffer  *buffer,
                               GError        **error)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (buffer);
  int fd;

  fd = gbm_bo_get_fd (buffer_gbm->bo);
  if (fd == -1)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to export buffer fd: %s", g_strerror (errno));
      return -1;
    }

  return fd;
}

static int
meta_drm_buffer_gbm_get_width (MetaDrmBuffer *buffer)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (buffer);

  return gbm_bo_get_width (buffer_gbm->bo);
}

static int
meta_drm_buffer_gbm_get_height (MetaDrmBuffer *buffer)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (buffer);

  return gbm_bo_get_height (buffer_gbm->bo);
}

static int
meta_drm_buffer_gbm_get_stride (MetaDrmBuffer *buffer)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (buffer);

  return gbm_bo_get_stride (buffer_gbm->bo);
}

static int
meta_drm_buffer_gbm_get_bpp (MetaDrmBuffer *buffer)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (buffer);

  return gbm_bo_get_bpp (buffer_gbm->bo);
}

static uint32_t
meta_drm_buffer_gbm_get_format (MetaDrmBuffer *buffer)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (buffer);

  return gbm_bo_get_format (buffer_gbm->bo);
}

static int
meta_drm_buffer_gbm_get_offset (MetaDrmBuffer *buffer,
                                int            plane)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (buffer);

  return gbm_bo_get_offset (buffer_gbm->bo, plane);
}

static uint64_t
meta_drm_buffer_gbm_get_modifier (MetaDrmBuffer *buffer)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (buffer);

  return gbm_bo_get_modifier (buffer_gbm->bo);
}

static gboolean
meta_drm_buffer_gbm_ensure_fb_id (MetaDrmBuffer  *buffer,
                                  GError        **error)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (buffer);
  MetaDrmFbArgs fb_args = { 0, };
  struct gbm_bo *bo = buffer_gbm->bo;

  if (gbm_bo_get_handle_for_plane (bo, 0).s32 == -1)
    {
      /* Failed to fetch handle to plane, falling back to old method */
      fb_args.strides[0] = gbm_bo_get_stride (bo);
      fb_args.handles[0] = gbm_bo_get_handle (bo).u32;
      fb_args.offsets[0] = 0;
      fb_args.modifiers[0] = DRM_FORMAT_MOD_INVALID;
    }
  else
    {
      int i;

      for (i = 0; i < gbm_bo_get_plane_count (bo); i++)
        {
          fb_args.strides[i] = gbm_bo_get_stride_for_plane (bo, i);
          fb_args.handles[i] = gbm_bo_get_handle_for_plane (bo, i).u32;
          fb_args.offsets[i] = gbm_bo_get_offset (bo, i);
          fb_args.modifiers[i] = gbm_bo_get_modifier (bo);
        }
     }

  fb_args.width = gbm_bo_get_width (bo);
  fb_args.height = gbm_bo_get_height (bo);
  fb_args.format = gbm_bo_get_format (bo);
  fb_args.handle = gbm_bo_get_handle (bo).u32;

  if (!meta_drm_buffer_do_ensure_fb_id (META_DRM_BUFFER (buffer_gbm),
                                        &fb_args, error))
    return FALSE;

  return TRUE;
}

static gboolean
lock_front_buffer (MetaDrmBufferGbm  *buffer_gbm,
                   GError           **error)
{
  buffer_gbm->bo = gbm_surface_lock_front_buffer (buffer_gbm->surface);
  if (!buffer_gbm->bo)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "gbm_surface_lock_front_buffer failed");
      return FALSE;
    }

  return TRUE;
}

MetaDrmBufferGbm *
meta_drm_buffer_gbm_new_lock_front (MetaDeviceFile      *device_file,
                                    struct gbm_surface  *gbm_surface,
                                    MetaDrmBufferFlags   flags,
                                    GError             **error)
{
  MetaDrmBufferGbm *buffer_gbm;

  buffer_gbm = g_object_new (META_TYPE_DRM_BUFFER_GBM,
                             "device-file", device_file,
                             "flags", flags,
                             NULL);
  buffer_gbm->surface = gbm_surface;

  if (!lock_front_buffer (buffer_gbm, error))
    {
      g_object_unref (buffer_gbm);
      return NULL;
    }

  return buffer_gbm;
}

MetaDrmBufferGbm *
meta_drm_buffer_gbm_new_take (MetaDeviceFile      *device_file,
                              struct gbm_bo       *bo,
                              MetaDrmBufferFlags   flags,
                              GError             **error)
{
  MetaDrmBufferGbm *buffer_gbm;

  buffer_gbm = g_object_new (META_TYPE_DRM_BUFFER_GBM,
                             "device-file", device_file,
                             "flags", flags,
                             NULL);
  buffer_gbm->bo = bo;

  return buffer_gbm;
}

static gboolean
meta_drm_buffer_gbm_blit_to_framebuffer (CoglScanout      *scanout,
                                         CoglFramebuffer  *framebuffer,
                                         int               x,
                                         int               y,
                                         GError          **error)
{
  CoglScanoutBuffer *scanout_buffer = cogl_scanout_get_buffer (scanout);
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (scanout_buffer);
  MetaDrmBuffer *buffer = META_DRM_BUFFER (buffer_gbm);
  MetaDeviceFile *device_file = meta_drm_buffer_get_device_file (buffer);
  MetaDevicePool *device_pool = meta_device_file_get_pool (device_file);
  MetaBackend *backend = meta_device_pool_get_backend (device_pool);
  MetaEgl *egl = meta_backend_get_egl (backend);
  ClutterBackend *clutter_backend =
    meta_backend_get_clutter_backend (backend);
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  CoglDisplay *cogl_display = cogl_context->display;
  CoglRenderer *cogl_renderer = cogl_display->renderer;
  CoglRendererEGL *cogl_renderer_egl = cogl_renderer->winsys;
  EGLDisplay egl_display = cogl_renderer_egl->edpy;
  EGLImageKHR egl_image;
  CoglPixelFormat cogl_format;
  CoglEglImageFlags flags;
  CoglOffscreen *cogl_fbo = NULL;
  CoglTexture *cogl_tex;
  uint32_t n_planes;
  uint64_t *modifiers;
  uint32_t *strides;
  uint32_t *offsets;
  uint32_t width;
  uint32_t height;
  uint32_t drm_format;
  int *fds;
  gboolean result;
  int dmabuf_fd = -1;
  uint32_t i;
  const MetaFormatInfo *format_info;

  dmabuf_fd = gbm_bo_get_fd (buffer_gbm->bo);
  if (dmabuf_fd == -1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                   "Failed to export buffer's DMA fd: %s",
                   g_strerror (errno));
      return FALSE;
    }

  drm_format = gbm_bo_get_format (buffer_gbm->bo);

  format_info = meta_format_info_from_drm_format (drm_format);
  g_assert (format_info);
  cogl_format = format_info->cogl_format;

  width = gbm_bo_get_width (buffer_gbm->bo);
  height = gbm_bo_get_height (buffer_gbm->bo);
  n_planes = gbm_bo_get_plane_count (buffer_gbm->bo);
  fds = g_alloca (sizeof (int) * n_planes);
  strides = g_alloca (sizeof (uint32_t) * n_planes);
  offsets = g_alloca (sizeof (uint32_t) * n_planes);
  modifiers = g_alloca (sizeof (uint64_t) * n_planes);

  for (i = 0; i < n_planes; i++)
    {
      fds[i] = dmabuf_fd;
      strides[i] = gbm_bo_get_stride_for_plane (buffer_gbm->bo, i);
      offsets[i] = gbm_bo_get_offset (buffer_gbm->bo, i);
      modifiers[i] = gbm_bo_get_modifier (buffer_gbm->bo);
    }

  egl_image = meta_egl_create_dmabuf_image (egl,
                                            egl_display,
                                            width,
                                            height,
                                            drm_format,
                                            n_planes,
                                            fds,
                                            strides,
                                            offsets,
                                            modifiers,
                                            error);
  if (egl_image == EGL_NO_IMAGE_KHR)
    {
      result = FALSE;
      goto out;
    }

  flags = COGL_EGL_IMAGE_FLAG_NO_GET_DATA;
  cogl_tex = cogl_egl_texture_2d_new_from_image (cogl_context,
                                                 width,
                                                 height,
                                                 cogl_format,
                                                 egl_image,
                                                 flags,
                                                 error);

  meta_egl_destroy_image (egl, egl_display, egl_image, NULL);

  if (!cogl_tex)
    {
      result = FALSE;
      goto out;
    }

  cogl_fbo = cogl_offscreen_new_with_texture (cogl_tex);
  g_object_unref (cogl_tex);

  if (!cogl_framebuffer_allocate (COGL_FRAMEBUFFER (cogl_fbo), error))
    {
      result = FALSE;
      goto out;
    }

  result = cogl_blit_framebuffer (COGL_FRAMEBUFFER (cogl_fbo),
                                  framebuffer,
                                  0, 0,
                                  x, y,
                                  width, height,
                                  error);

out:
  g_clear_object (&cogl_fbo);
  close (dmabuf_fd);

  return result;
}

static int
meta_drm_buffer_gbm_scanout_get_width (CoglScanoutBuffer *scanout_buffer)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (scanout_buffer);

  return meta_drm_buffer_get_width (buffer);
}

static int
meta_drm_buffer_gbm_scanout_get_height (CoglScanoutBuffer *scanout_buffer)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (scanout_buffer);

  return meta_drm_buffer_get_height (buffer);
}

static void
cogl_scanout_buffer_iface_init (CoglScanoutBufferInterface *iface)
{
  iface->blit_to_framebuffer = meta_drm_buffer_gbm_blit_to_framebuffer;
  iface->get_width = meta_drm_buffer_gbm_scanout_get_width;
  iface->get_height = meta_drm_buffer_gbm_scanout_get_height;
}

static void
meta_drm_buffer_gbm_finalize (GObject *object)
{
  MetaDrmBufferGbm *buffer_gbm = META_DRM_BUFFER_GBM (object);

  if (buffer_gbm->bo)
    {
      if (buffer_gbm->surface)
        gbm_surface_release_buffer (buffer_gbm->surface, buffer_gbm->bo);
      else
        gbm_bo_destroy (buffer_gbm->bo);
    }

  G_OBJECT_CLASS (meta_drm_buffer_gbm_parent_class)->finalize (object);
}

static void
meta_drm_buffer_gbm_init (MetaDrmBufferGbm *buffer_gbm)
{
}

static void
meta_drm_buffer_gbm_class_init (MetaDrmBufferGbmClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaDrmBufferClass *buffer_class = META_DRM_BUFFER_CLASS (klass);

  object_class->finalize = meta_drm_buffer_gbm_finalize;

  buffer_class->export_fd = meta_drm_buffer_gbm_export_fd;
  buffer_class->ensure_fb_id = meta_drm_buffer_gbm_ensure_fb_id;
  buffer_class->get_width = meta_drm_buffer_gbm_get_width;
  buffer_class->get_height = meta_drm_buffer_gbm_get_height;
  buffer_class->get_stride = meta_drm_buffer_gbm_get_stride;
  buffer_class->get_bpp = meta_drm_buffer_gbm_get_bpp;
  buffer_class->get_format = meta_drm_buffer_gbm_get_format;
  buffer_class->get_offset = meta_drm_buffer_gbm_get_offset;
  buffer_class->get_modifier = meta_drm_buffer_gbm_get_modifier;
}
