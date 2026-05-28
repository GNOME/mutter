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

#include "backends/native/meta-renderer-context-egl-private.h"
#include "backends/native/meta-renderer-egl.h"
#include "backends/native/meta-renderer-native-private.h"
#include "cogl/cogl-driver-private.h"

#include <gio/gio.h>

struct _MetaRendererContextEgl
{
  CoglContextEGL parent_instance;
};

static gboolean
meta_renderer_context_egl_initable_init (GInitable     *initable,
                                         GCancellable  *cancellable,
                                         GError       **error);

static void
meta_renderer_context_egl_initable_iface_init (GInitableIface *iface)
{
  iface->init = meta_renderer_context_egl_initable_init;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (MetaRendererContextEgl, meta_renderer_context_egl,
                               COGL_TYPE_CONTEXT_EGL,
                               G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                      meta_renderer_context_egl_initable_iface_init))

static gboolean
meta_renderer_context_egl_initable_init (GInitable     *initable,
                                         GCancellable  *cancellable,
                                         GError       **error)
{
  GInitableIface *parent_iface =
    g_type_interface_peek (meta_renderer_context_egl_parent_class, G_TYPE_INITABLE);

  if (!parent_iface->init (initable, cancellable, error))
    return FALSE;

#ifdef HAVE_EGL_DEVICE
  {
    CoglContext *context = COGL_CONTEXT (initable);
    CoglRenderer *cogl_renderer = cogl_context_get_renderer (context);
    MetaRendererNativeGpuData *renderer_gpu_data =
      meta_renderer_egl_get_renderer_gpu_data (META_RENDERER_EGL (cogl_renderer));

    if (renderer_gpu_data->mode == META_RENDERER_NATIVE_MODE_EGL_DEVICE)
      cogl_driver_set_feature (cogl_context_get_driver (context),
                               COGL_FEATURE_ID_TEXTURE_EGL_IMAGE_EXTERNAL, TRUE);
  }
#endif

  return TRUE;
}

static void
meta_renderer_context_egl_init (MetaRendererContextEgl *context_gl)
{
}

static void
meta_renderer_context_egl_class_init (MetaRendererContextEglClass *klass)
{
}

CoglContext *
meta_renderer_context_egl_new (CoglDisplay  *display,
                               GError      **error)
{
  g_return_val_if_fail (display != NULL, NULL);

  return g_initable_new (META_TYPE_RENDERER_CONTEXT_EGL, NULL, error,
                         "display", display,
                         NULL);
}
