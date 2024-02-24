/*
 * Copyright (C) 2016-2021 Red Hat Inc.
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

#include "backends/native/meta-render-device-egl-stream.h"

#include "backends/meta-backend-private.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms.h"

struct _MetaRenderDeviceEglStream
{
  MetaRenderDevice parent;

  gboolean inhibited_kms_kernel_thread;

  EGLDeviceEXT egl_device;
};

static GInitableIface *initable_parent_iface;

static void
initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (MetaRenderDeviceEglStream, meta_render_device_egl_stream,
                         META_TYPE_RENDER_DEVICE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static EGLDisplay
get_egl_device_display (MetaRenderDevice  *render_device,
                        EGLDeviceEXT       egl_device,
                        GError           **error)
{
  MetaBackend *backend = meta_render_device_get_backend (render_device);
  MetaEgl *egl = meta_backend_get_egl (backend);
  MetaDeviceFile *device_file =
    meta_render_device_get_device_file (render_device);
  int kms_fd = meta_device_file_get_fd (device_file);
  EGLint platform_attribs[] = {
    EGL_DRM_MASTER_FD_EXT, kms_fd,
    EGL_NONE
  };

  return meta_egl_get_platform_display (egl, EGL_PLATFORM_DEVICE_EXT,
                                        (void *) egl_device,
                                        platform_attribs,
                                        error);
}

static gboolean
get_drm_device_file (MetaEgl       *egl,
                     EGLDeviceEXT   device,
                     const char   **out_device_file_path,
                     GError       **error)
{
  if (!meta_egl_egl_device_has_extensions (egl, device,
                                           NULL,
                                           "EGL_EXT_device_drm",
                                           NULL))
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing required EGLDevice extension EGL_EXT_device_drm");
      return FALSE;
    }

  return meta_egl_query_device_string (egl, device, EGL_DRM_DEVICE_FILE_EXT,
                                       out_device_file_path, error);
}

static EGLDeviceEXT
find_egl_device (MetaRenderDevice  *render_device,
                 GError           **error)
{
  MetaBackend *backend = meta_render_device_get_backend (render_device);
  MetaEgl *egl = meta_backend_get_egl (backend);
  g_autofree const char **missing_extensions = NULL;
  MetaDeviceFile *device_file =
    meta_render_device_get_device_file (render_device);
  EGLint num_devices;
  g_autofree EGLDeviceEXT *devices = NULL;
  const char *device_file_path;
  EGLDeviceEXT device;
  EGLint i;

  if (!meta_egl_has_extensions (egl,
                                EGL_NO_DISPLAY,
                                &missing_extensions,
                                "EGL_EXT_device_base",
                                NULL))
    {
      g_autofree char *missing_extensions_str = NULL;

      missing_extensions_str = g_strjoinv (", ", (char **) missing_extensions);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing EGL extensions required for EGLDevice renderer: %s",
                   missing_extensions_str);
      return EGL_NO_DEVICE_EXT;
    }

  if (!meta_egl_query_devices (egl, 0, NULL, &num_devices, error))
    return EGL_NO_DEVICE_EXT;

  devices = g_new0 (EGLDeviceEXT, num_devices);
  if (!meta_egl_query_devices (egl, num_devices, devices, &num_devices,
                               error))
    return EGL_NO_DEVICE_EXT;

  device_file_path = meta_device_file_get_path (device_file);

  device = EGL_NO_DEVICE_EXT;
  for (i = 0; i < num_devices; i++)
    {
      const char *egl_device_drm_path;

      g_clear_error (error);

      if (!get_drm_device_file (egl, devices[i], &egl_device_drm_path, error) ||
          !egl_device_drm_path)
        continue;

      if (g_str_equal (egl_device_drm_path, device_file_path))
        {
          device = devices[i];
          break;
        }
    }

  if (device == EGL_NO_DEVICE_EXT)
    {
      if (!*error)
        g_set_error (error, G_IO_ERROR,
                     G_IO_ERROR_FAILED,
                     "Failed to find matching EGLDeviceEXT");
      return EGL_NO_DEVICE_EXT;
    }

  return device;
}

static gboolean
meta_render_device_egl_stream_initable_init (GInitable     *initable,
                                             GCancellable  *cancellable,
                                             GError       **error)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (initable);
  MetaRenderDeviceEglStream *render_device_egl_stream =
    META_RENDER_DEVICE_EGL_STREAM (initable);
  MetaBackend *backend = meta_render_device_get_backend (render_device);
  MetaKms *kms;
  EGLDeviceEXT egl_device;
  EGLDisplay egl_display;

  egl_device = find_egl_device (render_device, error);
  if (egl_device == EGL_NO_DEVICE_EXT)
    return FALSE;

  render_device_egl_stream->egl_device = egl_device;

  if (!initable_parent_iface->init (initable, cancellable, error))
    return FALSE;

  egl_display = meta_render_device_get_egl_display (render_device);
  if (egl_display == EGL_NO_DISPLAY)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "EGLStream render device requires an EGL display");
      return FALSE;
    }

  kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  meta_kms_inhibit_kernel_thread (kms);
  render_device_egl_stream->inhibited_kms_kernel_thread = TRUE;

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_parent_iface = g_type_interface_peek_parent (initable_iface);

  initable_iface->init = meta_render_device_egl_stream_initable_init;
}

static EGLDisplay
meta_render_device_egl_stream_create_egl_display (MetaRenderDevice  *render_device,
                                                  GError           **error)
{
  MetaRenderDeviceEglStream *render_device_egl_stream =
    META_RENDER_DEVICE_EGL_STREAM (render_device);
  EGLDeviceEXT egl_device = render_device_egl_stream->egl_device;
  MetaBackend *backend = meta_render_device_get_backend (render_device);
  MetaEgl *egl = meta_backend_get_egl (backend);
  EGLDisplay egl_display;
  g_autofree const char **missing_extensions = NULL;

  egl_display = get_egl_device_display (render_device, egl_device, error);
  if (egl_display == EGL_NO_DISPLAY)
    return EGL_NO_DISPLAY;

  if (!meta_egl_initialize (egl, egl_display, error))
    {
      meta_egl_terminate (egl, egl_display, NULL);
      return EGL_NO_DISPLAY;
    }

  if (!meta_egl_has_extensions (egl,
                                egl_display,
                                &missing_extensions,
                                "EGL_NV_output_drm_flip_event",
                                "EGL_EXT_output_base",
                                "EGL_EXT_output_drm",
                                "EGL_KHR_stream",
                                "EGL_KHR_stream_producer_eglsurface",
                                "EGL_EXT_stream_consumer_egloutput",
                                "EGL_EXT_stream_acquire_mode",
                                NULL))
    {
      g_autofree char *missing_extensions_str = NULL;

      meta_egl_terminate (egl, egl_display, NULL);

      missing_extensions_str = g_strjoinv (", ", (char **) missing_extensions);
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Missing EGL extensions required for EGLDevice renderer: %s",
                   missing_extensions_str);
      meta_egl_terminate (egl, egl_display, NULL);
      return EGL_NO_DISPLAY;
    }

  return egl_display;
}

static void
meta_render_device_egl_stream_finalize (GObject *object)
{
  MetaRenderDevice *render_device = META_RENDER_DEVICE (object);
  MetaRenderDeviceEglStream *render_device_egl_stream =
    META_RENDER_DEVICE_EGL_STREAM (render_device);

  if (render_device_egl_stream->inhibited_kms_kernel_thread)
    {
      MetaBackend *backend = meta_render_device_get_backend (render_device);
      MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));

      meta_kms_uninhibit_kernel_thread (kms);
    }

  G_OBJECT_CLASS (meta_render_device_egl_stream_parent_class)->finalize (object);
}

static void
meta_render_device_egl_stream_class_init (MetaRenderDeviceEglStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaRenderDeviceClass *render_device_class = META_RENDER_DEVICE_CLASS (klass);

  object_class->finalize = meta_render_device_egl_stream_finalize;

  render_device_class->create_egl_display =
    meta_render_device_egl_stream_create_egl_display;
}

static void
meta_render_device_egl_stream_init (MetaRenderDeviceEglStream *render_device_egl_stream)
{
}

MetaRenderDeviceEglStream *
meta_render_device_egl_stream_new (MetaBackend     *backend,
                                   MetaDeviceFile  *device_file,
                                   GError         **error)
{
  return g_initable_new (META_TYPE_RENDER_DEVICE_EGL_STREAM,
                         NULL, error,
                         "backend", backend,
                         "device-file", device_file,
                         NULL);
}
