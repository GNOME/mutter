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


G_DEFINE_QUARK (cogl-egl-error-quark, cogl_egl_error)

static const char *
get_egl_error_str (EGLint error_number)
{
  switch (error_number)
    {
    case EGL_SUCCESS:
      return "The last function succeeded without error.";
    case EGL_NOT_INITIALIZED:
      return "EGL is not initialized, or could not be initialized, for the specified EGL display connection.";
    case EGL_BAD_ACCESS:
      return "EGL cannot access a requested resource.";
    case EGL_BAD_ALLOC:
      return "EGL failed to allocate resources for the requested operation.";
    case EGL_BAD_ATTRIBUTE:
      return "An unrecognized attribute or attribute value was passed in the attribute list.";
    case EGL_BAD_CONTEXT:
      return "An EGLContext argument does not name a valid EGL rendering context.";
    case EGL_BAD_CONFIG:
      return "An EGLConfig argument does not name a valid EGL frame buffer configuration.";
    case EGL_BAD_CURRENT_SURFACE:
      return "The current surface of the calling thread is no longer valid.";
    case EGL_BAD_DISPLAY:
      return "An EGLDisplay argument does not name a valid EGL display connection.";
    case EGL_BAD_SURFACE:
      return "An EGLSurface argument does not name a valid surface configured for GL rendering.";
    case EGL_BAD_MATCH:
      return "Arguments are inconsistent.";
    case EGL_BAD_PARAMETER:
      return "One or more argument values are invalid.";
    case EGL_BAD_NATIVE_PIXMAP:
      return "A NativePixmapType argument does not refer to a valid native pixmap.";
    case EGL_BAD_NATIVE_WINDOW:
      return "A NativeWindowType argument does not refer to a valid native window.";
    case EGL_CONTEXT_LOST:
      return "A power management event has occurred.";
    default:
      return "Unknown EGL error";
    }
}

static void
set_egl_error (GError **error)
{
  EGLint error_number;

  if (!error)
    return;

  error_number = eglGetError ();
  if (error_number == EGL_SUCCESS)
    return;

  g_set_error_literal (error, COGL_EGL_ERROR,
                       error_number,
                       get_egl_error_str (error_number));
}

static gboolean
is_egl_proc_valid (void    *proc,
                   GError **error)
{
  if (proc)
    return TRUE;

  g_set_error_literal (error, COGL_EGL_ERROR, EGL_BAD_ACCESS,
                       "EGL extension function pointer not available");
  return FALSE;
}

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

  PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
  PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;

  PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
  PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;

  PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT;
  PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT;

  PFNEGLQUERYDISPLAYATTRIBEXTPROC eglQueryDisplayAttribEXT;

  PFNEGLQUERYDEVICESTRINGEXTPROC eglQueryDeviceStringEXT;

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
cogl_renderer_egl_init_extensions (CoglRenderer *renderer)
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

  if (_cogl_check_extension ("EGL_KHR_image_base", split_extensions))
    {
      GET_EGL_PROC_ADDR (eglCreateImageKHR);
      GET_EGL_PROC_ADDR (eglDestroyImageKHR);
    }

  if (_cogl_check_extension ("EGL_WL_bind_wayland_display", split_extensions))
    {
      GET_EGL_PROC_ADDR (eglBindWaylandDisplayWL);
      GET_EGL_PROC_ADDR (eglQueryWaylandBufferWL);
    }

  if (_cogl_check_extension ("EGL_EXT_image_dma_buf_import_modifiers", split_extensions))
    {
      GET_EGL_PROC_ADDR (eglQueryDmaBufFormatsEXT);
      GET_EGL_PROC_ADDR (eglQueryDmaBufModifiersEXT);
    }

  GET_EGL_PROC_ADDR (eglQueryDisplayAttribEXT);
  GET_EGL_PROC_ADDR (eglQueryDeviceStringEXT);

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

  cogl_renderer_egl_init_extensions (renderer);

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
cogl_renderer_egl_swap_buffers_with_damage (CoglRendererEGL  *renderer_egl,
                                            EGLSurface        surface,
                                            const EGLint     *rects,
                                            EGLint            n_rects,
                                            GError          **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (priv->eglSwapBuffersWithDamageKHR)
    {
      if (priv->eglSwapBuffersWithDamageKHR (priv->edisplay, surface,
                                             rects, n_rects) == EGL_FALSE)
        {
          set_egl_error (error);
          return FALSE;
        }
      return TRUE;
    }

  if (priv->eglSwapBuffersWithDamageEXT)
    {
      if (priv->eglSwapBuffersWithDamageEXT (priv->edisplay, surface,
                                             rects, n_rects) == EGL_FALSE)
        {
          set_egl_error (error);
          return FALSE;
        }
      return TRUE;
    }

  return FALSE;
}

gboolean
cogl_renderer_egl_swap_buffers_region (CoglRendererEGL  *renderer_egl,
                                       EGLSurface        surface,
                                       EGLint            n_rects,
                                       const EGLint     *rects,
                                       GError          **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!priv->eglSwapBuffersRegionNOK)
    return FALSE;

  if (priv->eglSwapBuffersRegionNOK (priv->edisplay, surface,
                                     n_rects, rects) == EGL_FALSE)
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
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

gboolean
cogl_renderer_egl_has_extensions (CoglRendererEGL  *renderer_egl,
                                  const char      ***missing_extensions,
                                  const char       *first_extension,
                                  ...)
{
  CoglRendererEGLPrivate *priv;
  const char *extensions_str;
  char **extensions;
  const char *extension;
  va_list var_args;
  size_t num_missing = 0;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  extensions_str = eglQueryString (priv->edisplay, EGL_EXTENSIONS);
  if (!extensions_str)
    return FALSE;

  if (missing_extensions)
    *missing_extensions = NULL;

  extensions = g_strsplit (extensions_str, " ", -1);

  va_start (var_args, first_extension);
  extension = first_extension;
  while (extension)
    {
      if (!g_strv_contains ((const char * const *) extensions, extension))
        {
          num_missing++;
          if (missing_extensions)
            {
              *missing_extensions = g_realloc_n (*missing_extensions,
                                                  num_missing + 1,
                                                  sizeof (const char *));
              (*missing_extensions)[num_missing - 1] = extension;
              (*missing_extensions)[num_missing] = NULL;
            }
          else
            {
              break;
            }
        }
      extension = va_arg (var_args, char *);
    }
  va_end (var_args);

  g_strfreev (extensions);

  return num_missing == 0;
}

const char *
cogl_renderer_egl_query_string (CoglRendererEGL *renderer_egl,
                                EGLint           name)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), NULL);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  return eglQueryString (priv->edisplay, name);
}

gboolean
cogl_renderer_egl_choose_first_config (CoglRendererEGL  *renderer_egl,
                                       const EGLint     *attrib_list,
                                       EGLConfig        *chosen_config,
                                       GError          **error)
{
  CoglRendererEGLPrivate *priv;
  EGLint num_configs;
  EGLBoolean status;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  status = eglChooseConfig (priv->edisplay, attrib_list,
                            chosen_config, 1, &num_configs);
  if (status != EGL_TRUE || num_configs < 1)
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLConfig *
cogl_renderer_egl_choose_all_configs (CoglRendererEGL  *renderer_egl,
                                      const EGLint     *attrib_list,
                                      EGLint           *out_num_configs,
                                      GError          **error)
{
  CoglRendererEGLPrivate *priv;
  EGLint num_configs;
  EGLConfig *configs;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), NULL);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!eglChooseConfig (priv->edisplay, attrib_list, NULL, 0, &num_configs))
    {
      set_egl_error (error);
      return NULL;
    }

  if (num_configs == 0)
    return NULL;

  configs = g_new0 (EGLConfig, num_configs);
  eglChooseConfig (priv->edisplay, attrib_list,
                   configs, num_configs, &num_configs);

  *out_num_configs = num_configs;
  return configs;
}

gboolean
cogl_renderer_egl_get_config_attrib (CoglRendererEGL  *renderer_egl,
                                     EGLConfig         config,
                                     EGLint            attribute,
                                     EGLint           *value,
                                     GError          **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!eglGetConfigAttrib (priv->edisplay, config, attribute, value))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLSurface
cogl_renderer_egl_create_window_surface (CoglRendererEGL     *renderer_egl,
                                         EGLConfig            config,
                                         EGLNativeWindowType  native_window,
                                         const EGLint        *attrib_list,
                                         GError             **error)
{
  CoglRendererEGLPrivate *priv;
  EGLSurface surface;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), EGL_NO_SURFACE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  surface = eglCreateWindowSurface (priv->edisplay, config,
                                    native_window, attrib_list);
  if (surface == EGL_NO_SURFACE)
    {
      set_egl_error (error);
      return EGL_NO_SURFACE;
    }

  return surface;
}

EGLSurface
cogl_renderer_egl_create_pbuffer_surface (CoglRendererEGL  *renderer_egl,
                                          EGLConfig         config,
                                          const EGLint     *attrib_list,
                                          GError          **error)
{
  CoglRendererEGLPrivate *priv;
  EGLSurface surface;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), EGL_NO_SURFACE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  surface = eglCreatePbufferSurface (priv->edisplay, config, attrib_list);
  if (surface == EGL_NO_SURFACE)
    {
      set_egl_error (error);
      return EGL_NO_SURFACE;
    }

  return surface;
}

gboolean
cogl_renderer_egl_destroy_surface (CoglRendererEGL  *renderer_egl,
                                   EGLSurface        surface,
                                   GError          **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!eglDestroySurface (priv->edisplay, surface))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLImageKHR
cogl_renderer_egl_create_image (CoglRendererEGL  *renderer_egl,
                                EGLContext        context,
                                EGLenum           target,
                                EGLClientBuffer   buffer,
                                const EGLint     *attrib_list,
                                GError          **error)
{
  CoglRendererEGLPrivate *priv;
  EGLImageKHR image;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), EGL_NO_IMAGE_KHR);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!is_egl_proc_valid (priv->eglCreateImageKHR, error))
    return EGL_NO_IMAGE_KHR;

  image = priv->eglCreateImageKHR (priv->edisplay, context,
                                   target, buffer, attrib_list);
  if (image == EGL_NO_IMAGE_KHR)
    {
      set_egl_error (error);
      return EGL_NO_IMAGE_KHR;
    }

  return image;
}

gboolean
cogl_renderer_egl_destroy_image (CoglRendererEGL  *renderer_egl,
                                 EGLImageKHR       image,
                                 GError          **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!is_egl_proc_valid (priv->eglDestroyImageKHR, error))
    return FALSE;

  if (!priv->eglDestroyImageKHR (priv->edisplay, image))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

EGLImageKHR
cogl_renderer_egl_create_dmabuf_image (CoglRendererEGL  *renderer_egl,
                                       unsigned int      width,
                                       unsigned int      height,
                                       uint32_t          drm_format,
                                       uint32_t          n_planes,
                                       const int        *fds,
                                       const uint32_t   *strides,
                                       const uint32_t   *offsets,
                                       const uint64_t   *modifiers,
                                       GError          **error)
{
  EGLint attribs[39];
  int atti = 0;

  attribs[atti++] = EGL_WIDTH;
  attribs[atti++] = width;
  attribs[atti++] = EGL_HEIGHT;
  attribs[atti++] = height;
  attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
  attribs[atti++] = drm_format;
  attribs[atti++] = EGL_IMAGE_PRESERVED_KHR;
  attribs[atti++] = EGL_TRUE;

  if (n_planes > 0)
    {
      attribs[atti++] = EGL_DMA_BUF_PLANE0_FD_EXT;
      attribs[atti++] = fds[0];
      attribs[atti++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
      attribs[atti++] = offsets[0];
      attribs[atti++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
      attribs[atti++] = strides[0];
      if (modifiers)
        {
          attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
          attribs[atti++] = modifiers[0] & 0xFFFFFFFF;
          attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
          attribs[atti++] = modifiers[0] >> 32;
        }
    }

  if (n_planes > 1)
    {
      attribs[atti++] = EGL_DMA_BUF_PLANE1_FD_EXT;
      attribs[atti++] = fds[1];
      attribs[atti++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
      attribs[atti++] = offsets[1];
      attribs[atti++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
      attribs[atti++] = strides[1];
      if (modifiers)
        {
          attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
          attribs[atti++] = modifiers[1] & 0xFFFFFFFF;
          attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
          attribs[atti++] = modifiers[1] >> 32;
        }
    }

  if (n_planes > 2)
    {
      attribs[atti++] = EGL_DMA_BUF_PLANE2_FD_EXT;
      attribs[atti++] = fds[2];
      attribs[atti++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
      attribs[atti++] = offsets[2];
      attribs[atti++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
      attribs[atti++] = strides[2];
      if (modifiers)
        {
          attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
          attribs[atti++] = modifiers[2] & 0xFFFFFFFF;
          attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
          attribs[atti++] = modifiers[2] >> 32;
        }
    }

  attribs[atti++] = EGL_NONE;
  g_assert (atti <= G_N_ELEMENTS (attribs));

  return cogl_renderer_egl_create_image (renderer_egl, EGL_NO_CONTEXT,
                                         EGL_LINUX_DMA_BUF_EXT, NULL,
                                         attribs, error);
}

gboolean
cogl_renderer_egl_bind_wayland_display (CoglRendererEGL  *renderer_egl,
                                        struct wl_display *wayland_display,
                                        GError           **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!is_egl_proc_valid (priv->eglBindWaylandDisplayWL, error))
    return FALSE;

  if (!priv->eglBindWaylandDisplayWL (priv->edisplay, wayland_display))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
cogl_renderer_egl_query_wayland_buffer (CoglRendererEGL     *renderer_egl,
                                        struct wl_resource  *buffer,
                                        EGLint               attribute,
                                        EGLint              *value,
                                        GError             **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!is_egl_proc_valid (priv->eglQueryWaylandBufferWL, error))
    return FALSE;

  if (!priv->eglQueryWaylandBufferWL (priv->edisplay, buffer, attribute, value))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
cogl_renderer_egl_query_dma_buf_formats (CoglRendererEGL  *renderer_egl,
                                         EGLint            max_formats,
                                         EGLint           *formats,
                                         EGLint           *num_formats,
                                         GError          **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!is_egl_proc_valid (priv->eglQueryDmaBufFormatsEXT, error))
    return FALSE;

  if (!priv->eglQueryDmaBufFormatsEXT (priv->edisplay, max_formats,
                                       formats, num_formats))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
cogl_renderer_egl_query_dma_buf_modifiers (CoglRendererEGL  *renderer_egl,
                                           EGLint            format,
                                           EGLint            max_modifiers,
                                           EGLuint64KHR     *modifiers,
                                           EGLBoolean       *external_only,
                                           EGLint           *num_modifiers,
                                           GError          **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!is_egl_proc_valid (priv->eglQueryDmaBufModifiersEXT, error))
    return FALSE;

  if (!priv->eglQueryDmaBufModifiersEXT (priv->edisplay, format,
                                         max_modifiers, modifiers,
                                         external_only, num_modifiers))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
cogl_renderer_egl_query_display_attrib (CoglRendererEGL  *renderer_egl,
                                        EGLint            attribute,
                                        EGLAttrib        *value,
                                        GError          **error)
{
  CoglRendererEGLPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!is_egl_proc_valid (priv->eglQueryDisplayAttribEXT, error))
    return FALSE;

  if (!priv->eglQueryDisplayAttribEXT (priv->edisplay, attribute, value))
    {
      set_egl_error (error);
      return FALSE;
    }

  return TRUE;
}

gboolean
cogl_renderer_egl_query_device_string (CoglRendererEGL  *renderer_egl,
                                       EGLDeviceEXT      device,
                                       EGLint            name,
                                       const char      **out_string,
                                       GError          **error)
{
  CoglRendererEGLPrivate *priv;
  g_autoptr (GError) local_error = NULL;
  const char *device_string;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);

  if (!is_egl_proc_valid (priv->eglQueryDeviceStringEXT, error))
    return FALSE;

  device_string = priv->eglQueryDeviceStringEXT (device, name);
  if (!device_string)
    {
      set_egl_error (&local_error);
      if (local_error)
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }
    }

  *out_string = device_string;
  return TRUE;
}

gboolean
cogl_renderer_egl_device_has_extensions (CoglRendererEGL  *renderer_egl,
                                         EGLDeviceEXT      device,
                                         const char      ***missing_extensions,
                                         const char       *first_extension,
                                         ...)
{
  va_list var_args;
  const char *extensions_str;
  char **extensions;
  const char *extension;
  size_t num_missing = 0;
  g_autoptr (GError) error = NULL;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  if (!cogl_renderer_egl_query_device_string (renderer_egl, device,
                                              EGL_EXTENSIONS,
                                              &extensions_str, &error))
    {
      g_warning ("Failed to query device string: %s", error->message);
      return FALSE;
    }

  if (!extensions_str)
    {
      g_warning ("EGL_EXTENSIONS device string returned NULL");
      return FALSE;
    }

  if (missing_extensions)
    *missing_extensions = NULL;

  extensions = g_strsplit (extensions_str, " ", -1);

  va_start (var_args, first_extension);
  extension = first_extension;
  while (extension)
    {
      if (!g_strv_contains ((const char * const *) extensions, extension))
        {
          num_missing++;
          if (missing_extensions)
            {
              *missing_extensions = g_realloc_n (*missing_extensions,
                                                  num_missing + 1,
                                                  sizeof (const char *));
              (*missing_extensions)[num_missing - 1] = extension;
              (*missing_extensions)[num_missing] = NULL;
            }
          else
            {
              break;
            }
        }
      extension = va_arg (var_args, char *);
    }
  va_end (var_args);

  g_strfreev (extensions);

  return num_missing == 0;
}
