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
 *
 *
 */

#pragma once

#include <EGL/eglext.h>

#include "cogl/cogl-context.h"
#include "cogl/cogl-types.h"
#include "cogl/winsys/cogl-winsys.h"

G_DECLARE_DERIVABLE_TYPE (CoglWinsysEGL,
                          cogl_winsys_egl,
                          COGL,
                          WINSYS_EGL,
                          CoglWinsys)

typedef struct _CoglWinsysEGLClass
{
  CoglWinsysClass parent_class;

  gboolean
  (* context_created) (CoglDisplay *display,
                       GError **error);

  void
  (* cleanup_context) (CoglDisplay *display);

  int
  (* add_config_attributes) (CoglDisplay *display,
                             EGLint      *attributes);
  gboolean
  (* choose_config) (CoglDisplay *display,
                     EGLint *attributes,
                     EGLConfig *out_config,
                     GError **error);
} CoglWinsysEGLClass;

typedef enum _CoglEGLWinsysFeature
{
  COGL_EGL_WINSYS_FEATURE_SWAP_REGION                   = 1L << 0,
  COGL_EGL_WINSYS_FEATURE_EGL_IMAGE_FROM_X11_PIXMAP     = 1L << 1,
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
