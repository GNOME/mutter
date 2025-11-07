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

static void
cleanup_context (CoglWinsysEGL *winsys,
                 CoglDisplay   *display)
{
  CoglDisplayEGL *display_egl = COGL_DISPLAY_EGL (display);
  CoglRenderer *renderer = cogl_display_get_renderer (display);
  EGLDisplay egl_display =
    cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (renderer));
  CoglWinsysEGLClass *egl_class = COGL_WINSYS_EGL_GET_CLASS (winsys);

  if (cogl_display_egl_get_egl_context (display_egl) != EGL_NO_CONTEXT)
    {
      cogl_display_egl_make_current (display_egl,
                                     EGL_NO_SURFACE, EGL_NO_SURFACE,
                                     EGL_NO_CONTEXT);
      eglDestroyContext (egl_display, cogl_display_egl_get_egl_context (display_egl));
      cogl_display_egl_set_egl_context (display_egl, EGL_NO_CONTEXT);
    }

  if (egl_class->cleanup_context)
    egl_class->cleanup_context (winsys, display);
}

static gboolean
try_create_context (CoglWinsysEGL  *winsys,
                    CoglDisplay    *display,
                    GError       **error)
{
  CoglRenderer *renderer = cogl_display_get_renderer (display);
  CoglDisplayEGL *egl_display = COGL_DISPLAY_EGL (display);
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);
  CoglWinsysEGLClass *egl_class = COGL_WINSYS_EGL_GET_CLASS (winsys);
  CoglDisplayEGLClass *display_egl_class = COGL_DISPLAY_EGL_GET_CLASS (winsys);
  EGLConfig config;
  EGLint attribs[11];
  EGLint cfg_attribs[COGL_MAX_EGL_CONFIG_ATTRIBS];
  GError *config_error = NULL;
  const char *error_message;
  int i = 0;

  g_return_val_if_fail (cogl_display_egl_get_egl_context (egl_display) == NULL, TRUE);

  cogl_renderer_bind_api (renderer);

  cogl_display_egl_determine_attributes (egl_display,
                                         cfg_attribs);

  if (!cogl_renderer_egl_has_feature (renderer_egl,
                                      COGL_EGL_WINSYS_FEATURE_NO_CONFIG_CONTEXT) ||
      cogl_renderer_egl_get_needs_config (renderer_egl))
    {
      if (!display_egl_class->choose_config (egl_display,
                                             cfg_attribs,
                                             &config,
                                             &config_error))
        {
          g_set_error (error, COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_CONTEXT,
                       "Couldn't choose config: %s", config_error->message);
          g_error_free (config_error);
          goto err;
        }

      cogl_display_egl_set_egl_config (egl_display, config);
    }

  if (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GL3)
    {
      if (!cogl_renderer_egl_has_feature (renderer_egl,
                                          COGL_EGL_WINSYS_FEATURE_CREATE_CONTEXT))
        {
          error_message = "Driver does not support GL 3 contexts";
          goto fail;
        }

      /* Try to get a core profile 3.1 context with no deprecated features */
      attribs[i++] = EGL_CONTEXT_MAJOR_VERSION_KHR;
      attribs[i++] = 3;
      attribs[i++] = EGL_CONTEXT_MINOR_VERSION_KHR;
      attribs[i++] = 1;
      attribs[i++] = EGL_CONTEXT_FLAGS_KHR;
      attribs[i++] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR;
      attribs[i++] = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
      attribs[i++] = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR;
    }
  else if (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GLES2)
    {
      attribs[i++] = EGL_CONTEXT_CLIENT_VERSION;
      attribs[i++] = 2;
    }

  if (cogl_renderer_egl_has_feature (renderer_egl,
                                     COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY))
    {
      attribs[i++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
      attribs[i++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
    }

  attribs[i++] = EGL_NONE;

  if (cogl_renderer_egl_has_feature (renderer_egl,
                                     COGL_EGL_WINSYS_FEATURE_NO_CONFIG_CONTEXT))
    {
      cogl_display_egl_set_egl_context (egl_display,
                                        eglCreateContext (edpy,
                                                          EGL_NO_CONFIG_KHR,
                                                          EGL_NO_CONTEXT,
                                                          attribs));
    }
  else
    {
      cogl_display_egl_set_egl_context (egl_display,
                                        eglCreateContext (edpy,
                                                          config,
                                                          EGL_NO_CONTEXT,
                                                          attribs));
    }

  if (cogl_display_egl_get_egl_context (egl_display) == EGL_NO_CONTEXT)
    {
      error_message = "Unable to create a suitable EGL context";
      goto fail;
    }

  if (cogl_renderer_egl_has_feature (renderer_egl,
                                     COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY))
    {
      EGLint value = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;

      eglQueryContext (edpy,
                       cogl_display_egl_get_egl_context (egl_display),
                       EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                       &value);

      if (value != EGL_CONTEXT_PRIORITY_HIGH_IMG)
        g_message ("Failed to obtain high priority context");
      else
        g_message ("Obtained a high priority EGL context");
    }

  if (egl_class->context_created &&
      !egl_class->context_created (winsys, display, error))
    return FALSE;

  return TRUE;

fail:
  g_set_error (error, COGL_WINSYS_ERROR,
               COGL_WINSYS_ERROR_CREATE_CONTEXT,
               "%s", error_message);

err:
  cleanup_context (COGL_WINSYS_EGL (winsys), display);

  return FALSE;
}

static void
_cogl_winsys_display_destroy (CoglWinsys  *winsys,
                              CoglDisplay *display)
{
  CoglRenderer *renderer = cogl_display_get_renderer (display);
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEglPrivate *priv_renderer =
    cogl_renderer_egl_get_private (renderer_egl);
  EGLSyncKHR sync = cogl_renderer_egl_get_sync (renderer_egl);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);

  if (sync != EGL_NO_SYNC_KHR)
    priv_renderer->pf_eglDestroySync (edpy, sync);

  cleanup_context (COGL_WINSYS_EGL (winsys), display);
}

static gboolean
_cogl_winsys_display_setup (CoglWinsys   *winsys,
                            CoglDisplay  *display,
                            GError      **error)
{
  if (!try_create_context (COGL_WINSYS_EGL (winsys), display, error))
    goto error;

  return TRUE;

error:
  _cogl_winsys_display_destroy (winsys, display);
  return FALSE;
}

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

  winsys_class->display_setup = _cogl_winsys_display_setup;
  winsys_class->display_destroy = _cogl_winsys_display_destroy;
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
