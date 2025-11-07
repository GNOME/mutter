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

#include "config.h"

#include "cogl/winsys/cogl-onscreen-egl.h"

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-display-egl-private.h"
#include "cogl/cogl-frame-info-private.h"
#include "cogl/cogl-renderer-egl-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-trace.h"
#include "cogl/winsys/cogl-winsys-egl.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>

typedef struct _CoglOnscreenEglPrivate
{
  EGLSurface egl_surface;

  /* Can use PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC (or the EXT variant) */
  EGLBoolean (*pf_eglSwapBuffersWithDamage) (EGLDisplay, EGLSurface, const EGLint *, EGLint);
} CoglOnscreenEglPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CoglOnscreenEgl, cogl_onscreen_egl,
                            COGL_TYPE_ONSCREEN)

static void
cogl_onscreen_egl_dispose (GObject *object)
{
  CoglOnscreenEgl *onscreen_egl = COGL_ONSCREEN_EGL (object);
  CoglOnscreenEglPrivate *priv =
    cogl_onscreen_egl_get_instance_private (onscreen_egl);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (object);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplayEGL *display_egl = COGL_DISPLAY_EGL (context->display);
  CoglRenderer *renderer = cogl_context_get_renderer (context);
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);
  EGLSurface dummy_surface = cogl_display_egl_get_dummy_surface (display_egl);

  if (priv->egl_surface != EGL_NO_SURFACE)
    {
      /* Cogl always needs a valid context bound to something so if we
       * are destroying the onscreen that is currently bound we'll
       * switch back to the dummy drawable. */
      if ((dummy_surface != EGL_NO_SURFACE ||
           cogl_renderer_egl_has_feature (renderer_egl,
                                           COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT)) &&
          (cogl_display_egl_get_current_draw_surface (display_egl) == priv->egl_surface ||
           cogl_display_egl_get_current_read_surface (display_egl) == priv->egl_surface))
        {
          cogl_display_egl_make_current (display_egl,
                                         dummy_surface,
                                         dummy_surface,
                                         cogl_display_egl_get_current_context (display_egl));
        }

      if (eglDestroySurface (edpy, priv->egl_surface)
          == EGL_FALSE)
        g_warning ("Failed to destroy EGL surface");
      priv->egl_surface = EGL_NO_SURFACE;
    }

  G_OBJECT_CLASS (cogl_onscreen_egl_parent_class)->dispose (object);
}

static void
bind_onscreen_with_context (CoglOnscreen *onscreen,
                            EGLContext egl_context)
{
  CoglOnscreenEgl *onscreen_egl = COGL_ONSCREEN_EGL (onscreen);
  CoglOnscreenEglPrivate *priv =
    cogl_onscreen_egl_get_instance_private (onscreen_egl);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);

  gboolean status = cogl_display_egl_make_current (COGL_DISPLAY_EGL (context->display),
                                                   priv->egl_surface,
                                                   priv->egl_surface,
                                                   egl_context);
  if (status)
    {
      CoglRenderer *renderer = cogl_context_get_renderer (context);
      CoglRendererEglPrivate *priv_renderer =
        cogl_renderer_egl_get_private (COGL_RENDERER_EGL (renderer));
      EGLDisplay edpy =
        cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (renderer));

      if (priv_renderer->pf_eglSwapBuffersWithDamageKHR)
        {
          priv->pf_eglSwapBuffersWithDamage =
            priv_renderer->pf_eglSwapBuffersWithDamageKHR;
        }
      else
        {
          priv->pf_eglSwapBuffersWithDamage =
            priv_renderer->pf_eglSwapBuffersWithDamageEXT;
        }

      eglSwapInterval (edpy, 1);
    }
}

static void
cogl_onscreen_egl_bind (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplayEGL *display_egl = COGL_DISPLAY_EGL (context->display);

  bind_onscreen_with_context (onscreen,
                              cogl_display_egl_get_egl_context (display_egl));
}

#ifndef EGL_BUFFER_AGE_EXT
#define EGL_BUFFER_AGE_EXT 0x313D
#endif

static int
cogl_onscreen_egl_get_buffer_age (CoglOnscreen *onscreen)
{
  CoglOnscreenEgl *onscreen_egl = COGL_ONSCREEN_EGL (onscreen);
  CoglOnscreenEglPrivate *priv =
    cogl_onscreen_egl_get_instance_private (onscreen_egl);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = cogl_context_get_renderer (context);
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);
  CoglDisplayEGL *display_egl = COGL_DISPLAY_EGL (context->display);
  EGLSurface surface = priv->egl_surface;
  static gboolean warned = FALSE;
  int age = 0;

  if (!cogl_renderer_egl_has_feature (renderer_egl, COGL_EGL_WINSYS_FEATURE_BUFFER_AGE))
    return 0;

  if (!cogl_display_egl_make_current (display_egl,
				      surface, surface,
                                      cogl_display_egl_get_egl_context (display_egl)))
    return 0;

  if (!eglQuerySurface (edpy, surface, EGL_BUFFER_AGE_EXT, &age))
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

static void
cogl_onscreen_egl_swap_region (CoglOnscreen    *onscreen,
                               const MtkRegion *region,
                               CoglFrameInfo   *info,
                               gpointer         user_data)
{
  CoglOnscreenEgl *onscreen_egl = COGL_ONSCREEN_EGL (onscreen);
  CoglOnscreenEglPrivate *priv =
    cogl_onscreen_egl_get_instance_private (onscreen_egl);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = cogl_context_get_renderer (context);
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEglPrivate *priv_renderer =
    cogl_renderer_egl_get_private (renderer_egl);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);
  int n_rectangles;
  int *egl_rectangles;

  n_rectangles = mtk_region_num_rectangles (region);
  egl_rectangles = g_alloca (n_rectangles * sizeof (int) * 4);
  cogl_region_to_flipped_array (region,
                                cogl_framebuffer_get_height (framebuffer),
                                egl_rectangles);

  /* At least for eglSwapBuffers the EGL spec says that the surface to
     swap must be bound to the current context. It looks like Mesa
     also validates that this is the case for eglSwapBuffersRegion so
     we must bind here too */
  cogl_context_flush_framebuffer_state (context,
                                        COGL_FRAMEBUFFER (onscreen),
                                        COGL_FRAMEBUFFER (onscreen),
                                        COGL_FRAMEBUFFER_STATE_BIND);

  if (priv_renderer->pf_eglSwapBuffersRegion (edpy,
                                              priv->egl_surface,
                                              n_rectangles,
                                              egl_rectangles) == EGL_FALSE)
    g_warning ("Error reported by eglSwapBuffersRegion");

  /* Update latest sync object after buffer swap */
  cogl_framebuffer_flush (framebuffer);
}

static void
cogl_onscreen_egl_queue_damage_region (CoglOnscreen    *onscreen,
                                       const MtkRegion *region)
{
  CoglOnscreenEgl *onscreen_egl = COGL_ONSCREEN_EGL (onscreen);
  CoglOnscreenEglPrivate *priv =
    cogl_onscreen_egl_get_instance_private (onscreen_egl);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = cogl_context_get_renderer (context);
  CoglRendererEgl *renderer_egl = COGL_RENDERER_EGL (renderer);
  CoglRendererEglPrivate *priv_renderer =
    cogl_renderer_egl_get_private (renderer_egl);
  EGLDisplay edpy = cogl_renderer_egl_get_edisplay (renderer_egl);
  int n_rectangles;
  int *egl_rectangles;

  if (!priv_renderer->pf_eglSetDamageRegion)
    return;

  g_return_if_fail (region);

  n_rectangles = mtk_region_num_rectangles (region);
  g_return_if_fail (n_rectangles > 0);

  egl_rectangles = g_alloca (n_rectangles * sizeof (int) * 4);
  cogl_region_to_flipped_array (region,
                                cogl_framebuffer_get_height (framebuffer),
                                egl_rectangles);

  if (priv_renderer->pf_eglSetDamageRegion (edpy,
                                            priv->egl_surface,
                                            egl_rectangles,
                                            n_rectangles) == EGL_FALSE)
    g_warning ("Error reported by eglSetDamageRegion");
}

static void
cogl_onscreen_egl_swap_buffers_with_damage (CoglOnscreen    *onscreen,
                                            const MtkRegion *region,
                                            CoglFrameInfo   *info,
                                            gpointer         user_data)
{
  CoglOnscreenEgl *onscreen_egl = COGL_ONSCREEN_EGL (onscreen);
  CoglOnscreenEglPrivate *priv =
    cogl_onscreen_egl_get_instance_private (onscreen_egl);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = cogl_context_get_renderer (context);
  EGLDisplay egl_display =
    cogl_renderer_egl_get_edisplay (COGL_RENDERER_EGL (renderer));

  COGL_TRACE_BEGIN_SCOPED (CoglOnscreenEGLSwapBuffersWithDamage,
                           "Cogl::Onscreen::egl_swap_buffers_with_damage()");

  /* The specification for EGL (at least in 1.4) says that the surface
     needs to be bound to the current context for the swap to work
     although it may change in future. Mesa explicitly checks for this
     and just returns an error if this is not the case so we can't
     just pretend this isn't in the spec. */
  cogl_context_flush_framebuffer_state (context,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);

  if (region && priv->pf_eglSwapBuffersWithDamage)
    {
      int n_rectangles;
      int *egl_rectangles;

      n_rectangles = mtk_region_num_rectangles (region);
      egl_rectangles = alloca (n_rectangles * sizeof (int) * 4);
      cogl_region_to_flipped_array (region,
                                    cogl_framebuffer_get_height (framebuffer),
                                    egl_rectangles);

      if (priv->pf_eglSwapBuffersWithDamage (egl_display,
                                             priv->egl_surface,
                                             egl_rectangles,
                                             n_rectangles) == EGL_FALSE)
        g_warning ("Error reported by eglSwapBuffersWithDamage");
    }
  else
    eglSwapBuffers (egl_display, priv->egl_surface);

  /* Update latest sync object after buffer swap */
  cogl_framebuffer_flush (framebuffer);
}

void
cogl_onscreen_egl_set_egl_surface (CoglOnscreenEgl *onscreen_egl,
                                   EGLSurface       egl_surface)
{
  CoglOnscreenEglPrivate *priv =
    cogl_onscreen_egl_get_instance_private (onscreen_egl);

  priv->egl_surface = egl_surface;
}

EGLSurface
cogl_onscreen_egl_get_egl_surface (CoglOnscreenEgl *onscreen_egl)
{
  CoglOnscreenEglPrivate *priv =
    cogl_onscreen_egl_get_instance_private (onscreen_egl);

  return priv->egl_surface;
}

static void
cogl_onscreen_egl_init (CoglOnscreenEgl *onscreen_egl)
{
}

static void
cogl_onscreen_egl_class_init (CoglOnscreenEglClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CoglOnscreenClass *onscreen_class = COGL_ONSCREEN_CLASS (klass);

  object_class->dispose = cogl_onscreen_egl_dispose;

  onscreen_class->bind = cogl_onscreen_egl_bind;
  onscreen_class->queue_damage_region =
    cogl_onscreen_egl_queue_damage_region;
  onscreen_class->swap_buffers_with_damage =
    cogl_onscreen_egl_swap_buffers_with_damage;
  onscreen_class->swap_region = cogl_onscreen_egl_swap_region;
  onscreen_class->get_buffer_age = cogl_onscreen_egl_get_buffer_age;
}
