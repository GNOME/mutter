/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010,2011,2013 Intel Corporation.
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
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-util.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-display-egl.h"
#include "cogl/cogl-display-egl-private.h"
#include "cogl/cogl-framebuffer.h"
#include "cogl/cogl-onscreen-private.h"
#include "cogl/cogl-renderer-egl.h"
#include "cogl/cogl-renderer-egl-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-trace.h"
#include "cogl/winsys/cogl-winsys.h"
#include "cogl/winsys/cogl-winsys-egl.h"
#include "cogl/winsys/cogl-onscreen-egl.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

G_DEFINE_ABSTRACT_TYPE (CoglWinsysEGL, cogl_winsys_egl, COGL_TYPE_WINSYS)

#ifndef EGL_KHR_create_context
#define EGL_CONTEXT_MAJOR_VERSION_KHR           0x3098
#define EGL_CONTEXT_MINOR_VERSION_KHR           0x30FB
#define EGL_CONTEXT_FLAGS_KHR                   0x30FC
#define EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR     0x30FD
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR  0x31BD
#define EGL_OPENGL_ES3_BIT_KHR                  0x0040
#define EGL_NO_RESET_NOTIFICATION_KHR           0x31BE
#define EGL_LOSE_CONTEXT_ON_RESET_KHR           0x31BF
#define EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR                 0x00000001
#define EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR    0x00000002
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR         0x00000004
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR          0x00000001
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR 0x00000002
#endif

#ifndef EGL_IMG_context_priority
#define EGL_CONTEXT_PRIORITY_LEVEL_IMG          0x3100
#define EGL_CONTEXT_PRIORITY_HIGH_IMG           0x3101
#define EGL_CONTEXT_PRIORITY_MEDIUM_IMG         0x3102
#define EGL_CONTEXT_PRIORITY_LOW_IMG            0x3103
#endif

static gboolean
_cogl_winsys_context_init (CoglWinsys  *winsys,
                           CoglContext *context,
                           GError     **error)
{
  CoglRenderer *renderer = cogl_context_get_renderer (context);
  CoglDriver *driver = cogl_renderer_get_driver (renderer);
  CoglDisplayEGL *egl_display = COGL_DISPLAY_EGL (context->display);
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);

  g_return_val_if_fail (cogl_display_egl_get_egl_context (egl_display), FALSE);

  memset (context->winsys_features, 0, sizeof (context->winsys_features));

  cogl_renderer_egl_check_extensions (renderer);

  if (!cogl_driver_update_features (driver, renderer, error))
    return FALSE;

  if (cogl_renderer_egl_has_feature (renderer_egl,
                                     COGL_EGL_WINSYS_FEATURE_SWAP_REGION))
    {
      COGL_FLAGS_SET (context->winsys_features,
                      COGL_WINSYS_FEATURE_SWAP_REGION, TRUE);
    }

  if ((cogl_renderer_egl_has_feature (renderer_egl,
                                     COGL_EGL_WINSYS_FEATURE_FENCE_SYNC)) &&
      cogl_driver_has_feature (driver, COGL_FEATURE_ID_OES_EGL_SYNC))
    cogl_driver_set_feature (driver, COGL_FEATURE_ID_FENCE, TRUE);

  if (cogl_renderer_egl_has_feature (renderer_egl,
                                     COGL_EGL_WINSYS_FEATURE_NATIVE_FENCE_SYNC))
    cogl_driver_set_feature (driver, COGL_FEATURE_ID_FENCE, TRUE);

  if (cogl_renderer_egl_has_feature (renderer_egl,
                                     COGL_EGL_WINSYS_FEATURE_BUFFER_AGE))
    {
      COGL_FLAGS_SET (context->winsys_features,
                      COGL_WINSYS_FEATURE_BUFFER_AGE,
                      TRUE);
    }

  return TRUE;
}

static void
cogl_winsys_egl_class_init (CoglWinsysEGLClass *klass)
{
  CoglWinsysClass *winsys_class = COGL_WINSYS_CLASS (klass);

  winsys_class->context_init = _cogl_winsys_context_init;
}

static void
cogl_winsys_egl_init (CoglWinsysEGL *winsys_egl)
{
}

EGLDisplay
cogl_context_get_egl_display (CoglContext *context)
{
  CoglRenderer *renderer = cogl_context_get_renderer (context);

  return cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (renderer));
}
