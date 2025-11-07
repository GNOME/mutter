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
#include "cogl/cogl-feature-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-driver-private.h"

#ifdef HAVE_GL
#include "cogl/driver/gl/gl3/cogl-driver-gl3-private.h"
#endif
#ifdef HAVE_GLES2
#include "cogl/driver/gl/gles2/cogl-driver-gles2-private.h"
#endif

#include "cogl/cogl-renderer-egl-private.h"

G_DEFINE_TYPE_WITH_PRIVATE (CoglRendererEgl, cogl_renderer_egl, COGL_TYPE_RENDERER)

static void
cogl_renderer_egl_dispose (GObject *object)
{
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (object);
  CoglRendererEglPrivate *priv =
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
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEglPrivate *priv =
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
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEglPrivate *priv =
    cogl_renderer_egl_get_instance_private (renderer_egl);
  GCallback result = eglGetProcAddress (name);

  if (result == NULL)
    g_module_symbol (priv->libgl_module,
                     name, (gpointer *)&result);

  return result;
}

/* Updates all the function pointers */
void
cogl_renderer_egl_check_extensions (CoglRenderer *renderer)
{
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEglPrivate *priv =
    cogl_renderer_egl_get_instance_private (renderer_egl);
  const char *egl_extensions;
  char **split_extensions;
  int i;

  egl_extensions = eglQueryString (priv->edisplay, EGL_EXTENSIONS);
  split_extensions = g_strsplit (egl_extensions, " ", 0 /* max_tokens */);

  COGL_NOTE (WINSYS, "  EGL Extensions: %s", egl_extensions);

  priv->private_features = 0;
  for (i = 0; i < G_N_ELEMENTS (winsys_feature_data); i++)
    if (_cogl_feature_check (renderer,
                             "EGL", winsys_feature_data + i, 0, 0,
                             COGL_DRIVER_ID_GL3, /* the driver isn't used */
                             split_extensions,
                             priv))
      {
        priv->private_features |=
          winsys_feature_data[i].feature_flags_private;
      }

  g_strfreev (split_extensions);
}

static gboolean
cogl_renderer_egl_connect (CoglRenderer   *renderer,
                           GError       **error)
{
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEglPrivate *priv =
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

static void
cogl_renderer_egl_class_init (CoglRendererEglClass *class)
{
  CoglRendererClass *renderer_class =
    COGL_RENDERER_CLASS (class);
  GObjectClass * object_class = G_OBJECT_CLASS (class);

  renderer_class->bind_api = cogl_renderer_egl_bind_api;
  renderer_class->load_driver = cogl_renderer_egl_load_driver;
  renderer_class->get_proc_address = cogl_renderer_egl_get_proc_address;
  renderer_class->connect = cogl_renderer_egl_connect;

  object_class->dispose = cogl_renderer_egl_dispose;
}

static void
cogl_renderer_egl_init (CoglRendererEgl *renderer_egl)
{
}

CoglRendererEgl *
cogl_renderer_egl_new (void)
{
  return g_object_new (COGL_TYPE_RENDERER_EGL, NULL);
}

CoglRendererEglPrivate *
cogl_renderer_egl_get_private (CoglRendererEgl *renderer_egl)
{
  return cogl_renderer_egl_get_instance_private (renderer_egl);
}

void
cogl_renderer_egl_set_edisplay (CoglRendererEgl *renderer_egl,
                                EGLDisplay       edisplay)
{
  CoglRendererEglPrivate *priv;

  g_return_if_fail (COGL_IS_RENDERER_EGL (renderer_egl));

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  priv->edisplay = edisplay;
}

EGLDisplay
cogl_renderer_egl_get_edisplay (CoglRendererEgl *renderer_egl)
{
  CoglRendererEglPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), EGL_NO_DISPLAY);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  return priv->edisplay;
}

void
cogl_renderer_egl_set_needs_config (CoglRendererEgl *renderer_egl,
                                    gboolean         needs_config)
{
  CoglRendererEglPrivate *priv;

  g_return_if_fail (COGL_IS_RENDERER_EGL (renderer_egl));

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  priv->needs_config = needs_config;
}

gboolean
cogl_renderer_egl_get_needs_config (CoglRendererEgl *renderer_egl)
{
  CoglRendererEglPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  return priv->needs_config;
}

EGLSyncKHR
cogl_renderer_egl_get_sync (CoglRendererEgl *renderer_egl)
{
  CoglRendererEglPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), 0);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  return priv->sync;
}

gboolean
cogl_renderer_egl_has_feature (CoglRendererEgl      *renderer_egl,
                               CoglEGLWinsysFeature  feature)
{
  CoglRendererEglPrivate *priv;

  g_return_val_if_fail (COGL_IS_RENDERER_EGL (renderer_egl), FALSE);

  priv = cogl_renderer_egl_get_instance_private (renderer_egl);
  return !!(priv->private_features & feature);
}
