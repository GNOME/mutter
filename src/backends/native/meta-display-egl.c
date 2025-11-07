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

#include "backends/native/meta-display-egl-private.h"
#include "backends/native/meta-renderer-egl.h"
#include "backends/native/meta-renderer-native-private.h"

struct _MetaDisplayEGL
{
  CoglDisplayEGL parent_instance;
};

G_DEFINE_FINAL_TYPE (MetaDisplayEGL, meta_display_egl, COGL_TYPE_DISPLAY_EGL)

static int
meta_display_egl_add_config_attributes (CoglDisplayEGL *cogl_display_egl,
                                        EGLint         *attributes)
{
  CoglDisplay *cogl_display = COGL_DISPLAY (cogl_display_egl);
  CoglRenderer *cogl_renderer = cogl_display_get_renderer (cogl_display);
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_egl_get_renderer_gpu_data (META_RENDERER_EGL (cogl_renderer));
  int i = 0;

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_WINDOW_BIT;
      break;
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_PBUFFER_BIT;
      break;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      attributes[i++] = EGL_SURFACE_TYPE;
      attributes[i++] = EGL_STREAM_BIT_KHR;
      break;
#endif
    }

  return i;
}

static gboolean
meta_display_egl_choose_config (CoglDisplayEGL  *cogl_display_egl,
                                EGLint          *attributes,
                                EGLConfig       *out_config,
                                GError         **error)
{
  CoglRenderer *cogl_renderer =
    cogl_display_get_renderer (COGL_DISPLAY (cogl_display_egl));
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_egl_get_renderer_gpu_data (META_RENDERER_EGL (cogl_renderer));
  MetaRendererNative *renderer = renderer_gpu_data->renderer_native;
  MetaBackend *backend = meta_renderer_get_backend (META_RENDERER (renderer));
  MetaEgl *egl = meta_backend_get_egl (backend);
  EGLDisplay egl_display =
    cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (cogl_renderer));

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
      {
        static const uint32_t formats[] = {
          GBM_FORMAT_XRGB8888,
          GBM_FORMAT_ARGB8888,
        };

        return meta_renderer_native_choose_gbm_format (NULL,
                                                       egl,
                                                       egl_display,
                                                       attributes,
                                                       formats,
                                                       G_N_ELEMENTS (formats),
                                                       "fallback",
                                                       out_config,
                                                       error);
      }
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
      *out_config = EGL_NO_CONFIG_KHR;
      return TRUE;
#ifdef HAVE_EGL_DEVICE
    case META_RENDERER_NATIVE_MODE_EGL_DEVICE:
      return meta_egl_choose_first_config (egl,
                                           egl_display,
                                           attributes,
                                           out_config,
                                           error);
#endif
    }

  return FALSE;
}

static gboolean
meta_display_egl_setup (CoglDisplay  *cogl_display,
                        GError      **error)
{
  CoglRenderer *cogl_renderer;
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaRendererNative *renderer_native;

  cogl_renderer = cogl_display_get_renderer (cogl_display);
  renderer_gpu_data =
    meta_renderer_egl_get_renderer_gpu_data (META_RENDERER_EGL (cogl_renderer));
  renderer_native = renderer_gpu_data->renderer_native;

#ifdef HAVE_EGL_DEVICE
  if (renderer_gpu_data->mode == META_RENDERER_NATIVE_MODE_EGL_DEVICE)
    cogl_renderer_egl_set_needs_config (COGL_RENDERER_EGL (cogl_renderer),  TRUE);
#endif

  if (!COGL_DISPLAY_CLASS (meta_display_egl_parent_class)->setup (cogl_display, error))
    return FALSE;

  /* Force a full modeset / drmModeSetCrtc on
   * the first swap buffers call.
   */
  meta_renderer_native_queue_modes_reset (renderer_native);

  return TRUE;
}

static void
meta_display_egl_init (MetaDisplayEGL *display_egl)
{
}

static void
meta_display_egl_class_init (MetaDisplayEGLClass *class)
{
  CoglDisplayEGLClass *egl_class = COGL_DISPLAY_EGL_CLASS (class);
  CoglDisplayClass *display_class = COGL_DISPLAY_CLASS (class);

  egl_class->add_config_attributes = meta_display_egl_add_config_attributes;
  egl_class->choose_config = meta_display_egl_choose_config;

  display_class->setup = meta_display_egl_setup;
}

MetaDisplayEGL *
meta_display_egl_new (CoglRenderer *renderer)
{
  MetaDisplayEGL *display_egl;

  g_return_val_if_fail (renderer != NULL, NULL);

  display_egl = g_object_new (META_TYPE_DISPLAY_EGL,
                              "renderer", renderer,
                              NULL);
  return display_egl;
}
