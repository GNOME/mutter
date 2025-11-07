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

void
cogl_display_egl_determine_attributes (CoglDisplay *display,
                                       EGLint      *attributes)
{
  CoglRenderer *renderer = display->renderer;
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
  CoglWinsysEGLClass *egl_class = COGL_WINSYS_EGL_GET_CLASS (winsys);
  int i = 0;

  /* Let the platform add attributes first, including setting the
   * EGL_SURFACE_TYPE */
  i = egl_class->add_config_attributes (COGL_WINSYS_EGL (winsys),
                                        display,
                                        attributes);

  attributes[i++] = EGL_STENCIL_SIZE;
  attributes[i++] = 2;

  attributes[i++] = EGL_RED_SIZE;
  attributes[i++] = 1;
  attributes[i++] = EGL_GREEN_SIZE;
  attributes[i++] = 1;
  attributes[i++] = EGL_BLUE_SIZE;
  attributes[i++] = 1;

  attributes[i++] = EGL_ALPHA_SIZE;
  attributes[i++] = EGL_DONT_CARE;

  attributes[i++] = EGL_DEPTH_SIZE;
  attributes[i++] = 1;

  attributes[i++] = EGL_BUFFER_SIZE;
  attributes[i++] = EGL_DONT_CARE;

  attributes[i++] = EGL_RENDERABLE_TYPE;
  attributes[i++] = (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GL3 ?
                     EGL_OPENGL_BIT :
                     EGL_OPENGL_ES2_BIT);

  attributes[i++] = EGL_NONE;

  g_assert (i < COGL_MAX_EGL_CONFIG_ATTRIBS);
}

EGLBoolean
_cogl_winsys_egl_make_current (CoglDisplay *display,
                               EGLSurface draw,
                               EGLSurface read,
                               EGLContext context)
{
  CoglDisplayEGL *display_egl = display->winsys;
  EGLDisplay egl_display =
    cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (display->renderer));
  EGLBoolean ret;

  if (display_egl->current_draw_surface == draw &&
      display_egl->current_read_surface == read &&
      display_egl->current_context == context)
    return EGL_TRUE;

  ret = eglMakeCurrent (egl_display,
                        draw,
                        read,
                        context);

  display_egl->current_draw_surface = draw;
  display_egl->current_read_surface = read;
  display_egl->current_context = context;

  return ret;
}

EGLBoolean
_cogl_winsys_egl_ensure_current (CoglDisplay *display)
{
  CoglDisplayEGL *display_egl = display->winsys;
  EGLDisplay egl_display =
    cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (display->renderer));

  return eglMakeCurrent (egl_display,
                         display_egl->current_draw_surface,
                         display_egl->current_read_surface,
                         display_egl->current_context);
}

static void
cleanup_context (CoglWinsysEGL *winsys,
                 CoglDisplay   *display)
{
  CoglDisplayEGL *display_egl = display->winsys;
  EGLDisplay egl_display =
    cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (display->renderer));
  CoglWinsysEGLClass *egl_class = COGL_WINSYS_EGL_GET_CLASS (winsys);

  if (display_egl->egl_context != EGL_NO_CONTEXT)
    {
      _cogl_winsys_egl_make_current (display,
                                     EGL_NO_SURFACE, EGL_NO_SURFACE,
                                     EGL_NO_CONTEXT);
      eglDestroyContext (egl_display, display_egl->egl_context);
      display_egl->egl_context = EGL_NO_CONTEXT;
    }

  if (egl_class->cleanup_context)
    egl_class->cleanup_context (winsys, display);
}

static gboolean
try_create_context (CoglWinsysEGL  *winsys,
                    CoglDisplay    *display,
                    GError       **error)
{
  CoglRenderer *renderer = display->renderer;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);
  CoglWinsysEGLClass *egl_class = COGL_WINSYS_EGL_GET_CLASS (winsys);
  EGLConfig config;
  EGLint attribs[11];
  EGLint cfg_attribs[COGL_MAX_EGL_CONFIG_ATTRIBS];
  GError *config_error = NULL;
  const char *error_message;
  int i = 0;

  g_return_val_if_fail (egl_display->egl_context == NULL, TRUE);

  cogl_renderer_bind_api (renderer);

  cogl_display_egl_determine_attributes (display,
                                         cfg_attribs);

  if (!cogl_renderer_egl_has_feature (renderer_egl,
                                      COGL_EGL_WINSYS_FEATURE_NO_CONFIG_CONTEXT) ||
      cogl_renderer_egl_get_needs_config (renderer_egl))
    {
      if (!egl_class->choose_config (winsys,
                                     display,
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

      egl_display->egl_config = config;
    }

  if (cogl_renderer_get_driver_id (display->renderer) == COGL_DRIVER_ID_GL3)
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
  else if (cogl_renderer_get_driver_id (display->renderer) == COGL_DRIVER_ID_GLES2)
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
      egl_display->egl_context = eglCreateContext (edpy,
                                                   EGL_NO_CONFIG_KHR,
                                                   EGL_NO_CONTEXT,
                                                   attribs);
    }
  else
    {
      egl_display->egl_context = eglCreateContext (edpy,
                                                   config,
                                                   EGL_NO_CONTEXT,
                                                   attribs);
    }

  if (egl_display->egl_context == EGL_NO_CONTEXT)
    {
      error_message = "Unable to create a suitable EGL context";
      goto fail;
    }

  if (cogl_renderer_egl_has_feature (renderer_egl,
                                     COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY))
    {
      EGLint value = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;

      eglQueryContext (edpy,
                       egl_display->egl_context,
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
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (display->renderer);
  CoglRendererEglPrivate *priv_renderer =
    cogl_renderer_egl_get_private (renderer_egl);
  EGLSyncKHR sync = cogl_renderer_egl_get_sync (renderer_egl);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);
  CoglDisplayEGL *egl_display = display->winsys;

  g_return_if_fail (egl_display != NULL);

  if (sync != EGL_NO_SYNC_KHR)
    priv_renderer->pf_eglDestroySync (edpy, sync);

  cleanup_context (COGL_WINSYS_EGL (winsys), display);

  g_free (display->winsys);
  display->winsys = NULL;
}

static gboolean
_cogl_winsys_display_setup (CoglWinsys   *winsys,
                            CoglDisplay  *display,
                            GError      **error)
{
  CoglDisplayEGL *egl_display;

  g_return_val_if_fail (display->winsys == NULL, FALSE);

  egl_display = g_new0 (CoglDisplayEGL, 1);
  display->winsys = egl_display;

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
  CoglRenderer *renderer = context->display->renderer;
  CoglDriver *driver = cogl_renderer_get_driver (renderer);
  CoglDisplayEGL *egl_display = context->display->winsys;
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);

  g_return_val_if_fail (egl_display->egl_context, FALSE);

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
  return cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (context->display->renderer));
}
