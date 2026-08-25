/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include "backends/native/meta-render-device-private.h"

#include "backends/meta-backend-private.h"
#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-drm-buffer-dumb.h"
#include "common/meta-cogl-drm-formats.h"
#include "backends/native/meta-renderer-native-private.h"
#include "cogl/cogl-context-private.h"

enum
{
  PROP_0,

  PROP_BACKEND,
  PROP_DEVICE_FILE,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaRenderDevicePrivate
{
  MetaBackend *backend;

  MetaDeviceFile *device_file;

  MetaRendererNativeSecondaryGpuData secondary_gpu_data;

  MetaGpuKms *gpu_kms;

  gulong crtc_needs_flush_handler_id;
} MetaRenderDevicePrivate;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaRenderDevice, meta_render_device,
                                  COGL_TYPE_RENDER_DEVICE,
                                  G_ADD_PRIVATE (MetaRenderDevice)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         initable_iface_init))

static gboolean
meta_render_device_initable_init (GInitable     *initable,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (initable);
  MetaRenderDeviceClass *klass = META_RENDER_DEVICE_GET_CLASS (render_device);
  g_autoptr (CoglRenderer) renderer = NULL;

  renderer = klass->create_renderer (render_device, error);
  if (!renderer)
    {
      meta_topic (META_DEBUG_RENDER, "Failed to create renderer for %s: %s",
                  meta_render_device_get_name (render_device),
                  (*error)->message);
      return FALSE;
    }

  cogl_render_device_set_renderer (COGL_RENDER_DEVICE (render_device),
                                    renderer);

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = meta_render_device_initable_init;
}

static void
meta_render_device_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (object);
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    case PROP_DEVICE_FILE:
      g_value_set_pointer (value, priv->device_file);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_render_device_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (object);
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    case PROP_DEVICE_FILE:
      priv->device_file = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_render_device_finalize (GObject *object)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (object);
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  g_clear_pointer (&priv->device_file, meta_device_file_release);

  G_OBJECT_CLASS (meta_render_device_parent_class)->finalize (object);
}

static void
meta_render_device_constructed (GObject *object)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (object);
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  if (priv->device_file)
    meta_device_file_acquire (priv->device_file);

  G_OBJECT_CLASS (meta_render_device_parent_class)->constructed (object);
}

static GArray *
meta_render_device_query_drm_modifiers_impl (CoglRenderDevice       *cogl_render_device,
                                             CoglPixelFormat         format,
                                             CoglDrmModifierFilter   filter,
                                             GError                **error)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (cogl_render_device);
  MetaRenderDeviceClass *klass = META_RENDER_DEVICE_GET_CLASS (render_device);
  const MetaFormatInfo *format_info;

  format_info = meta_format_info_from_cogl_format (format);
  if (!format_info)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Format %s not supported",
                   cogl_pixel_format_to_string (format));
      return NULL;
    }

  if (klass->query_drm_modifiers)
    return klass->query_drm_modifiers (render_device,
                                       format_info->drm_format,
                                       filter, error);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Render device '%s' doesn't support querying DRM modifiers",
               meta_render_device_get_name (render_device));

  return NULL;
}

static uint64_t
meta_render_device_get_implicit_drm_modifier_impl (CoglRenderDevice *cogl_render_device)
{
  return DRM_FORMAT_MOD_INVALID;
}

static gboolean
meta_render_device_is_dma_buf_supported_impl (CoglRenderDevice *cogl_render_device)
{
  switch (cogl_render_device_get_mode (cogl_render_device))
    {
    case COGL_RENDER_DEVICE_MODE_GBM:
      return cogl_render_device_is_hardware_accelerated (cogl_render_device);
    case COGL_RENDER_DEVICE_MODE_SURFACELESS:
      return FALSE;
    }

  g_assert_not_reached ();
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
meta_render_device_create_dma_buf_impl (CoglRenderDevice  *cogl_render_device,
                                        CoglPixelFormat    format,
                                        uint64_t          *modifiers,
                                        int                n_modifiers,
                                        int                width,
                                        int                height,
                                        GError           **error)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (cogl_render_device);
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);
  MetaBackend *backend = priv->backend;
  MetaRendererNative *renderer_native =
    META_RENDERER_NATIVE (meta_backend_get_renderer (backend));

  switch (cogl_render_device_get_mode (cogl_render_device))
    {
    case COGL_RENDER_DEVICE_MODE_GBM:
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
        buffer = meta_render_device_allocate_dma_buf (render_device,
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
          cogl_renderer_create_dma_buf_framebuffer (cogl_render_device_get_renderer (cogl_render_device),
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
    case COGL_RENDER_DEVICE_MODE_SURFACELESS:
      break;
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
               "Current mode does not support exporting DMA buffers");

  return NULL;
}

static void
meta_render_device_class_init (MetaRenderDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CoglRenderDeviceClass *render_device_class =
    COGL_RENDER_DEVICE_CLASS (klass);

  render_device_class->query_drm_modifiers =
    meta_render_device_query_drm_modifiers_impl;
  render_device_class->get_implicit_drm_modifier =
    meta_render_device_get_implicit_drm_modifier_impl;
  render_device_class->is_dma_buf_supported =
    meta_render_device_is_dma_buf_supported_impl;
  render_device_class->create_dma_buf =
    meta_render_device_create_dma_buf_impl;

  object_class->get_property = meta_render_device_get_property;
  object_class->set_property = meta_render_device_set_property;
  object_class->constructed = meta_render_device_constructed;
  object_class->finalize = meta_render_device_finalize;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_DEVICE_FILE] =
    g_param_spec_pointer ("device-file", NULL, NULL,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_render_device_init (MetaRenderDevice *render_device)
{
}

MetaBackend *
meta_render_device_get_backend (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  return priv->backend;
}

MetaDeviceFile *
meta_render_device_get_device_file (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  return priv->device_file;
}

const char *
meta_render_device_get_name (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  if (priv->device_file)
    return meta_device_file_get_path (priv->device_file);
  else
    return "(device-less)";
}

GArray *
meta_render_device_query_drm_modifiers (MetaRenderDevice       *render_device,
                                        uint32_t                drm_format,
                                        CoglDrmModifierFilter   filter,
                                        GError                **error)
{
  MetaRenderDeviceClass *klass = META_RENDER_DEVICE_GET_CLASS (render_device);

  if (klass->query_drm_modifiers)
    {
      return klass->query_drm_modifiers (render_device,
                                         drm_format, filter,
                                         error);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Render device '%s' doesn't support allocating DMA buffers",
               meta_render_device_get_name (render_device));

  return NULL;
}

MetaDrmBuffer *
meta_render_device_allocate_dma_buf (MetaRenderDevice    *render_device,
                                     int                  width,
                                     int                  height,
                                     uint32_t             format,
                                     uint64_t            *modifiers,
                                     int                  n_modifiers,
                                     MetaDrmBufferFlags   flags,
                                     GError             **error)
{
  MetaRenderDeviceClass *klass = META_RENDER_DEVICE_GET_CLASS (render_device);

  if (klass->allocate_dma_buf)
    {
      return klass->allocate_dma_buf (render_device,
                                      width, height,
                                      format,
                                      modifiers, n_modifiers,
                                      flags,
                                      error);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Render device '%s' doesn't support allocating DMA buffers",
               meta_render_device_get_name (render_device));

  return NULL;
}

MetaDrmBuffer *
meta_render_device_import_dma_buf (MetaRenderDevice  *render_device,
                                   MetaDrmBuffer     *buffer,
                                   GError           **error)
{
  MetaRenderDeviceClass *klass = META_RENDER_DEVICE_GET_CLASS (render_device);

  if (klass->import_dma_buf)
    return klass->import_dma_buf (render_device, buffer, error);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Render device '%s' doesn't importing DMA buffers",
               meta_render_device_get_name (render_device));

  return NULL;
}

MetaDrmBuffer *
meta_render_device_allocate_dumb_buf (MetaRenderDevice  *render_device,
                                      int                width,
                                      int                height,
                                      uint32_t           format,
                                      GError           **error)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);
  MetaDrmBufferDumb *buffer_dumb;

  if (!priv->device_file)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "No device file to allocate from");
      return NULL;
    }

  buffer_dumb = meta_drm_buffer_dumb_new (priv->device_file,
                                          width, height,
                                          format,
                                          error);
  if (!buffer_dumb)
    return NULL;

  return META_DRM_BUFFER (buffer_dumb);
}

MetaRendererNativeSecondaryGpuData *
meta_render_device_get_secondary_gpu_data (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  return &priv->secondary_gpu_data;
}

MetaGpuKms *
meta_render_device_get_gpu_kms (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  return priv->gpu_kms;
}

void
meta_render_device_set_gpu_kms (MetaRenderDevice *render_device,
                                MetaGpuKms       *gpu_kms)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  priv->gpu_kms = gpu_kms;
}

gulong *
meta_render_device_get_crtc_needs_flush_handler_id (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  return &priv->crtc_needs_flush_handler_id;
}
