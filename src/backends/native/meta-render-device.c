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
#include "backends/meta-egl.h"
#include "backends/native/meta-backend-native-types.h"
#include "backends/native/meta-drm-buffer-dumb.h"

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

  EGLDisplay egl_display;
  EGLConfig egl_config;

  gboolean is_hardware_rendering;
} MetaRenderDevicePrivate;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (MetaRenderDevice, meta_render_device,
                                  G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (MetaRenderDevice)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         initable_iface_init))

static EGLDisplay
meta_render_device_create_egl_display (MetaRenderDevice  *render_device,
                                       GError           **error)
{
  MetaRenderDeviceClass *klass = META_RENDER_DEVICE_GET_CLASS (render_device);

  return klass->create_egl_display (render_device, error);
}

static void
detect_hardware_rendering (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);
  MetaEgl *egl = meta_backend_get_egl (priv->backend);
  g_autoptr (GError) error = NULL;
  EGLint *attributes;
  EGLContext egl_context;
  const char *renderer_str;

  attributes = (EGLint[]) {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };
  egl_context = meta_egl_create_context (egl,
                                         priv->egl_display,
                                         EGL_NO_CONFIG_KHR,
                                         EGL_NO_CONTEXT,
                                         attributes,
                                         &error);
  if (egl_context == EGL_NO_CONTEXT)
    {
      meta_topic (META_DEBUG_RENDER, "Failed to create EGLContext for %s: %s",
                  meta_device_file_get_path (priv->device_file),
                  error->message);
      return;
    }

  if (!meta_egl_make_current (egl,
                              priv->egl_display,
                              EGL_NO_SURFACE,
                              EGL_NO_SURFACE,
                              egl_context,
                              &error))
    {
      g_warning ("Failed to detect hardware rendering: eglMakeCurrent(): %s",
                 error->message);
      goto out_has_context;
    }

  renderer_str = (const char *) glGetString (GL_RENDERER);
  if (g_str_has_prefix (renderer_str, "llvmpipe") ||
      g_str_has_prefix (renderer_str, "softpipe") ||
      g_str_has_prefix (renderer_str, "swrast"))
    goto out_current_context;

  priv->is_hardware_rendering = TRUE;

out_current_context:
  meta_egl_make_current (egl, priv->egl_display,
                         EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT, NULL);

out_has_context:
  meta_egl_destroy_context (egl, priv->egl_display, egl_context, NULL);
}

static void
init_egl (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);
  MetaEgl *egl = meta_backend_get_egl (priv->backend);
  g_autoptr (GError) error = NULL;
  EGLDisplay egl_display;

  meta_egl_bind_api (egl, EGL_OPENGL_ES_API, NULL);

  egl_display = meta_render_device_create_egl_display (render_device, &error);
  if (egl_display == EGL_NO_DISPLAY)
    {
      meta_topic (META_DEBUG_RENDER, "Failed to create EGLDisplay for %s: %s",
                  meta_device_file_get_path (priv->device_file),
                  error->message);
      return;
    }

  priv->egl_display = egl_display;
  detect_hardware_rendering (render_device);
}

static gboolean
meta_render_device_initable_init (GInitable     *initable,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (initable);

  init_egl (render_device);

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
meta_render_device_dispose (GObject *object)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (object);
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);
  MetaEgl *egl = meta_backend_get_egl (priv->backend);

  if (priv->egl_display != EGL_NO_DISPLAY)
    {
      meta_egl_terminate (egl, priv->egl_display, NULL);
      priv->egl_display = EGL_NO_DISPLAY;
    }

  G_OBJECT_CLASS (meta_render_device_parent_class)->dispose (object);
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

static void
meta_render_device_class_init (MetaRenderDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = meta_render_device_get_property;
  object_class->set_property = meta_render_device_set_property;
  object_class->constructed = meta_render_device_constructed;
  object_class->dispose = meta_render_device_dispose;
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
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  priv->egl_display = EGL_NO_DISPLAY;
  priv->egl_config = EGL_NO_CONFIG_KHR;
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

EGLDisplay
meta_render_device_get_egl_display (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  return priv->egl_display;
}

gboolean
meta_render_device_is_hardware_accelerated (MetaRenderDevice *render_device)
{
  MetaRenderDevicePrivate *priv =
    meta_render_device_get_instance_private (render_device);

  return priv->is_hardware_rendering;
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
