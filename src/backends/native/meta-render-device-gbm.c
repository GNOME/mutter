/*
 * Copyright (C) 2016-2021 Red Hat Inc.
 * Copyright (c) 2018-2019 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-render-device-gbm.h"

#include <gbm.h>

#include "backends/meta-backend-private.h"
#include "backends/native/meta-drm-buffer-gbm.h"
#include "backends/native/meta-drm-buffer-import.h"

struct _MetaRenderDeviceGbm
{
  MetaRenderDevice parent;

  struct gbm_device *gbm_device;
};

static GInitableIface *initable_parent_iface;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaRenderDeviceGbm, meta_render_device_gbm,
                         META_TYPE_RENDER_DEVICE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static gboolean
meta_render_device_gbm_initable_init (GInitable     *initable,
                                      GCancellable  *cancellable,
                                      GError       **error)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (initable);
  MetaRenderDeviceGbm *render_device_gbm = META_RENDER_DEVICE_GBM (initable);
  MetaDeviceFile *device_file =
    meta_render_device_get_device_file (render_device);
  struct gbm_device *gbm_device;

  gbm_device = gbm_create_device (meta_device_file_get_fd (device_file));
  if (!gbm_device)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create gbm device: %s", g_strerror (errno));
      return FALSE;
    }

  render_device_gbm->gbm_device = gbm_device;

  return initable_parent_iface->init (initable, cancellable, error);
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_parent_iface = g_type_interface_peek_parent (initable_iface);

  initable_iface->init = meta_render_device_gbm_initable_init;
}

static MetaDrmBuffer *
meta_render_device_gbm_import_dma_buf (MetaRenderDevice  *render_device,
                                       MetaDrmBuffer     *buffer,
                                       GError           **error)
{
  MetaRenderDeviceGbm *render_device_gbm =
    META_RENDER_DEVICE_GBM (render_device);
  MetaDeviceFile *device_file;
  MetaDrmBufferGbm *buffer_gbm;
  MetaDrmBufferImport *buffer_import;

  if (!META_IS_DRM_BUFFER_GBM (buffer))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Can only import gbm backed DMA buffers");
      return NULL;
    }

  device_file = meta_render_device_get_device_file (render_device);
  buffer_gbm = META_DRM_BUFFER_GBM (buffer);
  buffer_import = meta_drm_buffer_import_new (device_file,
                                              render_device_gbm->gbm_device,
                                              buffer_gbm,
                                              error);
  if (!buffer_import)
    return NULL;

  return META_DRM_BUFFER (buffer_import);
}

static EGLDisplay
meta_render_device_gbm_create_egl_display (MetaRenderDevice  *render_device,
                                           GError           **error)
{
  MetaRenderDeviceGbm *render_device_gbm =
    META_RENDER_DEVICE_GBM (render_device);
  MetaBackend *backend = meta_render_device_get_backend (render_device);
  MetaEgl *egl = meta_backend_get_egl (backend);
  EGLDisplay egl_display;

  if (!meta_egl_has_extensions (egl, EGL_NO_DISPLAY, NULL,
                                "EGL_MESA_platform_gbm",
                                NULL) &&
      !meta_egl_has_extensions (egl, EGL_NO_DISPLAY, NULL,
                                "EGL_KHR_platform_gbm",
                                NULL))
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing extension for GBM renderer: EGL_KHR_platform_gbm");
      return EGL_NO_DISPLAY;
    }

  egl_display = meta_egl_get_platform_display (egl,
                                               EGL_PLATFORM_GBM_KHR,
                                               render_device_gbm->gbm_device,
                                               NULL, error);
  if (egl_display == EGL_NO_DISPLAY)
    return EGL_NO_DISPLAY;

  if (!meta_egl_initialize (egl, egl_display, error))
    {
      meta_egl_terminate (egl, egl_display, NULL);
      return EGL_NO_DISPLAY;
    }

  return egl_display;
}

static MetaDrmBuffer *
meta_render_device_gbm_allocate_dma_buf (MetaRenderDevice    *render_device,
                                         int                  width,
                                         int                  height,
                                         uint32_t             format,
                                         uint64_t            *modifiers,
                                         int                  n_modifiers,
                                         MetaDrmBufferFlags   flags,
                                         GError             **error)
{
  MetaRenderDeviceGbm *render_device_gbm =
    META_RENDER_DEVICE_GBM (render_device);
  MetaDeviceFile *device_file;
  struct gbm_bo *gbm_bo;
  MetaDrmBufferGbm *buffer_gbm;

  if (n_modifiers == 0)
    {
      gbm_bo = gbm_bo_create (render_device_gbm->gbm_device,
                              width, height, format,
                              GBM_BO_USE_RENDERING);
    }
  else
    {
      g_warn_if_fail (!(flags & META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS));
      gbm_bo = gbm_bo_create_with_modifiers2 (render_device_gbm->gbm_device,
                                              width, height, format,
                                              modifiers, n_modifiers,
                                              GBM_BO_USE_RENDERING);
    }

  if (!gbm_bo)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to gbm_bo: %s", g_strerror (errno));
      return NULL;
    }

  device_file = meta_render_device_get_device_file (render_device);
  buffer_gbm = meta_drm_buffer_gbm_new_take (device_file, gbm_bo, flags,
                                             error);
  return META_DRM_BUFFER (buffer_gbm);
}

static void
meta_render_device_gbm_finalize (GObject *object)
{
  MetaRenderDeviceGbm *render_device_gbm = META_RENDER_DEVICE_GBM (object);

  g_clear_pointer (&render_device_gbm->gbm_device, gbm_device_destroy);

  G_OBJECT_CLASS (meta_render_device_gbm_parent_class)->finalize (object);
}

static void
meta_render_device_gbm_class_init (MetaRenderDeviceGbmClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaRenderDeviceClass *render_device_class = META_RENDER_DEVICE_CLASS (klass);

  object_class->finalize = meta_render_device_gbm_finalize;

  render_device_class->create_egl_display =
    meta_render_device_gbm_create_egl_display;
  render_device_class->allocate_dma_buf =
    meta_render_device_gbm_allocate_dma_buf;
  render_device_class->import_dma_buf =
    meta_render_device_gbm_import_dma_buf;
}

static void
meta_render_device_gbm_init (MetaRenderDeviceGbm *render_device_gbm)
{
}

MetaRenderDeviceGbm *
meta_render_device_gbm_new (MetaBackend     *backend,
                            MetaDeviceFile  *device_file,
                            GError         **error)
{
  return g_initable_new (META_TYPE_RENDER_DEVICE_GBM,
                         NULL, error,
                         "backend", backend,
                         "device-file", device_file,
                         NULL);
}

struct gbm_device *
meta_render_device_gbm_get_gbm_device (MetaRenderDeviceGbm *render_device_gbm)
{
  return render_device_gbm->gbm_device;
}
