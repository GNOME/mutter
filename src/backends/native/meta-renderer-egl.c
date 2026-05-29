/*
 * Copyright (C) 2025 Red Hat Inc.
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
 */

#include "config.h"

#include "backends/native/meta-renderer-egl.h"
#include "backends/native/meta-drm-buffer.h"
#include "backends/native/meta-render-device-private.h"
#include "backends/native/meta-renderer-native.h"
#include "backends/native/meta-renderer-native-private.h"
#include "common/meta-cogl-drm-formats.h"

struct _MetaRendererEgl
{
  CoglRendererEGL parent_instance;

  MetaRenderDevice *render_device;
  MetaRendererNativeGpuData *renderer_gpu_data;
};

G_DEFINE_FINAL_TYPE (MetaRendererEgl, meta_renderer_egl, COGL_TYPE_RENDERER_EGL)

enum
{
  PROP_0,
  PROP_RENDER_DEVICE,
  N_PROPS
};

static GParamSpec *props[N_PROPS] = { NULL };

static void
meta_renderer_egl_get_property (GObject      *object,
                                unsigned int  prop_id,
                                GValue       *value,
                                GParamSpec   *pspec)
{
  MetaRendererEgl *renderer_egl = META_RENDERER_EGL (object);

  switch (prop_id)
    {
    case PROP_RENDER_DEVICE:
      g_value_set_object (value, renderer_egl->render_device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_renderer_egl_set_property (GObject      *object,
                                unsigned int  prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  MetaRendererEgl *renderer_egl = META_RENDERER_EGL (object);

  switch (prop_id)
    {
    case PROP_RENDER_DEVICE:
      renderer_egl->render_device = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static gboolean
meta_renderer_egl_connect (CoglRenderer  *cogl_renderer,
                           GError       **error)
{
  MetaRendererEgl *renderer_egl = META_RENDERER_EGL (cogl_renderer);
  MetaRenderDeviceClass *render_device_class =
    META_RENDER_DEVICE_GET_CLASS (renderer_egl->render_device);
  EGLDisplay egl_display;

  egl_display = render_device_class->create_egl_display (renderer_egl->render_device,
                                                         error);
  if (egl_display == EGL_NO_DISPLAY)
    return FALSE;

  cogl_renderer_egl_set_edisplay (COGL_RENDERER_EGL (cogl_renderer),
                                  egl_display);

  if (!COGL_RENDERER_CLASS (meta_renderer_egl_parent_class)->connect (cogl_renderer, error))
    return FALSE;

  return TRUE;
}

static GArray *
meta_renderer_egl_query_drm_modifiers (CoglRenderer           *cogl_renderer,
                                       CoglPixelFormat         format,
                                       CoglDrmModifierFilter   filter,
                                       GError                **error)
{
  MetaRendererEgl *renderer_egl = META_RENDERER_EGL (cogl_renderer);
  const MetaFormatInfo *format_info;
  uint32_t drm_format;

  format_info = meta_format_info_from_cogl_format (format);
  if (!format_info)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Format %s not supported",
                   cogl_pixel_format_to_string (format));
      return NULL;
    }

  drm_format = format_info->drm_format;

  return meta_render_device_query_drm_modifiers (renderer_egl->render_device,
                                                 drm_format, filter, error);
}

static uint64_t
meta_renderer_egl_get_implicit_drm_modifier (CoglRenderer *renderer)
{
  return DRM_FORMAT_MOD_INVALID;
}

static void
close_fds (int *fds,
           int  n_fds)
{
  int i;

  for (i = 0; i < n_fds; i++)
    close (fds[i]);
}

static CoglDmaBufHandle *
meta_renderer_egl_create_dma_buf (CoglRenderer     *cogl_renderer,
                                  CoglPixelFormat   format,
                                  uint64_t         *modifiers,
                                  int               n_modifiers,
                                  int               width,
                                  int               height,
                                  GError          **error)
{
  MetaRendererEgl *renderer_egl = META_RENDERER_EGL (cogl_renderer);
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_egl_get_renderer_gpu_data (renderer_egl);
  MetaRendererNative *renderer_native = renderer_gpu_data->renderer_native;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      {
        MetaDrmBufferFlags flags;
        g_autoptr (MetaDrmBuffer) buffer = NULL;
        uint64_t buffer_modifier;
        int n_planes;
        int *fds;
        uint32_t *offsets;
        uint32_t *strides;
        uint64_t *plane_modifiers = NULL;
        uint32_t bpp;
        uint32_t drm_format;
        int i;
        g_autoptr (CoglFramebuffer) dmabuf_fb = NULL;
        CoglDmaBufHandle *dmabuf_handle;
        const MetaFormatInfo *format_info;

        format_info = meta_format_info_from_cogl_format (format);
        if (!format_info)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                         "Native renderer doesn't support creating DMA buffer with format %s",
                         cogl_pixel_format_to_string (format));
            return NULL;
          }

        drm_format = format_info->drm_format;
        flags = META_DRM_BUFFER_FLAG_NONE;
        buffer = meta_render_device_allocate_dma_buf (renderer_egl->render_device,
                                                      width, height,
                                                      drm_format,
                                                      modifiers, n_modifiers,
                                                      flags,
                                                      error);
        if (!buffer)
          return NULL;

        buffer_modifier = meta_drm_buffer_get_modifier (buffer);
        bpp = meta_drm_buffer_get_bpp (buffer);

        n_planes = meta_drm_buffer_get_n_planes (buffer);
        fds = g_newa (int, n_planes);
        offsets = g_newa (uint32_t, n_planes);
        strides = g_newa (uint32_t, n_planes);

        if (n_modifiers > 0)
          plane_modifiers = g_newa (uint64_t, n_planes);

        for (i = 0; i < n_planes; i++)
          {
            fds[i] = meta_drm_buffer_export_fd_for_plane (buffer, i, error);
            if (fds[i] == -1)
              {
                close_fds (fds, i);
                return NULL;
              }

            offsets[i] = meta_drm_buffer_get_offset_for_plane (buffer, i);
            strides[i] = meta_drm_buffer_get_stride_for_plane (buffer, i);
            if (n_modifiers > 0)
              plane_modifiers[i] = buffer_modifier;
          }

        dmabuf_fb =
          cogl_renderer_create_dma_buf_framebuffer (cogl_renderer,
                                                    meta_renderer_native_get_cogl_context (renderer_native),
                                                    width,
                                                    height,
                                                    drm_format,
                                                    format,
                                                    n_planes,
                                                    fds,
                                                    strides,
                                                    offsets,
                                                    plane_modifiers,
                                                    error);
        if (!dmabuf_fb)
          {
            close_fds (fds, n_planes);
            return NULL;
          }

        dmabuf_handle =
          cogl_dma_buf_handle_new (dmabuf_fb,
                                   width, height,
                                   format,
                                   buffer_modifier,
                                   n_planes,
                                   fds,
                                   strides,
                                   offsets,
                                   bpp,
                                   g_steal_pointer (&buffer),
                                   g_object_unref);
        return dmabuf_handle;
      }
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      break;
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
               "Current mode does not support exporting DMA buffers");

  return NULL;
}

static gboolean
meta_renderer_egl_is_dma_buf_supported (CoglRenderer *cogl_renderer)
{
  MetaRendererEgl *renderer_egl = META_RENDERER_EGL (cogl_renderer);
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_egl_get_renderer_gpu_data (renderer_egl);

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      return meta_render_device_is_hardware_accelerated (renderer_egl->render_device);
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      return FALSE;
    }

  g_assert_not_reached ();
}

static void
meta_renderer_egl_class_init (MetaRendererEglClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CoglRendererClass *renderer_class =
    COGL_RENDERER_CLASS (object_class);

  object_class->get_property = meta_renderer_egl_get_property;
  object_class->set_property = meta_renderer_egl_set_property;

  renderer_class->connect = meta_renderer_egl_connect;
  renderer_class->query_drm_modifiers = meta_renderer_egl_query_drm_modifiers;
  renderer_class->get_implicit_drm_modifier = meta_renderer_egl_get_implicit_drm_modifier;
  renderer_class->create_dma_buf = meta_renderer_egl_create_dma_buf;
  renderer_class->is_dma_buf_supported = meta_renderer_egl_is_dma_buf_supported;

  props[PROP_RENDER_DEVICE] =
    g_param_spec_object ("render-device", NULL, NULL,
                         META_TYPE_RENDER_DEVICE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_renderer_egl_init (MetaRendererEgl *renderer_egl)
{
}

MetaRendererEgl *
meta_renderer_egl_new (MetaRenderDevice *render_device)
{
  return g_object_new (META_TYPE_RENDERER_EGL,
                       "render-device", render_device,
                       NULL);
}

void
meta_renderer_egl_set_renderer_gpu_data (MetaRendererEgl           *renderer_egl,
                                         MetaRendererNativeGpuData *renderer_gpu_data)
{
  g_return_if_fail (META_IS_RENDERER_EGL (renderer_egl));

  renderer_egl->renderer_gpu_data = renderer_gpu_data;
}

MetaRendererNativeGpuData *
meta_renderer_egl_get_renderer_gpu_data (MetaRendererEgl *renderer_egl)
{
  g_return_val_if_fail (META_IS_RENDERER_EGL (renderer_egl), NULL);

  return renderer_egl->renderer_gpu_data;
}
