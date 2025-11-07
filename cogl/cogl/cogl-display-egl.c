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

#include "cogl/cogl-display-egl.h"
#include "cogl/cogl-display-egl-private.h"
#include "cogl/cogl-renderer-egl.h"
#include "cogl/cogl-renderer-egl-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/winsys/cogl-winsys-egl.h"

typedef struct _CoglDisplayEGLPrivate
{
  EGLDisplay platform_display;
  EGLConfig egl_config;
  EGLContext egl_context;
  EGLSurface dummy_surface;
  EGLSurface current_draw_surface;
  EGLSurface current_read_surface;
  EGLContext current_context;
} CoglDisplayEGLPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CoglDisplayEGL, cogl_display_egl, COGL_TYPE_DISPLAY)

static void
cogl_display_egl_init (CoglDisplayEGL *display_egl)
{
}

static void
cleanup_context (CoglDisplay *display)
{
  CoglDisplayEGL *display_egl = COGL_DISPLAY_EGL (display);
  CoglRenderer *renderer = cogl_display_get_renderer (display);
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
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
    egl_class->cleanup_context (COGL_WINSYS_EGL (winsys), display);
}

static gboolean
try_create_context (CoglDisplay  *display,
                    GError      **error)
{
  CoglRenderer *renderer = cogl_display_get_renderer (display);
  CoglWinsys *winsys = cogl_renderer_get_winsys (renderer);
  CoglDisplayEGL *egl_display = COGL_DISPLAY_EGL (display);
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);
  CoglWinsysEGLClass *winsys_egl_class = COGL_WINSYS_EGL_GET_CLASS (winsys);
  CoglDisplayEGLClass *display_egl_class = COGL_DISPLAY_EGL_GET_CLASS (display);
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

  if (winsys_egl_class->context_created &&
      !winsys_egl_class->context_created (COGL_WINSYS_EGL (winsys), display, error))
    return FALSE;

  return TRUE;

fail:
  g_set_error (error, COGL_WINSYS_ERROR,
               COGL_WINSYS_ERROR_CREATE_CONTEXT,
               "%s", error_message);

err:
  cleanup_context (display);

  return FALSE;
}

static void
cogl_display_egl_destroy (CoglDisplay *display)
{
  CoglRenderer *renderer = cogl_display_get_renderer (display);
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEglPrivate *priv_renderer =
    cogl_renderer_egl_get_private (renderer_egl);
  EGLSyncKHR sync = cogl_renderer_egl_get_sync (renderer_egl);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);

  if (sync != EGL_NO_SYNC_KHR)
    priv_renderer->pf_eglDestroySync (edpy, sync);

  cleanup_context (display);
}

static gboolean
cogl_display_egl_setup (CoglDisplay  *display,
                        GError      **error)
{
  if (!try_create_context (display, error))
    goto error;

  return TRUE;

error:
  cogl_display_egl_destroy (display);
  return FALSE;
}

static void
cogl_display_egl_class_init (CoglDisplayEGLClass *class)
{
  CoglDisplayClass *display_class = COGL_DISPLAY_CLASS (class);

  display_class->setup = cogl_display_egl_setup;
  display_class->destroy = cogl_display_egl_destroy;
}

CoglDisplayEGL *
cogl_display_egl_new (CoglRenderer *renderer)
{
  CoglDisplayEGL *display_egl;

  g_return_val_if_fail (renderer != NULL, NULL);

  display_egl = g_object_new (COGL_TYPE_DISPLAY_EGL,
                              "renderer", renderer,
                              NULL);

  return display_egl;
}

EGLContext
cogl_display_egl_get_egl_context (CoglDisplayEGL *display_egl)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  return priv->egl_context;
}

void
cogl_display_egl_set_egl_context (CoglDisplayEGL *display_egl,
                                  EGLContext      egl_context)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  priv->egl_context = egl_context;
}

EGLConfig
cogl_display_egl_get_egl_config (CoglDisplayEGL *display_egl)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  return priv->egl_config;
}

void
cogl_display_egl_set_egl_config (CoglDisplayEGL *display_egl,
                                 EGLConfig       egl_config)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  priv->egl_config = egl_config;
}

EGLSurface
cogl_display_egl_get_dummy_surface (CoglDisplayEGL *display_egl)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  return priv->dummy_surface;
}

void
cogl_display_egl_set_dummy_surface (CoglDisplayEGL *display_egl,
                                    EGLSurface      dummy_surface)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  priv->dummy_surface = dummy_surface;
}

EGLSurface
cogl_display_egl_get_current_draw_surface (CoglDisplayEGL *display_egl)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  return priv->current_draw_surface;
}

void
cogl_display_egl_set_current_draw_surface (CoglDisplayEGL *display_egl,
                                           EGLSurface      current_draw_surface)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  priv->current_draw_surface = current_draw_surface;
}

EGLSurface
cogl_display_egl_get_current_read_surface (CoglDisplayEGL *display_egl)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  return priv->current_read_surface;
}

void
cogl_display_egl_set_current_read_surface (CoglDisplayEGL *display_egl,
                                           EGLSurface      current_read_surface)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  priv->current_read_surface = current_read_surface;
}

EGLContext
cogl_display_egl_get_current_context (CoglDisplayEGL *display_egl)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  return priv->current_context;
}

void
cogl_display_egl_set_current_context (CoglDisplayEGL *display_egl,
                                      EGLContext      current_context)
{
  CoglDisplayEGLPrivate *priv = cogl_display_egl_get_instance_private (display_egl);
  priv->current_context = current_context;
}

void
cogl_display_egl_determine_attributes (CoglDisplayEGL *display_egl,
                                       EGLint         *attributes)
{
  CoglDisplay *display = COGL_DISPLAY (display_egl);
  CoglRenderer *renderer = cogl_display_get_renderer (display);
  CoglDisplayEGLClass *display_class = COGL_DISPLAY_EGL_GET_CLASS (display_egl);
  int i = 0;

  /* Let the platform add attributes first, including setting the
   * EGL_SURFACE_TYPE */
  i = display_class->add_config_attributes (display_egl,
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
cogl_display_egl_make_current (CoglDisplayEGL *display_egl,
                               EGLSurface      draw,
                               EGLSurface      read,
                               EGLContext      context)
{
  CoglDisplay *display = COGL_DISPLAY (display_egl);
  CoglRenderer *renderer = cogl_display_get_renderer (display);
  EGLDisplay egl_display =
    cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (renderer));
  EGLBoolean ret;

  if (cogl_display_egl_get_current_draw_surface (display_egl) == draw &&
      cogl_display_egl_get_current_read_surface (display_egl) == read &&
      cogl_display_egl_get_current_context (display_egl) == context)
    return EGL_TRUE;

  ret = eglMakeCurrent (egl_display,
                        draw,
                        read,
                        context);

  cogl_display_egl_set_current_draw_surface (display_egl, draw);
  cogl_display_egl_set_current_read_surface (display_egl, read);
  cogl_display_egl_set_current_context (display_egl, context);

  return ret;
}

EGLBoolean
cogl_display_egl_ensure_current (CoglDisplayEGL *display_egl)
{
  CoglDisplay *display = COGL_DISPLAY (display_egl);
  CoglRenderer *renderer = cogl_display_get_renderer (display);
  EGLDisplay egl_display =
    cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (renderer));

  return eglMakeCurrent (egl_display,
                         cogl_display_egl_get_current_draw_surface (display_egl),
                         cogl_display_egl_get_current_read_surface (display_egl),
                         cogl_display_egl_get_current_context (display_egl));
}
