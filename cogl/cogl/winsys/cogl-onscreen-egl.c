/*
 * Copyright (C) 2007,2008,2009,2010,2011,2013 Intel Corporation.
 * Copyright (C) 2020 Red Hat
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

#include "cogl-config.h"

#include "winsys/cogl-onscreen-egl.h"

#include "cogl-context-private.h"
#include "cogl-renderer-private.h"
#include "cogl-trace.h"
#include "winsys/cogl-winsys-egl-private.h"

typedef struct _CoglOnscreenEGL
{
  EGLSurface egl_surface;

  /* Platform specific data */
  void *platform;
} CoglOnscreenEGL;

CoglOnscreenEGL *
cogl_onscreen_egl_new (void)
{
  return g_slice_new0 (CoglOnscreenEGL);
}

void
cogl_onscreen_egl_free (CoglOnscreenEGL *onscreen_egl)
{
  g_slice_free (CoglOnscreenEGL, onscreen_egl);
}

gboolean
_cogl_winsys_onscreen_egl_init (CoglOnscreen  *onscreen,
                                GError       **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *display = context->display;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  const CoglFramebufferConfig *config;
  EGLint attributes[MAX_EGL_CONFIG_ATTRIBS];
  EGLConfig egl_config;
  EGLint config_count = 0;
  EGLBoolean status;
  CoglOnscreenEGL *winsys;

  g_return_val_if_fail (egl_display->egl_context, FALSE);

  config = cogl_framebuffer_get_config (framebuffer);
  cogl_display_egl_determine_attributes (display, config, attributes);

  status = eglChooseConfig (egl_renderer->edpy,
                            attributes,
                            &egl_config, 1,
                            &config_count);
  if (status != EGL_TRUE || config_count == 0)
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                   "Failed to find a suitable EGL configuration");
      return FALSE;
    }

  /* Update the real number of samples_per_pixel now that we have
   * found an egl_config... */
  if (config->samples_per_pixel)
    {
      EGLint samples;
      status = eglGetConfigAttrib (egl_renderer->edpy,
                                   egl_config,
                                   EGL_SAMPLES, &samples);
      g_return_val_if_fail (status == EGL_TRUE, TRUE);
      cogl_framebuffer_update_samples_per_pixel (framebuffer, samples);
    }

  winsys = g_slice_new0 (CoglOnscreenEGL);
  cogl_onscreen_set_winsys (onscreen, winsys);

  if (egl_renderer->platform_vtable->onscreen_init &&
      !egl_renderer->platform_vtable->onscreen_init (onscreen,
                                                     egl_config,
                                                     error))
    {
      g_slice_free (CoglOnscreenEGL, winsys);
      return FALSE;
    }

  return TRUE;
}

void
_cogl_winsys_onscreen_egl_deinit (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplayEGL *egl_display = context->display->winsys;
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglOnscreenEGL *egl_onscreen = cogl_onscreen_get_winsys (onscreen);

  /* If we never successfully allocated then there's nothing to do */
  if (egl_onscreen == NULL)
    return;

  if (egl_onscreen->egl_surface != EGL_NO_SURFACE)
    {
      /* Cogl always needs a valid context bound to something so if we
       * are destroying the onscreen that is currently bound we'll
       * switch back to the dummy drawable. */
      if ((egl_display->dummy_surface != EGL_NO_SURFACE ||
           (egl_renderer->private_features &
            COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT) != 0) &&
          (egl_display->current_draw_surface == egl_onscreen->egl_surface ||
           egl_display->current_read_surface == egl_onscreen->egl_surface))
        {
          _cogl_winsys_egl_make_current (context->display,
                                         egl_display->dummy_surface,
                                         egl_display->dummy_surface,
                                         egl_display->current_context);
        }

      if (eglDestroySurface (egl_renderer->edpy, egl_onscreen->egl_surface)
          == EGL_FALSE)
        g_warning ("Failed to destroy EGL surface");
      egl_onscreen->egl_surface = EGL_NO_SURFACE;
    }

  if (egl_renderer->platform_vtable->onscreen_deinit)
    egl_renderer->platform_vtable->onscreen_deinit (onscreen);

  g_slice_free (CoglOnscreenEGL, cogl_onscreen_get_winsys (onscreen));
  cogl_onscreen_set_winsys (onscreen, NULL);
}

static gboolean
bind_onscreen_with_context (CoglOnscreen *onscreen,
                            EGLContext egl_context)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglOnscreenEGL *egl_onscreen = cogl_onscreen_get_winsys (onscreen);

  gboolean status = _cogl_winsys_egl_make_current (context->display,
                                                   egl_onscreen->egl_surface,
                                                   egl_onscreen->egl_surface,
                                                   egl_context);
  if (status)
    {
      CoglRenderer *renderer = context->display->renderer;
      CoglRendererEGL *egl_renderer = renderer->winsys;

      eglSwapInterval (egl_renderer->edpy, 1);
    }

  return status;
}

static gboolean
bind_onscreen (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplayEGL *egl_display = context->display->winsys;

  return bind_onscreen_with_context (onscreen, egl_display->egl_context);
}

void
_cogl_winsys_onscreen_egl_bind (CoglOnscreen *onscreen)
{
  bind_onscreen (onscreen);
}

#ifndef EGL_BUFFER_AGE_EXT
#define EGL_BUFFER_AGE_EXT 0x313D
#endif

int
_cogl_winsys_onscreen_egl_get_buffer_age (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglOnscreenEGL *egl_onscreen = cogl_onscreen_get_winsys (onscreen);
  CoglDisplayEGL *egl_display = context->display->winsys;
  EGLSurface surface = egl_onscreen->egl_surface;
  static gboolean warned = FALSE;
  int age = 0;

  if (!(egl_renderer->private_features & COGL_EGL_WINSYS_FEATURE_BUFFER_AGE))
    return 0;

  if (!_cogl_winsys_egl_make_current (context->display,
				      surface, surface,
                                      egl_display->egl_context))
    return 0;

  if (!eglQuerySurface (egl_renderer->edpy, surface, EGL_BUFFER_AGE_EXT, &age))
    {
      if (!warned)
        g_critical ("Failed to query buffer age, got error %x", eglGetError ());
      warned = TRUE;
    }
  else
    {
      warned = FALSE;
    }

  return age;
}

void
_cogl_winsys_onscreen_egl_swap_region (CoglOnscreen  *onscreen,
                                       const int     *user_rectangles,
                                       int            n_rectangles,
                                       CoglFrameInfo *info,
                                       gpointer       user_data)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglOnscreenEGL *egl_onscreen = cogl_onscreen_get_winsys (onscreen);
  int framebuffer_height  = cogl_framebuffer_get_height (framebuffer);
  int *rectangles = g_alloca (sizeof (int) * n_rectangles * 4);
  int i;

  /* eglSwapBuffersRegion expects rectangles relative to the
   * bottom left corner but we are given rectangles relative to
   * the top left so we need to flip them... */
  memcpy (rectangles, user_rectangles, sizeof (int) * n_rectangles * 4);
  for (i = 0; i < n_rectangles; i++)
    {
      int *rect = &rectangles[4 * i];
      rect[1] = framebuffer_height - rect[1] - rect[3];
    }

  /* At least for eglSwapBuffers the EGL spec says that the surface to
     swap must be bound to the current context. It looks like Mesa
     also validates that this is the case for eglSwapBuffersRegion so
     we must bind here too */
  _cogl_framebuffer_flush_state (COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER_STATE_BIND);

  if (egl_renderer->pf_eglSwapBuffersRegion (egl_renderer->edpy,
                                             egl_onscreen->egl_surface,
                                             n_rectangles,
                                             rectangles) == EGL_FALSE)
    g_warning ("Error reported by eglSwapBuffersRegion");
}

void
_cogl_winsys_onscreen_egl_swap_buffers_with_damage (CoglOnscreen  *onscreen,
                                                    const int     *rectangles,
                                                    int            n_rectangles,
                                                    CoglFrameInfo *info,
                                                    gpointer       user_data)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglOnscreenEGL *egl_onscreen = cogl_onscreen_get_winsys (onscreen);

  COGL_TRACE_BEGIN_SCOPED (CoglOnscreenEGLSwapBuffersWithDamage,
                           "Onscreen (eglSwapBuffers)");

  /* The specification for EGL (at least in 1.4) says that the surface
     needs to be bound to the current context for the swap to work
     although it may change in future. Mesa explicitly checks for this
     and just returns an error if this is not the case so we can't
     just pretend this isn't in the spec. */
  _cogl_framebuffer_flush_state (COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER (onscreen),
                                 COGL_FRAMEBUFFER_STATE_BIND);

  if (n_rectangles && egl_renderer->pf_eglSwapBuffersWithDamage)
    {
      CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
      size_t size = n_rectangles * sizeof (int) * 4;
      int *flipped = alloca (size);
      int i;

      memcpy (flipped, rectangles, size);
      for (i = 0; i < n_rectangles; i++)
        {
          const int *rect = rectangles + 4 * i;
          int *flip_rect = flipped + 4 * i;

          flip_rect[1] =
            cogl_framebuffer_get_height (framebuffer) - rect[1] - rect[3];
        }

      if (egl_renderer->pf_eglSwapBuffersWithDamage (egl_renderer->edpy,
                                                     egl_onscreen->egl_surface,
                                                     flipped,
                                                     n_rectangles) == EGL_FALSE)
        g_warning ("Error reported by eglSwapBuffersWithDamage");
    }
  else
    eglSwapBuffers (egl_renderer->edpy, egl_onscreen->egl_surface);
}

void
cogl_onscreen_egl_set_platform (CoglOnscreenEGL *onscreen_egl,
                                gpointer         platform)
{
  onscreen_egl->platform = platform;
}

gpointer
cogl_onscreen_egl_get_platform (CoglOnscreenEGL *onscreen_egl)
{
  return onscreen_egl->platform;
}

void
cogl_onscreen_egl_set_egl_surface (CoglOnscreenEGL *onscreen_egl,
                                   EGLSurface       egl_surface)
{
  onscreen_egl->egl_surface = egl_surface;
}

EGLSurface
cogl_onscreen_egl_get_egl_surface (CoglOnscreenEGL *onscreen_egl)
{
  return onscreen_egl->egl_surface;
}
