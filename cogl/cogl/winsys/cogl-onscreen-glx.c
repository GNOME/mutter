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

#include "cogl/winsys/cogl-onscreen-glx.h"

#include <GL/glx.h>
#include <sys/time.h>

#include "cogl/cogl-context-private.h"
#include "cogl/cogl-frame-info-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-x11-onscreen.h"
#include "cogl/cogl-xlib-renderer-private.h"
#include "cogl/winsys/cogl-glx-display-private.h"
#include "cogl/winsys/cogl-glx-renderer-private.h"
#include "cogl/winsys/cogl-winsys-glx-private.h"
#include "mtk/mtk-x11.h"

struct _CoglOnscreenGlx
{
  CoglOnscreen parent;

  Window xwin;
  int x, y;
  CoglOutput *output;

  GLXDrawable glxwin;
  uint32_t last_swap_vsync_counter;
  uint32_t pending_sync_notify;
  uint32_t pending_complete_notify;
};

static void
x11_onscreen_init_iface (CoglX11OnscreenInterface *iface);

G_DEFINE_TYPE_WITH_CODE (CoglOnscreenGlx, cogl_onscreen_glx,
                         COGL_TYPE_ONSCREEN,
                         G_IMPLEMENT_INTERFACE (COGL_TYPE_X11_ONSCREEN,
                                                x11_onscreen_init_iface))

#define COGL_ONSCREEN_X11_EVENT_MASK (StructureNotifyMask | ExposureMask)

static gboolean
cogl_onscreen_glx_allocate (CoglFramebuffer  *framebuffer,
                            GError          **error)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (framebuffer);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *display = context->display;
  CoglGLXDisplay *glx_display = display->winsys;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (display->renderer);
  CoglGLXRenderer *glx_renderer = display->renderer->winsys;
  Window xwin;
  const CoglFramebufferConfig *config;
  GLXFBConfig fbconfig;
  GError *fbconfig_error = NULL;

  g_return_val_if_fail (glx_display->glx_context, FALSE);

  config = cogl_framebuffer_get_config (framebuffer);
  if (!cogl_display_glx_find_fbconfig (display, config,
                                       &fbconfig,
                                       &fbconfig_error))
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_CONTEXT,
                   "Unable to find suitable fbconfig for the GLX context: %s",
                   fbconfig_error->message);
      g_error_free (fbconfig_error);
      return FALSE;
    }

  /* Update the real number of samples_per_pixel now that we have
   * found an fbconfig... */
  if (config->samples_per_pixel)
    {
      int samples;
      int status = glx_renderer->glXGetFBConfigAttrib (xlib_renderer->xdpy,
                                                       fbconfig,
                                                       GLX_SAMPLES,
                                                       &samples);
      g_return_val_if_fail (status == Success, TRUE);
      cogl_framebuffer_update_samples_per_pixel (framebuffer, samples);
    }

  /* FIXME: We need to explicitly Select for ConfigureNotify events.
   * We need to document that for windows we create then toolkits
   * must be careful not to clear event mask bits that we select.
   */
    {
      int width;
      int height;
      XVisualInfo *xvisinfo;
      XSetWindowAttributes xattr;
      unsigned long mask;
      int xerror;

      width = cogl_framebuffer_get_width (framebuffer);
      height = cogl_framebuffer_get_height (framebuffer);

      mtk_x11_error_trap_push (xlib_renderer->xdpy);

      xvisinfo = glx_renderer->glXGetVisualFromFBConfig (xlib_renderer->xdpy,
                                                         fbconfig);
      if (xvisinfo == NULL)
        {
          g_set_error_literal (error, COGL_WINSYS_ERROR,
                               COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                               "Unable to retrieve the X11 visual of context's "
                               "fbconfig");
          mtk_x11_error_trap_pop (xlib_renderer->xdpy);
          return FALSE;
        }

      /* window attributes */
      xattr.background_pixel = WhitePixel (xlib_renderer->xdpy,
                                           DefaultScreen (xlib_renderer->xdpy));
      xattr.border_pixel = 0;
      /* XXX: is this an X resource that we are leaking‽... */
      xattr.colormap = XCreateColormap (xlib_renderer->xdpy,
                                        DefaultRootWindow (xlib_renderer->xdpy),
                                        xvisinfo->visual,
                                        AllocNone);
      xattr.event_mask = COGL_ONSCREEN_X11_EVENT_MASK;

      mask = CWBorderPixel | CWColormap | CWEventMask;

      xwin = XCreateWindow (xlib_renderer->xdpy,
                            DefaultRootWindow (xlib_renderer->xdpy),
                            0, 0,
                            width, height,
                            0,
                            xvisinfo->depth,
                            InputOutput,
                            xvisinfo->visual,
                            mask, &xattr);

      XFree (xvisinfo);

      XSync (xlib_renderer->xdpy, False);
      xerror = mtk_x11_error_trap_pop_with_return (xlib_renderer->xdpy);
      if (xerror)
        {
          char message[1000];
          XGetErrorText (xlib_renderer->xdpy, xerror,
                         message, sizeof (message));
          g_set_error (error, COGL_WINSYS_ERROR,
                       COGL_WINSYS_ERROR_CREATE_ONSCREEN,
                       "X error while creating Window for CoglOnscreen: %s",
                       message);
          return FALSE;
        }
    }

  onscreen_glx->xwin = xwin;

  /* Try and create a GLXWindow to use with extensions dependent on
   * GLX versions >= 1.3 that don't accept regular X Windows as GLX
   * drawables. */
  if (glx_renderer->glx_major == 1 && glx_renderer->glx_minor >= 3)
    {
      onscreen_glx->glxwin =
        glx_renderer->glXCreateWindow (xlib_renderer->xdpy,
                                       fbconfig,
                                       onscreen_glx->xwin,
                                       NULL);
    }

#ifdef GLX_INTEL_swap_event
  if (_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT))
    {
      GLXDrawable drawable =
        onscreen_glx->glxwin ? onscreen_glx->glxwin : onscreen_glx->xwin;

      /* similarly to above, we unconditionally select this event
       * because we rely on it to advance the master clock, and
       * drive redraw/relayout, animations and event handling.
       */
      glx_renderer->glXSelectEvent (xlib_renderer->xdpy,
                                    drawable,
                                    GLX_BUFFER_SWAP_COMPLETE_INTEL_MASK);
    }
#endif /* GLX_INTEL_swap_event */

  return TRUE;
}

static void
cogl_onscreen_glx_dispose (GObject *object)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (object);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (object);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglGLXDisplay *glx_display = context->display->winsys;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (context->display->renderer);
  CoglGLXRenderer *glx_renderer = context->display->renderer->winsys;
  GLXDrawable drawable;

  G_OBJECT_CLASS (cogl_onscreen_glx_parent_class)->dispose (object);

  g_clear_object (&onscreen_glx->output);

  if (onscreen_glx->glxwin != None ||
      onscreen_glx->xwin != None)
    {
      mtk_x11_error_trap_push (xlib_renderer->xdpy);

      drawable =
        onscreen_glx->glxwin == None ? onscreen_glx->xwin : onscreen_glx->glxwin;

      /* Cogl always needs a valid context bound to something so if we are
       * destroying the onscreen that is currently bound we'll switch back
       * to the dummy drawable. Although the documentation for
       * glXDestroyWindow states that a currently bound window won't
       * actually be destroyed until it is unbound, it looks like this
       * doesn't work if the X window itself is destroyed */
      if (drawable == cogl_context_glx_get_current_drawable (context))
        {
          GLXDrawable dummy_drawable = (glx_display->dummy_glxwin == None ?
                                        glx_display->dummy_xwin :
                                        glx_display->dummy_glxwin);

          glx_renderer->glXMakeContextCurrent (xlib_renderer->xdpy,
                                               dummy_drawable,
                                               dummy_drawable,
                                               glx_display->glx_context);
          cogl_context_glx_set_current_drawable (context, dummy_drawable);
        }

      if (onscreen_glx->glxwin != None)
        {
          glx_renderer->glXDestroyWindow (xlib_renderer->xdpy,
                                          onscreen_glx->glxwin);
          onscreen_glx->glxwin = None;
        }

      if (onscreen_glx->xwin != None)
        {
          XDestroyWindow (xlib_renderer->xdpy, onscreen_glx->xwin);
          onscreen_glx->xwin = None;
        }
      else
        {
          onscreen_glx->xwin = None;
        }

      XSync (xlib_renderer->xdpy, False);

      mtk_x11_error_trap_pop (xlib_renderer->xdpy);
    }
}

static void
cogl_onscreen_glx_bind (CoglOnscreen *onscreen)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglGLXDisplay *glx_display = context->display->winsys;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (context->display->renderer);
  CoglGLXRenderer *glx_renderer = context->display->renderer->winsys;
  GLXDrawable drawable;

  drawable =
    onscreen_glx->glxwin ? onscreen_glx->glxwin : onscreen_glx->xwin;

  if (cogl_context_glx_get_current_drawable (context) == drawable)
    return;

  mtk_x11_error_trap_push (xlib_renderer->xdpy);

  COGL_NOTE (WINSYS,
             "MakeContextCurrent dpy: %p, window: 0x%x, context: %p",
             xlib_renderer->xdpy,
             (unsigned int) drawable,
             glx_display->glx_context);

  glx_renderer->glXMakeContextCurrent (xlib_renderer->xdpy,
                                       drawable,
                                       drawable,
                                       glx_display->glx_context);

  /* In case we are using GLX_SGI_swap_control for vblank syncing
   * we need call glXSwapIntervalSGI here to make sure that it
   * affects the current drawable.
   *
   * Note: we explicitly set to 0 when we aren't using the swap
   * interval to synchronize since some drivers have a default
   * swap interval of 1. Sadly some drivers even ignore requests
   * to disable the swap interval.
   *
   * NB: glXSwapIntervalSGI applies to the context not the
   * drawable which is why we can't just do this once when the
   * framebuffer is allocated.
   *
   * FIXME: We should check for GLX_EXT_swap_control which allows
   * per framebuffer swap intervals. GLX_MESA_swap_control also
   * allows per-framebuffer swap intervals but the semantics tend
   * to be more muddled since Mesa drivers tend to expose both the
   * MESA and SGI extensions which should technically be mutually
   * exclusive.
   */
  if (glx_renderer->glXSwapInterval)
    glx_renderer->glXSwapInterval (1);

  XSync (xlib_renderer->xdpy, False);

  /* FIXME: We should be reporting a GError here */
  if (mtk_x11_error_trap_pop_with_return (xlib_renderer->xdpy))
    {
      g_warning ("X Error received while making drawable 0x%08lX current",
                 drawable);
      return;
    }

  cogl_context_glx_set_current_drawable (context, drawable);
}

static void
ensure_ust_type (CoglRenderer *renderer,
                 GLXDrawable   drawable)
{
  CoglGLXRenderer *glx_renderer =  renderer->winsys;
  CoglXlibRenderer *xlib_renderer = _cogl_xlib_renderer_get_data (renderer);
  int64_t ust;
  int64_t msc;
  int64_t sbc;
  struct timeval tv;
  int64_t current_system_time;
  int64_t current_monotonic_time;

  if (glx_renderer->ust_type != COGL_GLX_UST_IS_UNKNOWN)
    return;

  glx_renderer->ust_type = COGL_GLX_UST_IS_OTHER;

  if (glx_renderer->glXGetSyncValues == NULL)
    goto out;

  if (!glx_renderer->glXGetSyncValues (xlib_renderer->xdpy, drawable,
                                       &ust, &msc, &sbc))
    goto out;

  /* This is the time source that existing (buggy) linux drm drivers
   * use */
  gettimeofday (&tv, NULL);
  current_system_time = (tv.tv_sec * G_GINT64_CONSTANT (1000000)) + tv.tv_usec;

  if (current_system_time > ust - 1000000 &&
      current_system_time < ust + 1000000)
    {
      glx_renderer->ust_type = COGL_GLX_UST_IS_GETTIMEOFDAY;
      goto out;
    }

  /* This is the time source that the newer (fixed) linux drm
   * drivers use (Linux >= 3.8) */
  current_monotonic_time = g_get_monotonic_time ();

  if (current_monotonic_time > ust - 1000000 &&
      current_monotonic_time < ust + 1000000)
    {
      glx_renderer->ust_type = COGL_GLX_UST_IS_MONOTONIC_TIME;
      goto out;
    }

 out:
  COGL_NOTE (WINSYS, "Classified OML system time as: %s",
             glx_renderer->ust_type == COGL_GLX_UST_IS_GETTIMEOFDAY ? "gettimeofday" :
             (glx_renderer->ust_type == COGL_GLX_UST_IS_MONOTONIC_TIME ? "monotonic" :
              "other"));
  return;
}

static int64_t
ust_to_microseconds (CoglRenderer *renderer,
                     GLXDrawable   drawable,
                     int64_t       ust)
{
  CoglGLXRenderer *glx_renderer = renderer->winsys;

  ensure_ust_type (renderer, drawable);

  switch (glx_renderer->ust_type)
    {
    case COGL_GLX_UST_IS_UNKNOWN:
      g_assert_not_reached ();
      break;
    case COGL_GLX_UST_IS_GETTIMEOFDAY:
    case COGL_GLX_UST_IS_MONOTONIC_TIME:
      return ust;
    case COGL_GLX_UST_IS_OTHER:
      /* In this case the scale of UST is undefined so we can't easily
       * scale to microseconds.
       *
       * For example the driver may be reporting the rdtsc CPU counter
       * as UST values and so the scale would need to be determined
       * empirically.
       *
       * Potentially we could block for a known duration within
       * ensure_ust_type() to measure the timescale of UST but for now
       * we just ignore unknown time sources */
      return 0;
    }

  return 0;
}

static gboolean
is_ust_monotonic (CoglRenderer *renderer,
                  GLXDrawable   drawable)
{
  CoglGLXRenderer *glx_renderer = renderer->winsys;

  ensure_ust_type (renderer, drawable);

  return (glx_renderer->ust_type == COGL_GLX_UST_IS_MONOTONIC_TIME);
}

static void
_cogl_winsys_wait_for_vblank (CoglOnscreen *onscreen)
{
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *ctx = cogl_framebuffer_get_context (framebuffer);
  CoglGLXRenderer *glx_renderer;
  CoglXlibRenderer *xlib_renderer;
  CoglGLXDisplay *glx_display;

  glx_renderer = ctx->display->renderer->winsys;
  xlib_renderer = _cogl_xlib_renderer_get_data (ctx->display->renderer);
  glx_display = ctx->display->winsys;

  if (glx_display->can_vblank_wait)
    {
      CoglFrameInfo *info = cogl_onscreen_peek_tail_frame_info (onscreen);
      info->flags |= COGL_FRAME_INFO_FLAG_VSYNC;

      if (glx_renderer->glXWaitForMsc)
        {
          CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
          Drawable drawable = onscreen_glx->glxwin;
          int64_t ust;
          int64_t msc;
          int64_t sbc;

          glx_renderer->glXWaitForMsc (xlib_renderer->xdpy, drawable,
                                       0, 1, 0,
                                       &ust, &msc, &sbc);

          if (is_ust_monotonic (ctx->display->renderer, drawable))
            {
              info->presentation_time_us =
                ust_to_microseconds (ctx->display->renderer,
                                     drawable,
                                     ust);
              info->flags |= COGL_FRAME_INFO_FLAG_HW_CLOCK;
            }
          else
            {
              info->presentation_time_us = g_get_monotonic_time ();
            }

          /* Intentionally truncating to lower 32 bits, same as DRM. */
          info->sequence = msc;
        }
      else
        {
          uint32_t current_count;

          glx_renderer->glXGetVideoSync (&current_count);
          glx_renderer->glXWaitVideoSync (2,
                                          (current_count + 1) % 2,
                                          &current_count);

          info->presentation_time_us = g_get_monotonic_time ();
        }
    }
}

static uint32_t
_cogl_winsys_get_vsync_counter (CoglContext *ctx)
{
  uint32_t video_sync_count;
  CoglGLXRenderer *glx_renderer;

  glx_renderer = ctx->display->renderer->winsys;

  glx_renderer->glXGetVideoSync (&video_sync_count);

  return video_sync_count;
}

#ifndef GLX_BACK_BUFFER_AGE_EXT
#define GLX_BACK_BUFFER_AGE_EXT 0x20F4
#endif

static int
cogl_onscreen_glx_get_buffer_age (CoglOnscreen *onscreen)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglXlibRenderer *xlib_renderer = _cogl_xlib_renderer_get_data (context->display->renderer);
  CoglGLXRenderer *glx_renderer = context->display->renderer->winsys;
  GLXDrawable drawable;
  unsigned int age = 0;

  if (!_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_BUFFER_AGE))
    return 0;

  cogl_onscreen_bind (onscreen);

  drawable = onscreen_glx->glxwin ? onscreen_glx->glxwin : onscreen_glx->xwin;
  mtk_x11_error_trap_push (xlib_renderer->xdpy);
  glx_renderer->glXQueryDrawable (xlib_renderer->xdpy, drawable, GLX_BACK_BUFFER_AGE_EXT, &age);
  mtk_x11_error_trap_pop (xlib_renderer->xdpy);

  return age;
}

static void
set_frame_info_output (CoglOnscreen *onscreen,
                       CoglOutput   *output)
{
  CoglFrameInfo *info = cogl_onscreen_peek_tail_frame_info (onscreen);

  if (output)
    {
      float refresh_rate = cogl_output_get_refresh_rate (output);
      if (refresh_rate != 0.0)
        info->refresh_rate = refresh_rate;
    }
}

static void
cogl_onscreen_glx_flush_notification (CoglOnscreen *onscreen)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);

  while (onscreen_glx->pending_sync_notify > 0 ||
         onscreen_glx->pending_complete_notify > 0)
    {
      if (onscreen_glx->pending_sync_notify > 0)
        {
          CoglFrameInfo *info;

          info = cogl_onscreen_peek_head_frame_info (onscreen);
          _cogl_onscreen_notify_frame_sync (onscreen, info);
          onscreen_glx->pending_sync_notify--;
        }

      if (onscreen_glx->pending_complete_notify > 0)
        {
          CoglFrameInfo *info;

          info = cogl_onscreen_pop_head_frame_info (onscreen);
          _cogl_onscreen_notify_complete (onscreen, info);
          g_object_unref (info);
          onscreen_glx->pending_complete_notify--;
        }
    }
}

static void
flush_pending_notifications_cb (void *data,
                                void *user_data)
{
  CoglFramebuffer *framebuffer = data;

  if (COGL_IS_ONSCREEN (framebuffer))
    {
      CoglOnscreen *onscreen = COGL_ONSCREEN (framebuffer);

      cogl_onscreen_glx_flush_notification (onscreen);
    }
}

static void
flush_pending_notifications_idle (void *user_data)
{
  CoglContext *context = user_data;
  CoglRenderer *renderer = context->display->renderer;
  CoglGLXRenderer *glx_renderer = renderer->winsys;

  /* This needs to be disconnected before invoking the callbacks in
   * case the callbacks cause it to be queued again */
  _cogl_closure_disconnect (glx_renderer->flush_notifications_idle);
  glx_renderer->flush_notifications_idle = NULL;

  g_list_foreach (context->framebuffers,
                  flush_pending_notifications_cb,
                  NULL);
}

static void
set_sync_pending (CoglOnscreen *onscreen)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = context->display->renderer;
  CoglGLXRenderer *glx_renderer = renderer->winsys;

  /* We only want to dispatch sync events when the application calls
   * cogl_context_dispatch so instead of immediately notifying we
   * queue an idle callback */
  if (!glx_renderer->flush_notifications_idle)
    {
      glx_renderer->flush_notifications_idle =
        _cogl_poll_renderer_add_idle (renderer,
                                      flush_pending_notifications_idle,
                                      context,
                                      NULL);
    }

  onscreen_glx->pending_sync_notify++;
}

static void
set_complete_pending (CoglOnscreen *onscreen)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = context->display->renderer;
  CoglGLXRenderer *glx_renderer = renderer->winsys;

  /* We only want to notify swap completion when the application calls
   * cogl_context_dispatch so instead of immediately notifying we
   * queue an idle callback */
  if (!glx_renderer->flush_notifications_idle)
    {
      glx_renderer->flush_notifications_idle =
        _cogl_poll_renderer_add_idle (renderer,
                                      flush_pending_notifications_idle,
                                      context,
                                      NULL);
    }

  onscreen_glx->pending_complete_notify++;
}

static void
cogl_onscreen_glx_swap_region (CoglOnscreen  *onscreen,
                               const int     *user_rectangles,
                               int            n_rectangles,
                               CoglFrameInfo *info,
                               gpointer       user_data)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (context->display->renderer);
  CoglGLXRenderer *glx_renderer = context->display->renderer->winsys;
  CoglGLXDisplay *glx_display = context->display->winsys;
  uint32_t end_frame_vsync_counter = 0;
  gboolean have_counter;
  gboolean can_wait;
  int x_min = 0, x_max = 0, y_min = 0, y_max = 0;

  /*
   * We assume that glXCopySubBuffer is synchronized which means it won't prevent multiple
   * blits per retrace if they can all be performed in the blanking period. If that's the
   * case then we still want to use the vblank sync menchanism but
   * we only need it to throttle redraws.
   */
  gboolean blit_sub_buffer_is_synchronized =
    _cogl_winsys_has_feature (COGL_WINSYS_FEATURE_SWAP_REGION_SYNCHRONIZED);

  int framebuffer_width = cogl_framebuffer_get_width (framebuffer);
  int framebuffer_height = cogl_framebuffer_get_height (framebuffer);
  int *rectangles = g_alloca (sizeof (int) * n_rectangles * 4);
  int i;

  /* glXCopySubBuffer expects rectangles relative to the bottom left corner but
   * we are given rectangles relative to the top left so we need to flip
   * them... */
  memcpy (rectangles, user_rectangles, sizeof (int) * n_rectangles * 4);
  for (i = 0; i < n_rectangles; i++)
    {
      int *rect = &rectangles[4 * i];

      if (i == 0)
        {
          x_min = rect[0];
          x_max = rect[0] + rect[2];
          y_min = rect[1];
          y_max = rect[1] + rect[3];
        }
      else
        {
          x_min = MIN (x_min, rect[0]);
          x_max = MAX (x_max, rect[0] + rect[2]);
          y_min = MIN (y_min, rect[1]);
          y_max = MAX (y_max, rect[1] + rect[3]);
        }

      rect[1] = framebuffer_height - rect[1] - rect[3];

    }

  cogl_context_flush_framebuffer_state (context,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);

  have_counter = glx_display->have_vblank_counter;
  can_wait = glx_display->can_vblank_wait;

  /* We need to ensure that all the rendering is done, otherwise
   * redraw operations that are slower than the framerate can
   * queue up in the pipeline during a heavy animation, causing a
   * larger and larger backlog of rendering visible as lag to the
   * user.
   *
   * For an exaggerated example consider rendering at 60fps (so 16ms
   * per frame) and you have a really slow frame that takes 160ms to
   * render, even though painting the scene and issuing the commands
   * to the GPU takes no time at all. If all we did was use the
   * video_sync extension to throttle the painting done by the CPU
   * then every 16ms we would have another frame queued up even though
   * the GPU has only rendered one tenth of the current frame. By the
   * time the GPU would get to the 2nd frame there would be 9 frames
   * waiting to be rendered.
   *
   * The problem is that we don't currently have a good way to throttle
   * the GPU, only the CPU so we have to resort to synchronizing the
   * GPU with the CPU to throttle it.
   *
   * Note: calling glFinish() and synchronizing the CPU with the GPU is far
   * from ideal. One idea is to use sync objects to track render completion
   * so we can throttle the backlog (ideally with an additional extension that
   * lets us get notifications in our mainloop instead of having to busy wait
   * for the completion).
   */
  cogl_framebuffer_finish (framebuffer);

  if (blit_sub_buffer_is_synchronized && have_counter && can_wait)
    {
      end_frame_vsync_counter = _cogl_winsys_get_vsync_counter (context);

      /* If we have the GLX_SGI_video_sync extension then we can
       * be a bit smarter about how we throttle blits by avoiding
       * any waits if we can see that the video sync count has
       * already progressed. */
      if (onscreen_glx->last_swap_vsync_counter == end_frame_vsync_counter)
        _cogl_winsys_wait_for_vblank (onscreen);
    }
  else if (can_wait)
    _cogl_winsys_wait_for_vblank (onscreen);

  if (glx_renderer->glXCopySubBuffer)
    {
      Display *xdpy = xlib_renderer->xdpy;
      GLXDrawable drawable;

      drawable =
        onscreen_glx->glxwin ? onscreen_glx->glxwin : onscreen_glx->xwin;
      int i;
      for (i = 0; i < n_rectangles; i++)
        {
          int *rect = &rectangles[4 * i];
          glx_renderer->glXCopySubBuffer (xdpy, drawable,
                                          rect[0], rect[1], rect[2], rect[3]);
        }
    }
  else if (context->glBlitFramebuffer)
    {
      int i;
      /* XXX: checkout how this state interacts with the code to use
       * glBlitFramebuffer in Neil's texture atlasing branch */

      /* glBlitFramebuffer is affected by the scissor so we need to
       * ensure we have flushed an empty clip stack to get rid of it.
       * We also mark that the clip state is dirty so that it will be
       * flushed to the correct state the next time something is
       * drawn */
      _cogl_clip_stack_flush (NULL, framebuffer);
      context->current_draw_buffer_changes |= COGL_FRAMEBUFFER_STATE_CLIP;

      context->glDrawBuffer (GL_FRONT);
      for (i = 0; i < n_rectangles; i++)
        {
          int *rect = &rectangles[4 * i];
          int x2 = rect[0] + rect[2];
          int y2 = rect[1] + rect[3];
          context->glBlitFramebuffer (rect[0], rect[1], x2, y2,
                                      rect[0], rect[1], x2, y2,
                                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
      context->glDrawBuffer (context->current_gl_draw_buffer);
    }

  /* NB: unlike glXSwapBuffers, glXCopySubBuffer and
   * glBlitFramebuffer don't issue an implicit glFlush() so we
   * have to flush ourselves if we want the request to complete in
   * a finite amount of time since otherwise the driver can batch
   * the command indefinitely. */
  context->glFlush ();

  /* NB: It's important we save the counter we read before acting on
   * the swap request since if we are mixing and matching different
   * swap methods between frames we don't want to read the timer e.g.
   * after calling glFinish() some times and not for others.
   *
   * In other words; this way we consistently save the time at the end
   * of the applications frame such that the counter isn't muddled by
   * the varying costs of different swap methods.
   */
  if (have_counter)
    onscreen_glx->last_swap_vsync_counter = end_frame_vsync_counter;

  {
    CoglOutput *output;

    x_min = CLAMP (x_min, 0, framebuffer_width);
    x_max = CLAMP (x_max, 0, framebuffer_width);
    y_min = CLAMP (y_min, 0, framebuffer_height);
    y_max = CLAMP (y_max, 0, framebuffer_height);

    output =
      _cogl_xlib_renderer_output_for_rectangle (context->display->renderer,
                                                onscreen_glx->x + x_min,
                                                onscreen_glx->y + y_min,
                                                x_max - x_min,
                                                y_max - y_min);

    set_frame_info_output (onscreen, output);
  }

  /* XXX: we don't get SwapComplete events based on how we implement
   * the _swap_region() API but if cogl-onscreen.c knows we are
   * handling _SYNC and _COMPLETE events in the winsys then we need to
   * send fake events in this case.
   */
  if (_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT))
    {
      set_sync_pending (onscreen);
      set_complete_pending (onscreen);
    }
}

static void
cogl_onscreen_glx_swap_buffers_with_damage (CoglOnscreen  *onscreen,
                                            const int     *rectangles,
                                            int            n_rectangles,
                                            CoglFrameInfo *info,
                                            gpointer       user_data)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (context->display->renderer);
  CoglGLXRenderer *glx_renderer = context->display->renderer->winsys;
  CoglGLXDisplay *glx_display = context->display->winsys;
  gboolean have_counter;
  GLXDrawable drawable;

  /* XXX: theoretically this shouldn't be necessary but at least with
   * the Intel drivers we have see that if we don't call
   * glXMakeContextCurrent for the drawable we are swapping then
   * we get a BadDrawable error from the X server. */
  cogl_context_flush_framebuffer_state (context,
                                        framebuffer,
                                        framebuffer,
                                        COGL_FRAMEBUFFER_STATE_BIND);

  drawable = onscreen_glx->glxwin ? onscreen_glx->glxwin : onscreen_glx->xwin;

  have_counter = glx_display->have_vblank_counter;

  if (!glx_renderer->glXSwapInterval)
    {
      gboolean can_wait = have_counter || glx_display->can_vblank_wait;

      uint32_t end_frame_vsync_counter = 0;

      /* If the swap_region API is also being used then we need to track
       * the vsync counter for each swap request so we can manually
       * throttle swap_region requests. */
      if (have_counter)
        end_frame_vsync_counter = _cogl_winsys_get_vsync_counter (context);

      /* If we are going to wait for VBLANK manually, we not only
       * need to flush out pending drawing to the GPU before we
       * sleep, we need to wait for it to finish. Otherwise, we
       * may end up with the situation:
       *
       *        - We finish drawing      - GPU drawing continues
       *        - We go to sleep         - GPU drawing continues
       * VBLANK - We call glXSwapBuffers - GPU drawing continues
       *                                 - GPU drawing continues
       *                                 - Swap buffers happens
       *
       * Producing a tear. Calling glFinish() first will cause us
       * to properly wait for the next VBLANK before we swap. This
       * obviously does not happen when we use _GLX_SWAP and let
       * the driver do the right thing
       */
      cogl_framebuffer_finish (framebuffer);

      if (have_counter && can_wait)
        {
          if (onscreen_glx->last_swap_vsync_counter ==
              end_frame_vsync_counter)
            _cogl_winsys_wait_for_vblank (onscreen);
        }
      else if (can_wait)
        _cogl_winsys_wait_for_vblank (onscreen);
    }

  glx_renderer->glXSwapBuffers (xlib_renderer->xdpy, drawable);

  if (have_counter)
    onscreen_glx->last_swap_vsync_counter =
      _cogl_winsys_get_vsync_counter (context);

  set_frame_info_output (onscreen, onscreen_glx->output);
}

static Window
cogl_onscreen_glx_get_x11_window (CoglX11Onscreen *x11_onscreen)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (x11_onscreen);

  return onscreen_glx->xwin;
}

void
cogl_onscreen_glx_notify_swap_buffers (CoglOnscreen          *onscreen,
                                       GLXBufferSwapComplete *swap_event)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  gboolean ust_is_monotonic;
  CoglFrameInfo *info;

  /* We only want to notify that the swap is complete when the
     application calls cogl_context_dispatch so instead of immediately
     notifying we'll set a flag to remember to notify later */
  set_sync_pending (onscreen);

  info = cogl_onscreen_peek_head_frame_info (onscreen);
  info->flags |= COGL_FRAME_INFO_FLAG_VSYNC;

  ust_is_monotonic = is_ust_monotonic (context->display->renderer,
                                       onscreen_glx->glxwin);

  if (swap_event->ust != 0 && ust_is_monotonic)
    {
      info->presentation_time_us =
        ust_to_microseconds (context->display->renderer,
                             onscreen_glx->glxwin,
                             swap_event->ust);
      info->flags |= COGL_FRAME_INFO_FLAG_HW_CLOCK;
    }

  /* Intentionally truncating to lower 32 bits, same as DRM. */
  info->sequence = swap_event->msc;

  set_complete_pending (onscreen);
}

void
cogl_onscreen_glx_update_output (CoglOnscreen *onscreen)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglDisplay *display = context->display;
  CoglOutput *output;
  int width, height;

  width = cogl_framebuffer_get_width (framebuffer);
  height = cogl_framebuffer_get_height (framebuffer);
  output = _cogl_xlib_renderer_output_for_rectangle (display->renderer,
                                                     onscreen_glx->x,
                                                     onscreen_glx->y,
                                                     width, height);
  if (onscreen_glx->output != output)
    {
      if (onscreen_glx->output)
        g_object_unref (onscreen_glx->output);

      onscreen_glx->output = output;

      if (output)
        g_object_ref (onscreen_glx->output);
    }
}

void
cogl_onscreen_glx_resize (CoglOnscreen    *onscreen,
                          XConfigureEvent *configure_event)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);
  CoglFramebuffer *framebuffer = COGL_FRAMEBUFFER (onscreen);
  CoglContext *context = cogl_framebuffer_get_context (framebuffer);
  CoglRenderer *renderer = context->display->renderer;
  CoglGLXRenderer *glx_renderer = renderer->winsys;
  int x, y;


  _cogl_framebuffer_winsys_update_size (framebuffer,
                                        configure_event->width,
                                        configure_event->height);

  /* We only want to notify that a resize happened when the
   * application calls cogl_context_dispatch so instead of immediately
   * notifying we queue an idle callback */
  if (!glx_renderer->flush_notifications_idle)
    {
      glx_renderer->flush_notifications_idle =
        _cogl_poll_renderer_add_idle (renderer,
                                      flush_pending_notifications_idle,
                                      context,
                                      NULL);
    }

  if (configure_event->send_event)
    {
      x = configure_event->x;
      y = configure_event->y;
    }
  else
    {
      Window child;
      XTranslateCoordinates (configure_event->display,
                             configure_event->window,
                             DefaultRootWindow (configure_event->display),
                             0, 0, &x, &y, &child);
    }

  onscreen_glx->x = x;
  onscreen_glx->y = y;

  cogl_onscreen_glx_update_output (onscreen);
}

gboolean
cogl_onscreen_glx_is_for_window (CoglOnscreen *onscreen,
                                 Window        window)
{
  CoglOnscreenGlx *onscreen_glx = COGL_ONSCREEN_GLX (onscreen);

  return onscreen_glx->xwin == window;
}

CoglOnscreenGlx *
cogl_onscreen_glx_new (CoglContext *context,
                       int          width,
                       int          height)
{
  CoglFramebufferDriverConfig driver_config;

  driver_config = (CoglFramebufferDriverConfig) {
    .type = COGL_FRAMEBUFFER_DRIVER_TYPE_BACK,
  };
  return g_object_new (COGL_TYPE_ONSCREEN_GLX,
                       "context", context,
                       "driver-config", &driver_config,
                       "width", width,
                       "height", height,
                       NULL);
}

static void
cogl_onscreen_glx_init (CoglOnscreenGlx *onscreen_glx)
{
}

static void
x11_onscreen_init_iface (CoglX11OnscreenInterface *iface)
{
  iface->get_x11_window = cogl_onscreen_glx_get_x11_window;
}

static void
cogl_onscreen_glx_class_init (CoglOnscreenGlxClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CoglFramebufferClass *framebuffer_class = COGL_FRAMEBUFFER_CLASS (klass);
  CoglOnscreenClass *onscreen_class = COGL_ONSCREEN_CLASS (klass);

  object_class->dispose = cogl_onscreen_glx_dispose;

  framebuffer_class->allocate = cogl_onscreen_glx_allocate;

  onscreen_class->bind = cogl_onscreen_glx_bind;
  onscreen_class->swap_buffers_with_damage =
    cogl_onscreen_glx_swap_buffers_with_damage;
  onscreen_class->swap_region = cogl_onscreen_glx_swap_region;
  onscreen_class->get_buffer_age = cogl_onscreen_glx_get_buffer_age;
}
