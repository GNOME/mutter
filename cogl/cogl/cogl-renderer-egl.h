/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2025 Red Hat.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-renderer.h"

#include <glib-object.h>
#include <gmodule.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

G_BEGIN_DECLS

#ifndef COGL_GIR_SCANNING
#define COGL_EGL_ERROR (cogl_egl_error_quark ())

COGL_EXPORT
GQuark cogl_egl_error_quark (void);
#endif

typedef enum _CoglEGLWinsysFeature
{
  COGL_EGL_WINSYS_FEATURE_SWAP_REGION                   = 1L << 0,
  COGL_EGL_WINSYS_FEATURE_EGL_IMAGE_FROM_WAYLAND_BUFFER = 1L << 2,
  COGL_EGL_WINSYS_FEATURE_CREATE_CONTEXT                = 1L << 3,
  COGL_EGL_WINSYS_FEATURE_BUFFER_AGE                    = 1L << 4,
  COGL_EGL_WINSYS_FEATURE_FENCE_SYNC                    = 1L << 5,
  COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT           = 1L << 6,
  COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY              = 1L << 7,
  COGL_EGL_WINSYS_FEATURE_NO_CONFIG_CONTEXT             = 1L << 8,
  COGL_EGL_WINSYS_FEATURE_NATIVE_FENCE_SYNC             = 1L << 9,
} CoglEGLWinsysFeature;

#define COGL_TYPE_RENDERER_EGL (cogl_renderer_egl_get_type ())

struct _CoglRendererEGLClass
{
  CoglRendererClass parent_class;
};

COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglRendererEGL,
                          cogl_renderer_egl,
                          COGL,
                          RENDERER_EGL,
                          CoglRenderer)

COGL_EXPORT
CoglRendererEGL *cogl_renderer_egl_new (void);

/**
 * cogl_renderer_egl_set_edisplay: (skip)
 */
COGL_EXPORT
void cogl_renderer_egl_set_edisplay (CoglRendererEGL *renderer_egl,
                                     EGLDisplay       edisplay);

/**
 * cogl_renderer_egl_get_edisplay: (skip)
 */
COGL_EXPORT
EGLDisplay cogl_renderer_egl_get_edisplay (CoglRendererEGL *renderer_egl);

/**
 * cogl_renderer_egl_get_sync: (skip)
 */
COGL_EXPORT
EGLSyncKHR cogl_renderer_egl_get_sync (CoglRendererEGL *renderer_egl);

COGL_EXPORT
gboolean cogl_renderer_egl_has_feature (CoglRendererEGL      *renderer_egl,
                                        CoglEGLWinsysFeature  feature);

/**
 * cogl_renderer_egl_destroy_sync: (skip)
 */
COGL_EXPORT
void cogl_renderer_egl_destroy_sync (CoglRendererEGL *renderer_egl);

COGL_EXPORT
gboolean cogl_renderer_egl_has_swap_buffers_with_damage (CoglRendererEGL *renderer_egl);

/**
 * cogl_renderer_egl_swap_buffers_with_damage: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_swap_buffers_with_damage (CoglRendererEGL  *renderer_egl,
                                                     EGLSurface        surface,
                                                     const EGLint     *rects,
                                                     EGLint            n_rects,
                                                     GError          **error);

/**
 * cogl_renderer_egl_swap_buffers_region: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_swap_buffers_region (CoglRendererEGL  *renderer_egl,
                                                EGLSurface        surface,
                                                EGLint            n_rects,
                                                const EGLint     *rects,
                                                GError          **error);

COGL_EXPORT
gboolean cogl_renderer_egl_has_set_damage_region (CoglRendererEGL *renderer_egl);

/**
 * cogl_renderer_egl_set_damage_region: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_set_damage_region (CoglRendererEGL *renderer_egl,
                                              EGLSurface       surface,
                                              const EGLint    *rects,
                                              EGLint           n_rects);

/**
 * cogl_renderer_egl_has_extensions: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_has_extensions (CoglRendererEGL   *renderer_egl,
                                           const char      ***missing_extensions,
                                           const char        *first_extension,
                                           ...);

/**
 * cogl_renderer_egl_query_string: (skip)
 */
COGL_EXPORT
const char * cogl_renderer_egl_query_string (CoglRendererEGL *renderer_egl,
                                             EGLint           name);

/**
 * cogl_renderer_egl_choose_first_config: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_choose_first_config (CoglRendererEGL  *renderer_egl,
                                                const EGLint     *attrib_list,
                                                EGLConfig        *chosen_config,
                                                GError          **error);

/**
 * cogl_renderer_egl_choose_all_configs: (skip)
 */
COGL_EXPORT
EGLConfig * cogl_renderer_egl_choose_all_configs (CoglRendererEGL  *renderer_egl,
                                                  const EGLint     *attrib_list,
                                                  EGLint           *out_num_configs,
                                                  GError          **error);

/**
 * cogl_renderer_egl_get_config_attrib: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_get_config_attrib (CoglRendererEGL  *renderer_egl,
                                              EGLConfig         config,
                                              EGLint            attribute,
                                              EGLint           *value,
                                              GError          **error);

/**
 * cogl_renderer_egl_create_window_surface: (skip)
 */
COGL_EXPORT
EGLSurface cogl_renderer_egl_create_window_surface (CoglRendererEGL     *renderer_egl,
                                                    EGLConfig            config,
                                                    EGLNativeWindowType  native_window,
                                                    const EGLint        *attrib_list,
                                                    GError             **error);

/**
 * cogl_renderer_egl_create_pbuffer_surface: (skip)
 */
COGL_EXPORT
EGLSurface cogl_renderer_egl_create_pbuffer_surface (CoglRendererEGL  *renderer_egl,
                                                     EGLConfig         config,
                                                     const EGLint     *attrib_list,
                                                     GError          **error);

/**
 * cogl_renderer_egl_destroy_surface: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_destroy_surface (CoglRendererEGL  *renderer_egl,
                                            EGLSurface        surface,
                                            GError          **error);

/**
 * cogl_renderer_egl_create_image: (skip)
 */
COGL_EXPORT
EGLImageKHR cogl_renderer_egl_create_image (CoglRendererEGL  *renderer_egl,
                                            EGLContext        context,
                                            EGLenum           target,
                                            EGLClientBuffer   buffer,
                                            const EGLint     *attrib_list,
                                            GError          **error);

/**
 * cogl_renderer_egl_destroy_image: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_destroy_image (CoglRendererEGL  *renderer_egl,
                                          EGLImageKHR       image,
                                          GError          **error);

/**
 * cogl_renderer_egl_create_dmabuf_image: (skip)
 */
COGL_EXPORT
EGLImageKHR cogl_renderer_egl_create_dmabuf_image (CoglRendererEGL  *renderer_egl,
                                                   unsigned int      width,
                                                   unsigned int      height,
                                                   uint32_t          drm_format,
                                                   uint32_t          n_planes,
                                                   const int        *fds,
                                                   const uint32_t   *strides,
                                                   const uint32_t   *offsets,
                                                   const uint64_t   *modifiers,
                                                   GError          **error);

/**
 * cogl_renderer_egl_bind_wayland_display: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_bind_wayland_display (CoglRendererEGL   *renderer_egl,
                                                 struct wl_display *wayland_display,
                                                 GError           **error);

/**
 * cogl_renderer_egl_query_wayland_buffer: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_query_wayland_buffer (CoglRendererEGL     *renderer_egl,
                                                 struct wl_resource  *buffer,
                                                 EGLint               attribute,
                                                 EGLint              *value,
                                                 GError             **error);

/**
 * cogl_renderer_egl_query_dma_buf_formats: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_query_dma_buf_formats (CoglRendererEGL  *renderer_egl,
                                                  EGLint            max_formats,
                                                  EGLint           *formats,
                                                  EGLint           *num_formats,
                                                  GError          **error);

/**
 * cogl_renderer_egl_query_dma_buf_modifiers: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_query_dma_buf_modifiers (CoglRendererEGL  *renderer_egl,
                                                    EGLint            format,
                                                    EGLint            max_modifiers,
                                                    EGLuint64KHR     *modifiers,
                                                    EGLBoolean       *external_only,
                                                    EGLint           *num_modifiers,
                                                    GError          **error);

/**
 * cogl_renderer_egl_query_display_attrib: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_query_display_attrib (CoglRendererEGL  *renderer_egl,
                                                 EGLint            attribute,
                                                 EGLAttrib        *value,
                                                 GError          **error);

/**
 * cogl_renderer_egl_query_device_string: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_query_device_string (CoglRendererEGL  *renderer_egl,
                                                EGLDeviceEXT      device,
                                                EGLint            name,
                                                const char      **out_string,
                                                GError          **error);

/**
 * cogl_renderer_egl_device_has_extensions: (skip)
 */
COGL_EXPORT
gboolean cogl_renderer_egl_device_has_extensions (CoglRendererEGL   *renderer_egl,
                                                  EGLDeviceEXT       device,
                                                  const char      ***missing_extensions,
                                                  const char        *first_extension,
                                                  ...);

G_END_DECLS
