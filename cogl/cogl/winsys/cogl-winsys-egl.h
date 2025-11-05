/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011 Intel Corporation.
 *               2025 Red Hat.
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

#include <EGL/eglext.h>

#include "cogl/cogl-context.h"
#include "cogl/winsys/cogl-winsys.h"

#define COGL_TYPE_WINSYS_EGL (cogl_winsys_egl_get_type ())

COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglWinsysEGL,
                          cogl_winsys_egl,
                          COGL,
                          WINSYS_EGL,
                          CoglWinsys)

struct _CoglWinsysEGLClass
{
  CoglWinsysClass parent_class;

  gboolean (* context_created) (CoglWinsysEGL  *winsys,
                                CoglDisplay    *display,
                                GError        **error);

  void (* cleanup_context) (CoglWinsysEGL *winsys,
                            CoglDisplay   *display);

  int (* add_config_attributes) (CoglWinsysEGL *winsys,
                                 CoglDisplay   *display,
                                 EGLint        *attributes);

  gboolean (* choose_config) (CoglWinsysEGL  *winsys,
                              CoglDisplay    *display,
                              EGLint         *attributes,
                              EGLConfig      *out_config,
                              GError        **error);
};

#define COGL_MAX_EGL_CONFIG_ATTRIBS 30

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

typedef struct _CoglRendererEGL
{
  CoglEGLWinsysFeature private_features;

  EGLDisplay edpy;

  EGLint egl_version_major;
  EGLint egl_version_minor;

  /* Data specific to the EGL platform */
  void *platform;

  gboolean needs_config;

  /* Sync for latest submitted work */
  EGLSyncKHR sync;

#ifndef APIENTRY
#define APIENTRY
#endif
  /* Function pointers for EGL specific extensions */
#define COGL_WINSYS_FEATURE_BEGIN(a, b, c, d)

#define COGL_WINSYS_FEATURE_FUNCTION(ret, name, args) \
  ret (APIENTRY * pf_ ## name) args;

#define COGL_WINSYS_FEATURE_END()

#include "cogl/winsys/cogl-winsys-egl-feature-functions.h"

#undef COGL_WINSYS_FEATURE_BEGIN
#undef COGL_WINSYS_FEATURE_FUNCTION
#undef COGL_WINSYS_FEATURE_END
} CoglRendererEGL;

typedef struct _CoglDisplayEGL
{
  EGLContext egl_context;
  EGLSurface dummy_surface;
  EGLSurface egl_surface;

  EGLConfig egl_config;

  EGLSurface current_read_surface;
  EGLSurface current_draw_surface;
  EGLContext current_context;

  /* Platform specific display data */
  void *platform;
} CoglDisplayEGL;


COGL_EXPORT EGLBoolean
_cogl_winsys_egl_make_current (CoglDisplay *display,
                               EGLSurface   draw,
                               EGLSurface   read,
                               EGLContext   context);

COGL_EXPORT EGLBoolean
_cogl_winsys_egl_ensure_current (CoglDisplay *display);

/**
 * cogl_display_egl_determine_attributes: (skip)
 */
COGL_EXPORT void
cogl_display_egl_determine_attributes (CoglDisplay *display,
                                       EGLint      *attributes);
