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
#include "cogl/cogl-feature-private.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-framebuffer.h"
#include "cogl/cogl-onscreen-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-trace.h"
#include "cogl/winsys/cogl-winsys-egl-private.h"
#include "cogl/winsys/cogl-winsys.h"
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

/* Define a set of arrays containing the functions required from GL
   for each winsys feature */
#define COGL_WINSYS_FEATURE_BEGIN(name, namespaces, extension_names,    \
                                  egl_private_flags)                    \
  static const CoglFeatureFunction                                      \
  cogl_egl_feature_ ## name ## _funcs[] = {
#define COGL_WINSYS_FEATURE_FUNCTION(ret, name, args)                   \
  { G_STRINGIFY (name), G_STRUCT_OFFSET (CoglRendererEGL, pf_ ## name) },
#define COGL_WINSYS_FEATURE_END()               \
  { NULL, 0 },                                  \
    };
#include "cogl/winsys/cogl-winsys-egl-feature-functions.h"

/* Define an array of features */
#undef COGL_WINSYS_FEATURE_BEGIN
#define COGL_WINSYS_FEATURE_BEGIN(name, namespaces, extension_names,    \
                                  egl_private_flags)                    \
  { 255, 255, 0, namespaces, extension_names,                           \
      egl_private_flags,                                                \
      0,                                                                \
      cogl_egl_feature_ ## name ## _funcs },
#undef COGL_WINSYS_FEATURE_FUNCTION
#define COGL_WINSYS_FEATURE_FUNCTION(ret, name, args)
#undef COGL_WINSYS_FEATURE_END
#define COGL_WINSYS_FEATURE_END()

static const CoglFeatureData winsys_feature_data[] =
  {
#include "cogl/winsys/cogl-winsys-egl-feature-functions.h"
  };

static GCallback
_cogl_winsys_renderer_get_proc_address (CoglWinsys   *winsys,
                                        CoglRenderer *renderer,
                                        const char   *name)
{
  GCallback result = eglGetProcAddress (name);

  if (result == NULL)
    g_module_symbol (cogl_renderer_get_gl_module (renderer), name, (gpointer *)&result);

  return result;
}

static void
_cogl_winsys_renderer_bind_api (CoglWinsys   *winsys,
                                CoglRenderer *renderer)
{
  if (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GL3)
    eglBindAPI (EGL_OPENGL_API);
  else if (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GLES2)
    eglBindAPI (EGL_OPENGL_ES_API);
}

/* Updates all the function pointers */
static void
check_egl_extensions (CoglRenderer *renderer)
{
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (renderer);
  const char *egl_extensions;
  char **split_extensions;
  int i;

  egl_extensions = eglQueryString (egl_renderer->edpy, EGL_EXTENSIONS);
  split_extensions = g_strsplit (egl_extensions, " ", 0 /* max_tokens */);

  COGL_NOTE (WINSYS, "  EGL Extensions: %s", egl_extensions);

  egl_renderer->private_features = 0;
  for (i = 0; i < G_N_ELEMENTS (winsys_feature_data); i++)
    if (_cogl_feature_check (renderer,
                             "EGL", winsys_feature_data + i, 0, 0,
                             COGL_DRIVER_ID_GL3, /* the driver isn't used */
                             split_extensions,
                             egl_renderer))
      {
        egl_renderer->private_features |=
          winsys_feature_data[i].feature_flags_private;
      }

  g_strfreev (split_extensions);
}

static gboolean
_cogl_winsys_renderer_connect (CoglWinsys   *winsys,
                              CoglRenderer  *renderer,
                               GError      **error)
{
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (renderer);

  if (!eglInitialize (egl_renderer->edpy,
                      &egl_renderer->egl_version_major,
                      &egl_renderer->egl_version_minor))
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_INIT,
                   "Couldn't initialize EGL");
      return FALSE;
    }

  check_egl_extensions (renderer);

  return TRUE;
}

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
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (display->renderer);
  EGLBoolean ret;

  if (egl_display->current_draw_surface == draw &&
      egl_display->current_read_surface == read &&
      egl_display->current_context == context)
    return EGL_TRUE;

  ret = eglMakeCurrent (egl_renderer->edpy,
                        draw,
                        read,
                        context);

  egl_display->current_draw_surface = draw;
  egl_display->current_read_surface = read;
  egl_display->current_context = context;

  return ret;
}

EGLBoolean
_cogl_winsys_egl_ensure_current (CoglDisplay *display)
{
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (display->renderer);

  return eglMakeCurrent (egl_renderer->edpy,
                         egl_display->current_draw_surface,
                         egl_display->current_read_surface,
                         egl_display->current_context);
}

static void
cleanup_context (CoglWinsysEGL *winsys,
                 CoglDisplay   *display)
{
  CoglRenderer *renderer = display->renderer;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (renderer);
  CoglWinsysEGLClass *egl_class = COGL_WINSYS_EGL_GET_CLASS (winsys);

  if (egl_display->egl_context != EGL_NO_CONTEXT)
    {
      _cogl_winsys_egl_make_current (display,
                                     EGL_NO_SURFACE, EGL_NO_SURFACE,
                                     EGL_NO_CONTEXT);
      eglDestroyContext (egl_renderer->edpy, egl_display->egl_context);
      egl_display->egl_context = EGL_NO_CONTEXT;
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
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (renderer);
  CoglWinsysEGLClass *egl_class = COGL_WINSYS_EGL_GET_CLASS (winsys);
  EGLDisplay edpy;
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

  edpy = egl_renderer->edpy;

  if (!(egl_renderer->private_features &
        COGL_EGL_WINSYS_FEATURE_NO_CONFIG_CONTEXT) ||
      egl_renderer->needs_config)
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
      if (!(egl_renderer->private_features &
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

  if (egl_renderer->private_features &
      COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY)
    {
      attribs[i++] = EGL_CONTEXT_PRIORITY_LEVEL_IMG;
      attribs[i++] = EGL_CONTEXT_PRIORITY_HIGH_IMG;
    }

  attribs[i++] = EGL_NONE;

  if (egl_renderer->private_features &
      COGL_EGL_WINSYS_FEATURE_NO_CONFIG_CONTEXT)
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

  if (egl_renderer->private_features &
      COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY)
    {
      EGLint value = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;

      eglQueryContext (egl_renderer->edpy,
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
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (display->renderer);
  CoglDisplayEGL *egl_display = display->winsys;

  g_return_if_fail (egl_display != NULL);

  if (egl_renderer->sync != EGL_NO_SYNC_KHR)
    egl_renderer->pf_eglDestroySync (egl_renderer->edpy, egl_renderer->sync);

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
  CoglDisplayEGL *egl_display = context->display->winsys;
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (renderer);

  context->winsys = g_new0 (CoglContextEGL, 1);

  g_return_val_if_fail (egl_display->egl_context, FALSE);

  memset (context->winsys_features, 0, sizeof (context->winsys_features));

  check_egl_extensions (renderer);

  if (!_cogl_context_update_features (context, error))
    return FALSE;

  if (egl_renderer->private_features & COGL_EGL_WINSYS_FEATURE_SWAP_REGION)
    {
      COGL_FLAGS_SET (context->winsys_features,
                      COGL_WINSYS_FEATURE_SWAP_REGION, TRUE);
      COGL_FLAGS_SET (context->winsys_features,
                      COGL_WINSYS_FEATURE_SWAP_REGION_THROTTLE, TRUE);
    }

  if ((egl_renderer->private_features & COGL_EGL_WINSYS_FEATURE_FENCE_SYNC) &&
      _cogl_has_private_feature (context, COGL_PRIVATE_FEATURE_OES_EGL_SYNC))
    COGL_FLAGS_SET (context->features, COGL_FEATURE_ID_FENCE, TRUE);

  if (egl_renderer->private_features & COGL_EGL_WINSYS_FEATURE_NATIVE_FENCE_SYNC)
    COGL_FLAGS_SET (context->features, COGL_FEATURE_ID_SYNC_FD, TRUE);

  if (egl_renderer->private_features & COGL_EGL_WINSYS_FEATURE_BUFFER_AGE)
    {
      COGL_FLAGS_SET (context->winsys_features,
                      COGL_WINSYS_FEATURE_BUFFER_AGE,
                      TRUE);
      COGL_FLAGS_SET (context->features, COGL_FEATURE_ID_BUFFER_AGE, TRUE);
    }

  return TRUE;
}

static void
_cogl_winsys_context_deinit (CoglWinsys  *winsys,
                             CoglContext *context)
{
  g_free (context->winsys);
}

#if defined(EGL_KHR_fence_sync) || defined(EGL_KHR_reusable_sync)

static int
_cogl_winsys_get_sync_fd (CoglWinsys  *winsys,
                          CoglContext *context)
{
  CoglRendererEGL *renderer = cogl_renderer_get_winsys_data (context->display->renderer);
  int fd;

  if (!renderer->pf_eglDupNativeFenceFD)
    return -1;

  fd = renderer->pf_eglDupNativeFenceFD (renderer->edpy, renderer->sync);
  if (fd == EGL_NO_NATIVE_FENCE_FD_ANDROID)
    return -1;

  return fd;
}

static void
_cogl_winsys_update_sync (CoglWinsys  *winsys,
                          CoglContext *context)
{
  CoglRendererEGL *renderer = cogl_renderer_get_winsys_data (context->display->renderer);

  if (!renderer->pf_eglDestroySync || !renderer->pf_eglCreateSync)
    return;

  if (renderer->sync != EGL_NO_SYNC_KHR)
    renderer->pf_eglDestroySync (renderer->edpy, renderer->sync);

  renderer->sync = renderer->pf_eglCreateSync (renderer->edpy,
        EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
}
#endif

static void
cogl_winsys_egl_class_init (CoglWinsysEGLClass *klass)
{
  CoglWinsysClass *winsys_class = COGL_WINSYS_CLASS (klass);

  winsys_class->renderer_get_proc_address = _cogl_winsys_renderer_get_proc_address;
  winsys_class->renderer_connect = _cogl_winsys_renderer_connect;
  winsys_class->renderer_bind_api = _cogl_winsys_renderer_bind_api;
  winsys_class->display_setup = _cogl_winsys_display_setup;
  winsys_class->display_destroy = _cogl_winsys_display_destroy;
  winsys_class->context_init = _cogl_winsys_context_init;
  winsys_class->context_deinit = _cogl_winsys_context_deinit;

  #if defined(EGL_KHR_fence_sync) || defined(EGL_KHR_reusable_sync)
  winsys_class->get_sync_fd = _cogl_winsys_get_sync_fd;
  winsys_class->update_sync = _cogl_winsys_update_sync;
#endif


}

static void
cogl_winsys_egl_init (CoglWinsysEGL *winsys_egl)
{
}

#ifdef EGL_KHR_image_base
EGLImageKHR
_cogl_egl_create_image (CoglContext *ctx,
                        EGLenum target,
                        EGLClientBuffer buffer,
                        const EGLint *attribs)
{
  CoglDisplayEGL *egl_display = ctx->display->winsys;
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (ctx->display->renderer);
  EGLContext egl_ctx;

  g_return_val_if_fail (egl_renderer->pf_eglCreateImage, EGL_NO_IMAGE_KHR);

  /* The EGL_KHR_image_pixmap spec explicitly states that EGL_NO_CONTEXT must
   * always be used in conjunction with the EGL_NATIVE_PIXMAP_KHR target */
#ifdef EGL_KHR_image_pixmap
  if (target == EGL_NATIVE_PIXMAP_KHR)
    egl_ctx = EGL_NO_CONTEXT;
  else
#endif
#ifdef EGL_WL_bind_wayland_display
  /* The WL_bind_wayland_display spec states that EGL_NO_CONTEXT is to be used
   * in conjunction with the EGL_WAYLAND_BUFFER_WL target */
  if (target == EGL_WAYLAND_BUFFER_WL)
    egl_ctx = EGL_NO_CONTEXT;
  else
#endif
    egl_ctx = egl_display->egl_context;

  return egl_renderer->pf_eglCreateImage (egl_renderer->edpy,
                                          egl_ctx,
                                          target,
                                          buffer,
                                          attribs);
}

void
_cogl_egl_destroy_image (CoglContext *ctx,
                         EGLImageKHR image)
{
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (ctx->display->renderer);

  g_return_if_fail (egl_renderer->pf_eglDestroyImage);

  egl_renderer->pf_eglDestroyImage (egl_renderer->edpy, image);
}
#endif

EGLDisplay
cogl_context_get_egl_display (CoglContext *context)
{
  CoglRendererEGL *egl_renderer = cogl_renderer_get_winsys_data (context->display->renderer);

  return egl_renderer->edpy;
}
