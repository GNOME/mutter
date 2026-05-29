/*
 * Copyright (C) 2020-2021 Red Hat Inc.
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

#include "backends/native/meta-render-device-surfaceless.h"

#include "backends/native/meta-render-device-private.h"
#include "cogl/cogl.h"

struct _MetaRenderDeviceSurfaceless
{
  MetaRenderDevice parent;
};

G_DEFINE_TYPE (MetaRenderDeviceSurfaceless, meta_render_device_surfaceless,
               META_TYPE_RENDER_DEVICE)

static EGLDisplay
meta_render_device_surfaceless_create_egl_display (MetaRenderDevice  *render_device,
                                                   GError           **error)
{
  CoglRendererEGL *renderer_egl =
    COGL_RENDERER_EGL (meta_render_device_get_renderer_egl (render_device));
  EGLDisplay egl_display;

  if (!cogl_renderer_egl_has_client_extensions (renderer_egl, NULL,
                                                "EGL_MESA_platform_surfaceless",
                                                NULL))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Missing EGL platform required for surfaceless context: "
                   "EGL_MESA_platform_surfaceless");
      return EGL_NO_DISPLAY;
    }

  egl_display = cogl_renderer_egl_get_platform_display (renderer_egl,
                                                        EGL_PLATFORM_SURFACELESS_MESA,
                                                        EGL_DEFAULT_DISPLAY,
                                                        NULL, error);
  if (egl_display == EGL_NO_DISPLAY)
    return EGL_NO_DISPLAY;

  if (!cogl_renderer_egl_initialize (renderer_egl, egl_display, error))
    {
      cogl_renderer_egl_terminate (renderer_egl, egl_display, NULL);
      return EGL_NO_DISPLAY;
    }

  return egl_display;
}

static void
meta_render_device_surfaceless_class_init (MetaRenderDeviceSurfacelessClass *klass)
{
  MetaRenderDeviceClass *render_device_class = META_RENDER_DEVICE_CLASS (klass);

  render_device_class->create_egl_display =
    meta_render_device_surfaceless_create_egl_display;
}

static void
meta_render_device_surfaceless_init (MetaRenderDeviceSurfaceless *render_device_surfaceless)
{
}

MetaRenderDeviceSurfaceless *
meta_render_device_surfaceless_new (MetaBackend  *backend,
                                    GError      **error)
{
  return g_initable_new (META_TYPE_RENDER_DEVICE_SURFACELESS,
                         NULL, error,
                         "backend", backend,
                         NULL);
}
