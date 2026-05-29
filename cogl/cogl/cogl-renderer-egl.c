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

#include "config.h"

#include "cogl/cogl-renderer-egl.h"
#include "cogl/cogl-debug.h"
#include "cogl/cogl-driver-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-private.h"

#ifdef HAVE_GL
#include "cogl/driver/gl/gl3/cogl-driver-gl3-private.h"
#endif
#ifdef HAVE_GLES2
#include "cogl/driver/gl/gles2/cogl-driver-gles2-private.h"
#endif

#include "cogl/cogl-renderer-egl-private.h"

typedef struct _CoglRendererEGLPrivate
{
  GModule *libgl_module;

  CoglEGLWinsysFeature private_features;

  EGLDisplay edisplay;

  EGLint egl_version_major;
  EGLint egl_version_minor;

  gboolean needs_config;

  /* Sync for latest submitted work */
  EGLSyncKHR sync;

  /* EGL extension function pointers */
  PFNEGLSWAPBUFFERSREGIONNOKPROC eglSwapBuffersRegionNOK;

  PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC eglSwapBuffersWithDamageKHR;
  PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC eglSwapBuffersWithDamageEXT;

  PFNEGLSETDAMAGEREGIONKHRPROC eglSetDamageRegionKHR;

#if defined(EGL_KHR_fence_sync) || defined(EGL_KHR_reusable_sync)
  PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
  PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
#endif

#ifdef EGL_ANDROID_native_fence_sync
  PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID;
#endif
} CoglRendererEGLPrivate;


G_DEFINE_TYPE_WITH_PRIVATE (CoglRendererEGL, cogl_renderer_egl, COGL_TYPE_RENDERER)

static void
cogl_renderer_egl_dispose (GObject *object)
{
  CoglRendererEGL *renderer_egl = COGL_RENDERER_EGL (object);
  CoglRendererEGLPrivate *priv =
    cogl_renderer_egl_get_instance_private (renderer_egl);

  if (priv->libgl_module)
    g_module_close (priv->libgl_module);

  G_OBJECT_CLASS (cogl_renderer_egl_parent_class)->dispose (object);
}

static void
cogl_renderer_egl_bind_api (CoglRenderer *renderer)
{
  if (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GL3)
    eglBindAPI (EGL_OPENGL_API);
  else if (cogl_renderer_get_driver_id (renderer) == COGL_DRIVER_ID_GLES2)
    eglBindAPI (EGL_OPENGL_ES_API);
}

static gboolean
cogl_renderer_egl_load_driver (CoglRenderer  *renderer,
                               CoglDriverId   driver_id,
                               GError       **error)
{
  g_return_val_if_fail (driver_id != COGL_DRIVER_ID_ANY, FALSE);

  CoglRendererEGL *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEGLPrivate *priv =
    cogl_renderer_egl_get_instance_private (renderer_egl);
  const char *libgl_name = NULL;
  CoglDriver *driver = NULL;

#ifdef HAVE_GL
  if (driver_id == COGL_DRIVER_ID_GL3)
    {
      driver = g_object_new (COGL_TYPE_DRIVER_GL3, NULL);
      libgl_name = COGL_GL_LIBNAME;
    }
#endif
#ifdef HAVE_GLES2
  if (driver_id == COGL_DRIVER_ID_GLES2)
    {
      driver = g_object_new (COGL_TYPE_DRIVER_GLES2, NULL);
      libgl_name = COGL_GLES2_LIBNAME;
    }
#endif

  /* Return early to fallback to NOP driver */
  if (driver == NULL)
    return FALSE;

  cogl_renderer_set_driver (renderer, driver);

  if (libgl_name)
    {
      priv->libgl_module = g_module_open (libgl_name,
                                          G_MODULE_BIND_LAZY);

      if (priv->libgl_module == NULL)
        {
          g_set_error (error, COGL_DRIVER_ERROR,
                       COGL_DRIVER_ERROR_FAILED_TO_LOAD_LIBRARY,
                       "Failed to dynamically open the GL library \"%s\"",
                       libgl_name);
          return FALSE;
        }
    }

  return TRUE;
}

static GCallback
cogl_renderer_egl_get_proc_address (CoglRenderer *renderer,
                                    const char   *name)
{
  CoglRendererEGL *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEGLPrivate *priv =
    cogl_renderer_egl_get_instance_private (renderer_egl);
  GCallback result = eglGetProcAddress (name);

  if (result == NULL)
    g_module_symbol (priv->libgl_module,
                     name, (gpointer *)&result);

  return result;
}

#define GET_EGL_PROC_ADDR(proc) \
  priv->proc = (void *) cogl_renderer_get_proc_address (renderer, #proc)

void
cogl_renderer_egl_check_extensions (CoglRenderer *renderer)
{
  CoglRendererEGL *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEGLPrivate *priv =
    cogl_renderer_egl_get_instance_private (renderer_egl);
  const char *egl_extensions;
  char **split_extensions;

  egl_extensions = eglQueryString (priv->edisplay, EGL_EXTENSIONS);
  split_extensions = g_strsplit (egl_extensions, " ", 0 /* max_tokens */);

  COGL_NOTE (WINSYS, "  EGL Extensions: %s", egl_extensions);

  priv->private_features = 0;

  if (_cogl_check_extension ("EGL_NOK_swap_region", split_extensions))
    {
      GET_EGL_PROC_ADDR (eglSwapBuffersRegionNOK);
      if (priv->eglSwapBuffersRegionNOK)
        priv->private_features |= COGL_EGL_WINSYS_FEATURE_SWAP_REGION;
    }

  if (_cogl_check_extension ("EGL_KHR_swap_buffers_with_damage", split_extensions))
    GET_EGL_PROC_ADDR (eglSwapBuffersWithDamageKHR);

  if (_cogl_check_extension ("EGL_EXT_swap_buffers_with_damage", split_extensions))
    GET_EGL_PROC_ADDR (eglSwapBuffersWithDamageEXT);

  if (_cogl_check_extension ("EGL_KHR_partial_update", split_extensions))
    {
      GET_EGL_PROC_ADDR (eglSetDamageRegionKHR);
      if (priv->eglSetDamageRegionKHR)
        priv->private_features |= COGL_EGL_WINSYS_FEATURE_BUFFER_AGE;
    }

  if (_cogl_check_extension ("EGL_KHR_create_context", split_extensions))
    priv->private_features |= COGL_EGL_WINSYS_FEATURE_CREATE_CONTEXT;

  if (_cogl_check_extension ("EGL_KHR_no_config_context", split_extensions))
    priv->private_features |= COGL_EGL_WINSYS_FEATURE_NO_CONFIG_CONTEXT;

  if (_cogl_check_extension ("EGL_EXT_buffer_age", split_extensions))
    priv->private_features |= COGL_EGL_WINSYS_FEATURE_BUFFER_AGE;

#if defined(EGL_KHR_fence_sync) || defined(EGL_KHR_reusable_sync)
  if (_cogl_check_extension ("EGL_KHR_fence_sync", split_extensions))
    {
      GET_EGL_PROC_ADDR (eglCreateSyncKHR);
      GET_EGL_PROC_ADDR (eglDestroySyncKHR);
      if (priv->eglCreateSyncKHR && priv->eglDestroySyncKHR)
        priv->private_features |= COGL_EGL_WINSYS_FEATURE_FENCE_SYNC;
    }
#endif

#ifdef EGL_ANDROID_native_fence_sync
  if (_cogl_check_extension ("EGL_ANDROID_native_fence_sync", split_extensions))
    {
      GET_EGL_PROC_ADDR (eglDupNativeFenceFDANDROID);
      if (priv->eglDupNativeFenceFDANDROID)
        priv->private_features |= COGL_EGL_WINSYS_FEATURE_NATIVE_FENCE_SYNC;
    }
#endif

  if (_cogl_check_extension ("EGL_KHR_surfaceless_context", split_extensions))
    priv->private_features |= COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT;

  if (_cogl_check_extension ("EGL_IMG_context_priority", split_extensions))
    priv->private_features |= COGL_EGL_WINSYS_FEATURE_CONTEXT_PRIORITY;

  g_strfreev (split_extensions);
}

static gboolean
cogl_renderer_egl_connect (CoglRenderer   *renderer,
                           GError        **error)
{
  CoglRendererEGL *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEGLPrivate *priv =
    cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!eglInitialize (priv->edisplay,
                      &priv->egl_version_major,
                      &priv->egl_version_minor))
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_INIT,
                   "Couldn't initialize EGL");
      return FALSE;
    }

  cogl_renderer_egl_check_extensions (renderer);

  return TRUE;
}

#if defined(EGL_KHR_fence_sync) || defined(EGL_KHR_reusable_sync)

static int
cogl_renderer_egl_get_sync_fd (CoglRenderer *renderer)
{
  CoglRendererEGL *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEGLPrivate *priv =
    cogl_renderer_egl_get_instance_private (renderer_egl);
  int fd;

  if (!priv->eglDupNativeFenceFDANDROID)
    return -1;

  fd = priv->eglDupNativeFenceFDANDROID (priv->edisplay, priv->sync);
  if (fd == EGL_NO_NATIVE_FENCE_FD_ANDROID)
    return -1;

  return fd;
}

static void
cogl_renderer_egl_update_sync (CoglRenderer *renderer)
{
  CoglRendererEGL *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEGLPrivate *priv =
    cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!priv->eglDestroySyncKHR || !priv->eglCreateSyncKHR)
    return;

  if (priv->sync != EGL_NO_SYNC_KHR)
    priv->eglDestroySyncKHR (priv->edisplay, priv->sync);

  priv->sync = priv->eglCreateSyncKHR (priv->edisplay,
                                       EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
}
#endif

static void
cogl_renderer_egl_class_init (CoglRendererEGLClass *class)
{
  CoglRendererClass *renderer_class =
    COGL_RENDERER_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  renderer_class->bind_api = cogl_renderer_egl_bind_api;
  renderer_class->load_driver = cogl_renderer_egl_load_driver;
  renderer_class->get_proc_address = cogl_renderer_egl_get_proc_address;
  renderer_class->connect = cogl_renderer_egl_connect;

#if defined(EGL_KHR_fence_sync) || defined(EGL_KHR_reusable_sync)
  renderer_class->get_sync_fd = cogl_renderer_egl_get_sync_fd;
  renderer_class->update_sync = cogl_renderer_egl_update_sync;
#endif

  object_class->dispose = cogl_renderer_egl_dispose;
}

static void
cogl_renderer_egl_init (CoglRendererEGL *renderer_egl)
{
}

CoglRendererEGL *
cogl_renderer_egl_new (void)
{
  return g_object_new (COGL_TYPE_RENDERER_EGL, NULL);
}

void
cogl_renderer_egl_set_edisplay (CoglRendererEGL *renderer_egl,
                                EGLDisplay       edisplay)
{
  CoglRendererEGLPrivate *priv;

  g_return_if_fail (COGL_IS_RENDERER_EGL (renderer_egl));

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  priv->edisplay = edisplay;
}

EGLDisplay
cogl_renderer_egl_get_edisplay (CoglRendererEGL *renderer_egl)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), EGL_NO_DISPLAY);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  return priv->edisplay;
}

EGLSyncKHR
cogl_renderer_egl_get_sync (CoglRendererEGL *renderer_egl)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), 0);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  return priv->sync;
}

gboolean
cogl_renderer_egl_has_feature (CoglRendererEGL      *renderer_egl,
                               CoglEGLWinsysFeature  feature)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  return !!(priv->private_features & feature);
}

void
cogl_renderer_egl_destroy_sync (CoglRendererEGL *renderer_egl)
{
  CoglRendererEGLPrivate *priv;

  g_return_if_fail (COGL_IS_RENDERER_EGL (renderer_egl));

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

#if defined(EGL_KHR_fence_sync) || defined(EGL_KHR_reusable_sync)
  if (priv->sync != EGL_NO_SYNC_KHR && priv->eglDestroySyncKHR)
    {
      priv->eglDestroySyncKHR (priv->edisplay, priv->sync);
      priv->sync = EGL_NO_SYNC_KHR;
    }
#endif
}

gboolean
cogl_renderer_egl_has_swap_buffers_with_damage (CoglRendererEGL *renderer_egl)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  return priv->eglSwapBuffersWithDamageKHR ||
         priv->eglSwapBuffersWithDamageEXT;
}

gboolean
cogl_renderer_egl_swap_buffers_with_damage (CoglRendererEGL *renderer_egl,
                                            EGLSurface       surface,
                                            const EGLint    *rects,
                                            EGLint           n_rects)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (priv->eglSwapBuffersWithDamageKHR)
    return priv->eglSwapBuffersWithDamageKHR (priv->edisplay, surface,
                                              rects, n_rects) != EGL_FALSE;

  if (priv->eglSwapBuffersWithDamageEXT)
    return priv->eglSwapBuffersWithDamageEXT (priv->edisplay, surface,
                                              rects, n_rects) != EGL_FALSE;

  return FALSE;
}

gboolean
cogl_renderer_egl_swap_buffers_region (CoglRendererEGL *renderer_egl,
                                       EGLSurface       surface,
                                       EGLint           n_rects,
                                       const EGLint    *rects)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!priv->eglSwapBuffersRegionNOK)
    return FALSE;

  return priv->eglSwapBuffersRegionNOK (priv->edisplay, surface,
                                        n_rects, rects) != EGL_FALSE;
}

gboolean
cogl_renderer_egl_has_set_damage_region (CoglRendererEGL *renderer_egl)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  return priv->eglSetDamageRegionKHR != NULL;
}

gboolean
cogl_renderer_egl_set_damage_region (CoglRendererEGL *renderer_egl,
                                     EGLSurface       surface,
                                     const EGLint    *rects,
                                     EGLint           n_rects)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!priv->eglSetDamageRegionKHR)
    return FALSE;

  return priv->eglSetDamageRegionKHR (priv->edisplay, surface,
                                      (EGLint *) rects, n_rects) != EGL_FALSE;
}
