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

#include "backends/native/meta-renderer-display-egl-private.h"

#include "backends/meta-backend-private.h"
#include "backends/native/meta-render-device-private.h"
#include "backends/native/meta-renderer-native-private.h"
#include "cogl/cogl.h"

struct _MetaRendererDisplayEgl
{
  CoglDisplayEGL parent_instance;
};

G_DEFINE_FINAL_TYPE (MetaRendererDisplayEgl, meta_renderer_display_egl, COGL_TYPE_DISPLAY_EGL)

static gboolean
meta_renderer_display_egl_choose_config (CoglDisplayEGL  *cogl_display_egl,
                                         EGLint          *attributes,
                                         EGLConfig       *out_config,
                                         GError         **error)
{
  CoglRenderer *cogl_renderer =
    cogl_display_get_renderer (COGL_DISPLAY (cogl_display_egl));
  MetaRenderDevice *render_device =
    META_RENDER_DEVICE (cogl_renderer_get_render_device (cogl_renderer));

  switch (cogl_render_device_get_mode (COGL_RENDER_DEVICE (render_device)))
    {
    case COGL_RENDER_DEVICE_MODE_GBM:
      {
        static const uint32_t formats[] = {
          GBM_FORMAT_XRGB8888,
          GBM_FORMAT_ARGB8888,
        };

        return meta_renderer_native_choose_gbm_format (NULL,
                                                       COGL_RENDERER_EGL (cogl_renderer),
                                                       attributes,
                                                       formats,
                                                       G_N_ELEMENTS (formats),
                                                       "fallback",
                                                       out_config,
                                                       error);
      }
    case COGL_RENDER_DEVICE_MODE_SURFACELESS:
      *out_config = EGL_NO_CONFIG_KHR;
      return TRUE;
    }

  return FALSE;
}

static gboolean
meta_renderer_display_egl_setup (CoglDisplay  *cogl_display,
                                 GError      **error)
{
  CoglRenderer *cogl_renderer;
  MetaRenderDevice *render_device;
  MetaRendererNative *renderer_native;

  cogl_renderer = cogl_display_get_renderer (cogl_display);
  render_device =
    META_RENDER_DEVICE (cogl_renderer_get_render_device (cogl_renderer));
  renderer_native =
    META_RENDERER_NATIVE (meta_backend_get_renderer (meta_render_device_get_backend (render_device)));

  if (!COGL_DISPLAY_CLASS (meta_renderer_display_egl_parent_class)->setup (cogl_display, error))
    return FALSE;

  /* Force a full modeset / drmModeSetCrtc on
   * the first swap buffers call.
   */
  meta_renderer_native_queue_modes_reset (renderer_native);

  return TRUE;
}

static void
meta_renderer_display_egl_init (MetaRendererDisplayEgl *display_egl)
{
}

static void
meta_renderer_display_egl_class_init (MetaRendererDisplayEglClass *class)
{
  CoglDisplayEGLClass *egl_class = COGL_DISPLAY_EGL_CLASS (class);
  CoglDisplayClass *display_class = COGL_DISPLAY_CLASS (class);

  egl_class->choose_config = meta_renderer_display_egl_choose_config;

  display_class->setup = meta_renderer_display_egl_setup;
}

MetaRendererDisplayEgl *
meta_renderer_display_egl_new (CoglRenderer *renderer)
{
  MetaRendererDisplayEgl *display_egl;

  g_return_val_if_fail (renderer != NULL, NULL);

  display_egl = g_object_new (META_TYPE_RENDERER_DISPLAY_EGL,
                              "renderer", renderer,
                              NULL);
  return display_egl;
}
