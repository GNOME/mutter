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
#include "cogl/cogl-frame-info-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-trace.h"
#include "cogl/winsys/cogl-winsys-egl-private.h"

typedef struct _CoglOnscreenEglPrivate
{
  EGLSurface egl_surface;

  /* Can use PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC (or the EXT variant) */
  EGLBoolean (*pf_eglSwapBuffersWithDamage) (EGLDisplay, EGLSurface, const EGLint *, EGLint);
} CoglOnscreenEglPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CoglOnscreenEgl, cogl_onscreen_egl,
                            COGL_TYPE_ONSCREEN)

gboolean
cogl_onscreen_egl_choose_config (CoglOnscreenEgl  *onscreen_egl,
                                 EGLConfig        *out_egl_config,
                                 GError          **error)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen_egl);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *display = context->display;
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  EGLint attributes[MAX_EGL_CONFIG_ATTRIBS];
  EGLConfig egl_config;
  EGLint config_count = 0;
  EGLBoolean status;

  cogl_display_egl_determine_attributes (display, attributes);

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

  *out_egl_config = egl_config;
  return TRUE;
}

static void
cogl_onscreen_egl_dispose (GObject *object)
{
  CoglOnscreenEgl *onscreen_egl = COGL_ONSCREEN_EGL (object);
  CoglOnscreenEglPrivate *priv =
    cogl_onscreen_egl_get_instance_private (onscreen_egl);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (object);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplayEGL *egl_display = context->display->winsys;
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;

  if (priv->egl_surface != EGL_NO_SURFACE)
    {
      /* Cogl always needs a valid context bound to something so if we
       * are destroying the onscreen that is currently bound we'll
       * switch back to the dummy drawable. */
      if ((egl_display->dummy_surface != EGL_NO_SURFACE ||
           (egl_renderer->private_features &
            COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT) != 0) &&
          (egl_display->current_draw_surface == priv->egl_surface ||
           egl_display->current_read_surface == priv->egl_surface))
        {
          _cogl_winsys_egl_make_current (context->display,
                                         egl_display->dummy_surface,
                                         egl_display->dummy_surface,
                                         egl_display->current_context);
        }

      if (eglDestroySurface (egl_renderer->edpy, priv->egl_surface)
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

  gboolean status = _cogl_winsys_egl_make_current (context->display,
                                                   priv->egl_surface,
                                                   priv->egl_surface,
                                                   egl_context);
  if (status)
    {
      CoglRenderer *renderer = context->display->renderer;
      CoglRendererEGL *egl_renderer = renderer->winsys;

      if (egl_renderer->pf_eglSwapBuffersWithDamageKHR)
        {
          priv->pf_eglSwapBuffersWithDamage =
            egl_renderer->pf_eglSwapBuffersWithDamageKHR;
        }
      else
        {
          priv->pf_eglSwapBuffersWithDamage =
            egl_renderer->pf_eglSwapBuffersWithDamageEXT;
        }

      eglSwapInterval (egl_renderer->edpy, 1);
    }
}

static void
cogl_onscreen_egl_bind (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplayEGL *egl_display = context->display->winsys;

  bind_onscreen_with_context (onscreen, egl_display->egl_context);
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
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglDisplayEGL *egl_display = context->display->winsys;
  EGLSurface surface = priv->egl_surface;
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
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
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

  if (egl_renderer->pf_eglSwapBuffersRegion (egl_renderer->edpy,
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
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  int n_rectangles;
  int *egl_rectangles;

  if (!egl_renderer->pf_eglSetDamageRegion)
    return;

  g_return_if_fail (region);

  n_rectangles = mtk_region_num_rectangles (region);
  g_return_if_fail (n_rectangles > 0);

  egl_rectangles = g_alloca (n_rectangles * sizeof (int) * 4);
  cogl_region_to_flipped_array (region,
                                cogl_framebuffer_get_height (framebuffer),
                                egl_rectangles);

  if (egl_renderer->pf_eglSetDamageRegion (egl_renderer->edpy,
                                           priv->egl_surface,
                                           egl_rectangles,
                                           n_rectangles) == EGL_FALSE)
    g_warning ("Error reported by eglSetDamageRegion");
}

void
cogl_onscreen_egl_maybe_create_timestamp_query (CoglOnscreen  *onscreen,
                                                CoglFrameInfo *info)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);

  if (!cogl_context_has_feature (context, COGL_FEATURE_ID_TIMESTAMP_QUERY))
    return;

  info->gpu_time_before_buffer_swap_ns =
    cogl_context_get_gpu_time_ns (context);
  info->cpu_time_before_buffer_swap_us = g_get_monotonic_time ();

  /* Set up a timestamp query for when all rendering will be finished. */
  info->timestamp_query =
    cogl_framebuffer_create_timestamp_query (framebuffer);

  info->has_valid_gpu_rendering_duration = TRUE;
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
  CoglRenderer *renderer = context->display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;

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

      if (priv->pf_eglSwapBuffersWithDamage (egl_renderer->edpy,
                                             priv->egl_surface,
                                             egl_rectangles,
                                             n_rectangles) == EGL_FALSE)
        g_warning ("Error reported by eglSwapBuffersWithDamage");
    }
  else
    eglSwapBuffers (egl_renderer->edpy, priv->egl_surface);

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
