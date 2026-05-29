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

#include "backends/native/meta-renderer-egl.h"
#include "backends/native/meta-renderer-native-private.h"
#include "cogl/cogl.h"

struct _MetaRendererDisplayEgl
{
  CoglDisplayEGL parent_instance;
};

G_DEFINE_FINAL_TYPE (MetaRendererDisplayEgl, meta_renderer_display_egl, COGL_TYPE_DISPLAY_EGL)

static int
meta_renderer_display_egl_add_config_attributes (CoglDisplayEGL *cogl_display_egl,
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
    }

  return i;
}

static gboolean
meta_renderer_display_egl_choose_config (CoglDisplayEGL  *cogl_display_egl,
                                         EGLint          *attributes,
                                         EGLConfig       *out_config,
                                         GError         **error)
{
  CoglRenderer *cogl_renderer =
    cogl_display_get_renderer (COGL_DISPLAY (cogl_display_egl));
  MetaRendererNativeGpuData *renderer_gpu_data =
    meta_renderer_egl_get_renderer_gpu_data (META_RENDERER_EGL (cogl_renderer));

  switch (renderer_gpu_data->mode)
    {
    case META_RENDERER_NATIVE_MODE_GBM:
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
    case META_RENDERER_NATIVE_MODE_SURFACELESS:
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
  MetaRendererNativeGpuData *renderer_gpu_data;
  MetaRendererNative *renderer_native;

  cogl_renderer = cogl_display_get_renderer (cogl_display);
  renderer_gpu_data =
    meta_renderer_egl_get_renderer_gpu_data (META_RENDERER_EGL (cogl_renderer));
  renderer_native = renderer_gpu_data->renderer_native;

  if (!COGL_DISPLAY_CLASS (meta_renderer_display_egl_parent_class)->setup (cogl_display, error))
    return FALSE;

  /* Force a full modeset / drmModeSetCrtc on
   * the first swap buffers call.
   */
  meta_renderer_native_queue_modes_reset (renderer_native);

  return TRUE;
}

static EGLSurface
create_dummy_pbuffer_surface (CoglRendererEGL  *renderer_egl,
                              GError          **error)
{
  EGLConfig pbuffer_config;
  static const EGLint pbuffer_config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, 0,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };
  static const EGLint pbuffer_attribs[] = {
    EGL_WIDTH, 16,
    EGL_HEIGHT, 16,
    EGL_NONE
  };

  if (!cogl_renderer_egl_choose_first_config (renderer_egl,
                                              pbuffer_config_attribs,
                                              &pbuffer_config, error))
    return EGL_NO_SURFACE;

  return cogl_renderer_egl_create_pbuffer_surface (renderer_egl,
                                                   pbuffer_config,
                                                   pbuffer_attribs,
                                                   error);
}

static gboolean
meta_renderer_display_egl_context_created (CoglDisplayEGL  *cogl_display_egl,
                                           GError         **error)
{
  CoglRenderer *cogl_renderer =
    cogl_display_get_renderer (COGL_DISPLAY (cogl_display_egl));
  CoglRendererEGL *renderer_egl = COGL_RENDERER_EGL (cogl_renderer);

  if (!cogl_renderer_egl_has_feature (renderer_egl,
                                      COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT))
    {
      cogl_display_egl_set_dummy_surface (cogl_display_egl,
                                          create_dummy_pbuffer_surface (renderer_egl,
                                                                        error));
      if (cogl_display_egl_get_dummy_surface (cogl_display_egl) == EGL_NO_SURFACE)
        return FALSE;
    }

  if (!cogl_display_egl_make_current (cogl_display_egl,
                                      cogl_display_egl_get_dummy_surface (cogl_display_egl),
                                      cogl_display_egl_get_dummy_surface (cogl_display_egl),
                                      cogl_display_egl_get_egl_context (cogl_display_egl)))
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_CONTEXT,
                   "Failed to make context current");
      return FALSE;
    }

  return TRUE;
}

static void
meta_renderer_display_egl_cleanup_context (CoglDisplayEGL *cogl_display_egl)
{
  CoglRenderer *cogl_renderer =
    cogl_display_get_renderer (COGL_DISPLAY (cogl_display_egl));

  if (cogl_display_egl_get_dummy_surface (cogl_display_egl) != EGL_NO_SURFACE)
    {
      cogl_renderer_egl_destroy_surface (COGL_RENDERER_EGL (cogl_renderer),
                                         cogl_display_egl_get_dummy_surface (cogl_display_egl),
                                         NULL);
      cogl_display_egl_set_dummy_surface (cogl_display_egl, EGL_NO_SURFACE);
    }
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

  egl_class->add_config_attributes = meta_renderer_display_egl_add_config_attributes;
  egl_class->choose_config = meta_renderer_display_egl_choose_config;
  egl_class->context_created = meta_renderer_display_egl_context_created;
  egl_class->cleanup_context = meta_renderer_display_egl_cleanup_context;

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
