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
cogl_display_egl_class_init (CoglDisplayEGLClass *class)
{
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
