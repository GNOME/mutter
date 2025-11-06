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

struct _CoglRendererEglClass
{
  CoglRendererClass parent_class;
};

COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglRendererEgl,
                          cogl_renderer_egl,
                          COGL,
                          RENDERER_EGL,
                          CoglRenderer)

COGL_EXPORT
CoglRendererEgl *cogl_renderer_egl_new (void);

/**
 * cogl_renderer_egl_set_edisplay: (skip)
 */
COGL_EXPORT
void cogl_renderer_egl_set_edisplay (CoglRendererEgl *renderer_egl,
                                     EGLDisplay       edisplay);

/**
 * cogl_renderer_egl_get_edisplay: (skip)
 */
COGL_EXPORT
EGLDisplay cogl_renderer_egl_get_edisplay (CoglRendererEgl *renderer_egl);

COGL_EXPORT
void cogl_renderer_egl_set_needs_config (CoglRendererEgl *renderer_egl,
                                         gboolean         needs_config);

COGL_EXPORT
gboolean cogl_renderer_egl_get_needs_config (CoglRendererEgl *renderer_egl);

/**
 * cogl_renderer_egl_get_sync: (skip)
 */
COGL_EXPORT
EGLSyncKHR cogl_renderer_egl_get_sync (CoglRendererEgl *renderer_egl);

COGL_EXPORT
gboolean cogl_renderer_egl_has_feature (CoglRendererEgl      *renderer_egl,
                                        CoglEGLWinsysFeature  feature);

G_END_DECLS
