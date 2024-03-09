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

#include "cogl/cogl-i18n-private.h"
#include "cogl/cogl-util.h"
#include "cogl/cogl-feature-private.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-framebuffer.h"
#include "cogl/cogl-swap-chain-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/cogl-onscreen-template-private.h"
#include "cogl/cogl-private.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/cogl-frame-info-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-onscreen-private.h"
#include "cogl/cogl-swap-chain-private.h"
#include "cogl/cogl-xlib-renderer.h"
#include "cogl/cogl-util.h"
#include "cogl/cogl-poll-private.h"
#include "cogl/driver/gl/cogl-pipeline-opengl-private.h"
#include "cogl/winsys/cogl-glx.h"
#include "cogl/winsys/cogl-glx-renderer-private.h"
#include "cogl/winsys/cogl-glx-display-private.h"
#include "cogl/winsys/cogl-onscreen-glx.h"
#include "cogl/winsys/cogl-winsys-private.h"
#include "cogl/winsys/cogl-winsys-glx-private.h"
#include "mtk/mtk-x11.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <GL/glx.h>
#include <X11/Xlib.h>

#include <glib.h>

/* This is a relatively new extension */
#ifndef GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV
#define GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV 0x20F7
#endif

#define MAX_GLX_CONFIG_ATTRIBS 30

typedef struct _CoglOnscreenGlx CoglOnscreenGlx;

typedef struct _CoglContextGLX
{
  GLXDrawable current_drawable;
} CoglContextGLX;

typedef struct _CoglPixmapTextureEyeGLX
{
  CoglTexture *glx_tex;
  gboolean bind_tex_image_queued;
  gboolean pixmap_bound;
} CoglPixmapTextureEyeGLX;

typedef struct _CoglTexturePixmapGLX
{
  GLXPixmap glx_pixmap;
  gboolean has_mipmap_space;
  gboolean can_mipmap;

  CoglPixmapTextureEyeGLX left;
  CoglPixmapTextureEyeGLX right;
} CoglTexturePixmapGLX;

/* Define a set of arrays containing the functions required from GL
   for each winsys feature */
#define COGL_WINSYS_FEATURE_BEGIN(major_version, minor_version,         \
                                  name, namespaces, extension_names,    \
                                  feature_flags,                        \
                                  winsys_feature)                       \
  static const CoglFeatureFunction                                      \
  cogl_glx_feature_ ## name ## _funcs[] = {
#define COGL_WINSYS_FEATURE_FUNCTION(ret, name, args)                   \
  { G_STRINGIFY (name), G_STRUCT_OFFSET (CoglGLXRenderer, name) },
#define COGL_WINSYS_FEATURE_END()               \
  { NULL, 0 },                                  \
    };
#include "cogl/winsys/cogl-winsys-glx-feature-functions.h"

/* Define an array of features */
#undef COGL_WINSYS_FEATURE_BEGIN
#define COGL_WINSYS_FEATURE_BEGIN(major_version, minor_version,         \
                                  name, namespaces, extension_names,    \
                                  feature_flags,                        \
                                  winsys_feature)                       \
  { major_version, minor_version,                                       \
      0, namespaces, extension_names,                                   \
      0,                                                                \
      winsys_feature, \
      cogl_glx_feature_ ## name ## _funcs },
#undef COGL_WINSYS_FEATURE_FUNCTION
#define COGL_WINSYS_FEATURE_FUNCTION(ret, name, args)
#undef COGL_WINSYS_FEATURE_END
#define COGL_WINSYS_FEATURE_END()

static const CoglFeatureData winsys_feature_data[] =
  {
#include "cogl/winsys/cogl-winsys-glx-feature-functions.h"
  };

static GCallback
_cogl_winsys_renderer_get_proc_address (CoglRenderer *renderer,
                                        const char   *name)
{
  CoglGLXRenderer *glx_renderer = renderer->winsys;

  return glx_renderer->glXGetProcAddress ((const GLubyte *) name);
}

static CoglOnscreen *
find_onscreen_for_xid (CoglContext *context, uint32_t xid)
{
  GList *l;

  for (l = context->framebuffers; l; l = l->next)
    {
      CoglFramebuffer *framebuffer = l->data;
      CoglOnscreen *onscreen;

      if (!COGL_IS_ONSCREEN (framebuffer))
        continue;

      onscreen = COGL_ONSCREEN (framebuffer);
      if (cogl_onscreen_glx_is_for_window (onscreen, (Window) xid))
        return onscreen;
    }

  return NULL;
}

static void
notify_swap_buffers (CoglContext *context, GLXBufferSwapComplete *swap_event)
{
  CoglOnscreen *onscreen = find_onscreen_for_xid (context, (uint32_t)swap_event->drawable);

  if (!onscreen)
    return;

  cogl_onscreen_glx_notify_swap_buffers (onscreen, swap_event);
}

static void
notify_resize (CoglContext *context,
               XConfigureEvent *configure_event)
{
  CoglOnscreen *onscreen;

  onscreen = find_onscreen_for_xid (context, configure_event->window);
  if (!onscreen)
    return;

  cogl_onscreen_glx_resize (onscreen, configure_event);
}

static CoglFilterReturn
glx_event_filter_cb (XEvent *xevent, void *data)
{
  CoglContext *context = data;
#ifdef GLX_INTEL_swap_event
  CoglGLXRenderer *glx_renderer;
#endif

  if (xevent->type == ConfigureNotify)
    {
      notify_resize (context,
                     &xevent->xconfigure);

      /* we let ConfigureNotify pass through */
      return COGL_FILTER_CONTINUE;
    }

#ifdef GLX_INTEL_swap_event
  glx_renderer = context->display->renderer->winsys;

  if (xevent->type == (glx_renderer->glx_event_base + GLX_BufferSwapComplete))
    {
      GLXBufferSwapComplete *swap_event = (GLXBufferSwapComplete *) xevent;

      notify_swap_buffers (context, swap_event);

      /* remove SwapComplete events from the queue */
      return COGL_FILTER_REMOVE;
    }
#endif /* GLX_INTEL_swap_event */

  if (xevent->type == Expose)
    {
      CoglOnscreen *onscreen =
        find_onscreen_for_xid (context, xevent->xexpose.window);

      if (onscreen)
        {
          CoglOnscreenDirtyInfo info;

          info.x = xevent->xexpose.x;
          info.y = xevent->xexpose.y;
          info.width = xevent->xexpose.width;
          info.height = xevent->xexpose.height;

          _cogl_onscreen_queue_dirty (onscreen, &info);
        }

      return COGL_FILTER_CONTINUE;
    }

  return COGL_FILTER_CONTINUE;
}

static void
_cogl_winsys_renderer_disconnect (CoglRenderer *renderer)
{
  CoglGLXRenderer *glx_renderer = renderer->winsys;

  _cogl_xlib_renderer_disconnect (renderer);

  if (glx_renderer->libgl_module)
    g_module_close (glx_renderer->libgl_module);

  g_free (renderer->winsys);
}

static gboolean
update_all_outputs (CoglRenderer *renderer)
{
  GList *l;

  _COGL_GET_CONTEXT (context, FALSE);

  if (context->display == NULL) /* during connection */
    return FALSE;

  if (context->display->renderer != renderer)
    return FALSE;

  for (l = context->framebuffers; l; l = l->next)
    {
      CoglFramebuffer *framebuffer = l->data;

      if (!COGL_IS_ONSCREEN (framebuffer))
        continue;

      cogl_onscreen_glx_update_output (COGL_ONSCREEN (framebuffer));
    }

  return TRUE;
}

static void
_cogl_winsys_renderer_outputs_changed (CoglRenderer *renderer)
{
  update_all_outputs (renderer);
}

static void
_cogl_winsys_renderer_bind_api (CoglRenderer *renderer)
{
}

static gboolean
resolve_core_glx_functions (CoglRenderer *renderer,
                            GError **error)
{
  CoglGLXRenderer *glx_renderer;

  glx_renderer = renderer->winsys;

  if (!g_module_symbol (glx_renderer->libgl_module, "glXQueryExtension",
                        (void **) &glx_renderer->glXQueryExtension) ||
      !g_module_symbol (glx_renderer->libgl_module, "glXQueryVersion",
                        (void **) &glx_renderer->glXQueryVersion) ||
      !g_module_symbol (glx_renderer->libgl_module, "glXQueryExtensionsString",
                        (void **) &glx_renderer->glXQueryExtensionsString) ||
      (!g_module_symbol (glx_renderer->libgl_module, "glXGetProcAddress",
                         (void **) &glx_renderer->glXGetProcAddress) &&
       !g_module_symbol (glx_renderer->libgl_module, "glXGetProcAddressARB",
                         (void **) &glx_renderer->glXGetProcAddress)) ||
       !g_module_symbol (glx_renderer->libgl_module, "glXQueryDrawable",
                         (void **) &glx_renderer->glXQueryDrawable))
    {
      g_set_error_literal (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_INIT,
                           "Failed to resolve required GLX symbol");
      return FALSE;
    }

  return TRUE;
}

static void
update_base_winsys_features (CoglRenderer *renderer)
{
  CoglGLXRenderer *glx_renderer = renderer->winsys;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);
  const char *glx_extensions;
  int default_screen;
  char **split_extensions;
  int i;

  default_screen = DefaultScreen (xlib_renderer->xdpy);
  glx_extensions =
    glx_renderer->glXQueryExtensionsString (xlib_renderer->xdpy,
                                            default_screen);

  COGL_NOTE (WINSYS, "  GLX Extensions: %s", glx_extensions);

  split_extensions = g_strsplit (glx_extensions, " ", 0 /* max_tokens */);

  for (i = 0; i < G_N_ELEMENTS (winsys_feature_data); i++)
    if (_cogl_feature_check (renderer,
                             "GLX", winsys_feature_data + i,
                             glx_renderer->glx_major,
                             glx_renderer->glx_minor,
                             COGL_DRIVER_GL3, /* the driver isn't used */
                             split_extensions,
                             glx_renderer))
      {
        if (winsys_feature_data[i].winsys_feature)
          COGL_FLAGS_SET (glx_renderer->base_winsys_features,
                          winsys_feature_data[i].winsys_feature,
                          TRUE);
      }

  g_strfreev (split_extensions);

  /* The GLX_SGI_video_sync spec explicitly states this extension
   * only works for direct contexts; we don't know per-renderer
   * if the context is direct or not, so we turn off the feature
   * flag; we still use the extension within this file looking
   * instead at glx_display->have_vblank_counter.
   */
  COGL_FLAGS_SET (glx_renderer->base_winsys_features,
                  COGL_WINSYS_FEATURE_VBLANK_COUNTER,
                  FALSE);

  /* Because of the direct-context dependency, the VBLANK_WAIT feature
   * doesn't reflect the presence of GLX_SGI_video_sync.
   */
  if (glx_renderer->glXWaitForMsc)
    COGL_FLAGS_SET (glx_renderer->base_winsys_features,
                    COGL_WINSYS_FEATURE_VBLANK_WAIT,
                    TRUE);
}

static gboolean
_cogl_winsys_renderer_connect (CoglRenderer *renderer,
                               GError **error)
{
  CoglGLXRenderer *glx_renderer;
  CoglXlibRenderer *xlib_renderer;

  renderer->winsys = g_new0 (CoglGLXRenderer, 1);

  glx_renderer = renderer->winsys;
  xlib_renderer = _cogl_xlib_renderer_get_data (renderer);

  if (!_cogl_xlib_renderer_connect (renderer, error))
    goto error;

  if (renderer->driver != COGL_DRIVER_GL3)
    {
      g_set_error_literal (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_INIT,
                           "GLX Backend can only be used in conjunction with OpenGL");
      goto error;
    }

  glx_renderer->libgl_module = g_module_open (COGL_GL_LIBNAME,
                                              G_MODULE_BIND_LAZY);

  if (glx_renderer->libgl_module == NULL)
    {
      g_set_error_literal (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_INIT,
                           "Failed to dynamically open the OpenGL library");
      goto error;
    }

  if (!resolve_core_glx_functions (renderer, error))
    goto error;

  if (!glx_renderer->glXQueryExtension (xlib_renderer->xdpy,
                                        &glx_renderer->glx_error_base,
                                        &glx_renderer->glx_event_base))
    {
      g_set_error_literal (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_INIT,
                           "XServer appears to lack required GLX support");
      goto error;
    }

  /* XXX: Note: For a long time Mesa exported a hybrid GLX, exporting
   * extensions specified to require GLX 1.3, but still reporting 1.2
   * via glXQueryVersion. */
  if (!glx_renderer->glXQueryVersion (xlib_renderer->xdpy,
                                      &glx_renderer->glx_major,
                                      &glx_renderer->glx_minor)
      || !(glx_renderer->glx_major == 1 && glx_renderer->glx_minor >= 2))
    {
      g_set_error_literal (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_INIT,
                           "XServer appears to lack required GLX 1.2 support");
      goto error;
    }

  update_base_winsys_features (renderer);

  glx_renderer->dri_fd = -1;

  return TRUE;

error:
  _cogl_winsys_renderer_disconnect (renderer);
  return FALSE;
}

static gboolean
update_winsys_features (CoglContext *context, GError **error)
{
  CoglGLXDisplay *glx_display = context->display->winsys;
  CoglGLXRenderer *glx_renderer = context->display->renderer->winsys;

  g_return_val_if_fail (glx_display->glx_context, FALSE);

  if (!_cogl_context_update_features (context, error))
    return FALSE;

  memcpy (context->winsys_features,
          glx_renderer->base_winsys_features,
          sizeof (context->winsys_features));

  if (glx_renderer->glXCopySubBuffer || context->glBlitFramebuffer)
    COGL_FLAGS_SET (context->winsys_features, COGL_WINSYS_FEATURE_SWAP_REGION, TRUE);

  /* Note: glXCopySubBuffer and glBlitFramebuffer won't be throttled
   * by the SwapInterval so we have to throttle swap_region requests
   * manually... */
  if (_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_SWAP_REGION) &&
      (glx_display->have_vblank_counter || glx_display->can_vblank_wait))
    COGL_FLAGS_SET (context->winsys_features,
                    COGL_WINSYS_FEATURE_SWAP_REGION_THROTTLE, TRUE);

  if (_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_SYNC_AND_COMPLETE_EVENT))
    {
      COGL_FLAGS_SET (context->winsys_features,
                      COGL_WINSYS_FEATURE_SWAP_BUFFERS_EVENT, TRUE);
    }

  /* We'll manually handle queueing dirty events in response to
   * Expose events from X */
  COGL_FLAGS_SET (context->private_features,
                  COGL_PRIVATE_FEATURE_DIRTY_EVENTS,
                  TRUE);

  if (_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_BUFFER_AGE))
    COGL_FLAGS_SET (context->features, COGL_FEATURE_ID_BUFFER_AGE, TRUE);

  return TRUE;
}

static void
glx_attributes_from_framebuffer_config (CoglDisplay                 *display,
                                        const CoglFramebufferConfig *config,
                                        int                         *attributes)
{
  CoglGLXRenderer *glx_renderer = display->renderer->winsys;
  int i = 0;

  attributes[i++] = GLX_DRAWABLE_TYPE;
  attributes[i++] = GLX_WINDOW_BIT;

  attributes[i++] = GLX_RENDER_TYPE;
  attributes[i++] = GLX_RGBA_BIT;

  attributes[i++] = GLX_DOUBLEBUFFER;
  attributes[i++] = GL_TRUE;

  attributes[i++] = GLX_RED_SIZE;
  attributes[i++] = 1;
  attributes[i++] = GLX_GREEN_SIZE;
  attributes[i++] = 1;
  attributes[i++] = GLX_BLUE_SIZE;
  attributes[i++] = 1;
  attributes[i++] = GLX_ALPHA_SIZE;
  attributes[i++] = GLX_DONT_CARE;
  attributes[i++] = GLX_DEPTH_SIZE;
  attributes[i++] = 1;
  attributes[i++] = GLX_STENCIL_SIZE;
  attributes[i++] = config->need_stencil ? 2 : 0;
  if (config->stereo_enabled)
    {
      attributes[i++] = GLX_STEREO;
      attributes[i++] = TRUE;
    }

  if (glx_renderer->glx_major == 1 && glx_renderer->glx_minor >= 4 &&
      config->samples_per_pixel)
    {
      attributes[i++] = GLX_SAMPLE_BUFFERS;
      attributes[i++] = 1;
      attributes[i++] = GLX_SAMPLES;
      attributes[i++] = config->samples_per_pixel;
    }

  attributes[i++] = None;

  g_assert (i < MAX_GLX_CONFIG_ATTRIBS);
}

/* It seems the GLX spec never defined an invalid GLXFBConfig that
 * we could overload as an indication of error, so we have to return
 * an explicit boolean status. */
gboolean
cogl_display_glx_find_fbconfig (CoglDisplay                  *display,
                                const CoglFramebufferConfig  *config,
                                GLXFBConfig                  *config_ret,
                                GError                      **error)
{
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (display->renderer);
  CoglGLXRenderer *glx_renderer = display->renderer->winsys;
  GLXFBConfig *configs = NULL;
  int n_configs;
  static int attributes[MAX_GLX_CONFIG_ATTRIBS];
  gboolean ret = TRUE;
  int xscreen_num = DefaultScreen (xlib_renderer->xdpy);

  glx_attributes_from_framebuffer_config (display, config, attributes);

  configs = glx_renderer->glXChooseFBConfig (xlib_renderer->xdpy,
                                             xscreen_num,
                                             attributes,
                                             &n_configs);

  if (!configs || n_configs == 0)
    {
      g_set_error_literal (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_CREATE_CONTEXT,
                           "Failed to find any compatible fbconfigs");
      ret = FALSE;
      goto done;
    }

  COGL_NOTE (WINSYS, "Using the first available FBConfig");
  *config_ret = configs[0];

done:
  XFree (configs);
  return ret;
}

static GLXContext
create_gl3_context (CoglDisplay *display,
                    GLXFBConfig fb_config)
{
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (display->renderer);
  CoglGLXRenderer *glx_renderer = display->renderer->winsys;

  /* We want a core profile 3.1 context with no deprecated features */
  static const int attrib_list[] =
    {
      GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
      GLX_CONTEXT_MINOR_VERSION_ARB, 1,
      GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
      GLX_CONTEXT_FLAGS_ARB,         GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
      None
    };
  /* NV_robustness_video_memory_purge relies on GLX_ARB_create_context
     and in part on ARB_robustness. Namely, it needs the notification
     strategy to be set to GLX_LOSE_CONTEXT_ON_RESET_ARB and that the
     driver exposes the GetGraphicsResetStatusARB function. This means
     we don't actually enable robust buffer access. */
  static const int attrib_list_reset_on_purge[] =
    {
      GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
      GLX_CONTEXT_MINOR_VERSION_ARB, 1,
      GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
      GLX_CONTEXT_FLAGS_ARB,         GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
      GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV,
                                     GL_TRUE,
      GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
                                     GLX_LOSE_CONTEXT_ON_RESET_ARB,
      None
    };

  /* Make sure that the display supports the GLX_ARB_create_context
     extension */
  if (glx_renderer->glXCreateContextAttribs == NULL)
    return NULL;

  /* We can't check the presence of this extension with the usual
     COGL_WINSYS_FEATURE machinery because that only gets initialized
     later when the CoglContext is created. */
  if (display->renderer->xlib_want_reset_on_video_memory_purge &&
      strstr (glx_renderer->glXQueryExtensionsString (xlib_renderer->xdpy,
                                                      DefaultScreen (xlib_renderer->xdpy)),
              "GLX_NV_robustness_video_memory_purge"))
    {
      GLXContext ctx;

      mtk_x11_error_trap_push (xlib_renderer->xdpy);
      ctx = glx_renderer->glXCreateContextAttribs (xlib_renderer->xdpy,
                                                   fb_config,
                                                   NULL /* share_context */,
                                                   True, /* direct */
                                                   attrib_list_reset_on_purge);
      if (!mtk_x11_error_trap_pop_with_return (xlib_renderer->xdpy) && ctx)
        return ctx;
    }

  return glx_renderer->glXCreateContextAttribs (xlib_renderer->xdpy,
                                                fb_config,
                                                NULL /* share_context */,
                                                True, /* direct */
                                                attrib_list);
}

static gboolean
create_context (CoglDisplay *display, GError **error)
{
  CoglGLXDisplay *glx_display = display->winsys;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (display->renderer);
  CoglGLXRenderer *glx_renderer = display->renderer->winsys;
  GLXFBConfig config;
  GError *fbconfig_error = NULL;
  XSetWindowAttributes attrs;
  XVisualInfo *xvisinfo;
  GLXDrawable dummy_drawable;

  g_return_val_if_fail (glx_display->glx_context == NULL, TRUE);

  glx_display->found_fbconfig =
    cogl_display_glx_find_fbconfig (display,
                                    &display->onscreen_template->config,
                                    &config,
                                    &fbconfig_error);
  if (!glx_display->found_fbconfig)
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_CONTEXT,
                   "Unable to find suitable fbconfig for the GLX context: %s",
                   fbconfig_error->message);
      g_error_free (fbconfig_error);
      return FALSE;
    }

  glx_display->fbconfig = config;

  COGL_NOTE (WINSYS, "Creating GLX Context (display: %p)",
             xlib_renderer->xdpy);

  mtk_x11_error_trap_push (xlib_renderer->xdpy);

  if (display->renderer->driver == COGL_DRIVER_GL3)
    glx_display->glx_context = create_gl3_context (display, config);
  else
    glx_display->glx_context =
      glx_renderer->glXCreateNewContext (xlib_renderer->xdpy,
                                         config,
                                         GLX_RGBA_TYPE,
                                         NULL,
                                         True);

  if (mtk_x11_error_trap_pop_with_return (xlib_renderer->xdpy) ||
      glx_display->glx_context == NULL)
    {
      g_set_error_literal (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_CREATE_CONTEXT,
                           "Unable to create suitable GL context");
      return FALSE;
    }

  glx_display->is_direct =
    glx_renderer->glXIsDirect (xlib_renderer->xdpy, glx_display->glx_context);
  glx_display->have_vblank_counter = glx_display->is_direct && glx_renderer->glXWaitVideoSync;
  glx_display->can_vblank_wait = glx_renderer->glXWaitForMsc || glx_display->have_vblank_counter;

  COGL_NOTE (WINSYS, "Setting %s context",
             glx_display->is_direct ? "direct" : "indirect");

  /* XXX: GLX doesn't let us make a context current without a window
   * so we create a dummy window that we can use while no CoglOnscreen
   * framebuffer is in use.
   */

  xvisinfo = glx_renderer->glXGetVisualFromFBConfig (xlib_renderer->xdpy,
                                                     config);
  if (xvisinfo == NULL)
    {
      g_set_error_literal (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_CREATE_CONTEXT,
                           "Unable to retrieve the X11 visual");
      return FALSE;
    }

  mtk_x11_error_trap_push (xlib_renderer->xdpy);

  attrs.override_redirect = True;
  attrs.colormap = XCreateColormap (xlib_renderer->xdpy,
                                    DefaultRootWindow (xlib_renderer->xdpy),
                                    xvisinfo->visual,
                                    AllocNone);
  attrs.border_pixel = 0;

  glx_display->dummy_xwin =
    XCreateWindow (xlib_renderer->xdpy,
                   DefaultRootWindow (xlib_renderer->xdpy),
                   -100, -100, 1, 1,
                   0,
                   xvisinfo->depth,
                   CopyFromParent,
                   xvisinfo->visual,
                   CWOverrideRedirect | CWColormap | CWBorderPixel,
                   &attrs);

  /* Try and create a GLXWindow to use with extensions dependent on
   * GLX versions >= 1.3 that don't accept regular X Windows as GLX
   * drawables. */
  if (glx_renderer->glx_major == 1 && glx_renderer->glx_minor >= 3)
    {
      glx_display->dummy_glxwin =
        glx_renderer->glXCreateWindow (xlib_renderer->xdpy,
                                       config,
                                       glx_display->dummy_xwin,
                                       NULL);
    }

  if (glx_display->dummy_glxwin)
    dummy_drawable = glx_display->dummy_glxwin;
  else
    dummy_drawable = glx_display->dummy_xwin;

  COGL_NOTE (WINSYS, "Selecting dummy 0x%x for the GLX context",
             (unsigned int) dummy_drawable);

  glx_renderer->glXMakeContextCurrent (xlib_renderer->xdpy,
                                       dummy_drawable,
                                       dummy_drawable,
                                       glx_display->glx_context);

  xlib_renderer->xvisinfo = xvisinfo;

  if (mtk_x11_error_trap_pop_with_return (xlib_renderer->xdpy))
    {
      g_set_error_literal (error, COGL_WINSYS_ERROR,
                           COGL_WINSYS_ERROR_CREATE_CONTEXT,
                           "Unable to select the newly created GLX context");
      return FALSE;
    }

  return TRUE;
}

static void
_cogl_winsys_display_destroy (CoglDisplay *display)
{
  CoglGLXDisplay *glx_display = display->winsys;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (display->renderer);
  CoglGLXRenderer *glx_renderer = display->renderer->winsys;

  g_return_if_fail (glx_display != NULL);

  if (glx_display->glx_context)
    {
      glx_renderer->glXMakeContextCurrent (xlib_renderer->xdpy,
                                           None, None, NULL);
      glx_renderer->glXDestroyContext (xlib_renderer->xdpy,
                                       glx_display->glx_context);
      glx_display->glx_context = NULL;
    }

  if (glx_display->dummy_glxwin)
    {
      glx_renderer->glXDestroyWindow (xlib_renderer->xdpy,
                                      glx_display->dummy_glxwin);
      glx_display->dummy_glxwin = None;
    }

  if (glx_display->dummy_xwin)
    {
      XDestroyWindow (xlib_renderer->xdpy, glx_display->dummy_xwin);
      glx_display->dummy_xwin = None;
    }

  g_free (display->winsys);
  display->winsys = NULL;
}

static gboolean
_cogl_winsys_display_setup (CoglDisplay *display,
                            GError **error)
{
  CoglGLXDisplay *glx_display;
  int i;

  g_return_val_if_fail (display->winsys == NULL, FALSE);

  glx_display = g_new0 (CoglGLXDisplay, 1);
  display->winsys = glx_display;

  if (!create_context (display, error))
    goto error;

  for (i = 0; i < COGL_GLX_N_CACHED_CONFIGS; i++)
    glx_display->glx_cached_configs[i].depth = -1;

  return TRUE;

error:
  _cogl_winsys_display_destroy (display);
  return FALSE;
}

static gboolean
_cogl_winsys_context_init (CoglContext *context, GError **error)
{
  context->winsys = g_new0 (CoglContextGLX, 1);

  cogl_xlib_renderer_add_filter (context->display->renderer,
                                 glx_event_filter_cb,
                                 context);
  return update_winsys_features (context, error);
}

static void
_cogl_winsys_context_deinit (CoglContext *context)
{
  cogl_xlib_renderer_remove_filter (context->display->renderer,
                                    glx_event_filter_cb,
                                    context);
  g_free (context->winsys);
}

static gboolean
get_fbconfig_for_depth (CoglContext *context,
                        unsigned int depth,
                        gboolean stereo,
                        GLXFBConfig *fbconfig_ret,
                        gboolean *can_mipmap_ret)
{
  CoglXlibRenderer *xlib_renderer;
  CoglGLXRenderer *glx_renderer;
  CoglGLXDisplay *glx_display;
  Display *dpy;
  GLXFBConfig *fbconfigs;
  int n_elements, i;
  int db, stencil, alpha, mipmap, rgba, value;
  int spare_cache_slot = 0;
  gboolean found = FALSE;

  xlib_renderer = _cogl_xlib_renderer_get_data (context->display->renderer);
  glx_renderer = context->display->renderer->winsys;
  glx_display = context->display->winsys;

  /* Check if we've already got a cached config for this depth and stereo */
  for (i = 0; i < COGL_GLX_N_CACHED_CONFIGS; i++)
    if (glx_display->glx_cached_configs[i].depth == -1)
      spare_cache_slot = i;
    else if (glx_display->glx_cached_configs[i].depth == depth &&
             glx_display->glx_cached_configs[i].stereo == stereo)
      {
        *fbconfig_ret = glx_display->glx_cached_configs[i].fb_config;
        *can_mipmap_ret = glx_display->glx_cached_configs[i].can_mipmap;
        return glx_display->glx_cached_configs[i].found;
      }

  dpy = xlib_renderer->xdpy;

  fbconfigs = glx_renderer->glXGetFBConfigs (dpy, DefaultScreen (dpy),
                                             &n_elements);

  db = G_MAXSHORT;
  stencil = G_MAXSHORT;
  mipmap = 0;
  rgba = 0;

  for (i = 0; i < n_elements; i++)
    {
      XVisualInfo *vi;
      int visual_depth;

      vi = glx_renderer->glXGetVisualFromFBConfig (dpy, fbconfigs[i]);
      if (vi == NULL)
        continue;

      visual_depth = vi->depth;

      XFree (vi);

      if (visual_depth != depth)
        continue;

      glx_renderer->glXGetFBConfigAttrib (dpy,
                                          fbconfigs[i],
                                          GLX_ALPHA_SIZE,
                                          &alpha);
      glx_renderer->glXGetFBConfigAttrib (dpy,
                                          fbconfigs[i],
                                          GLX_BUFFER_SIZE,
                                          &value);
      if (value != depth && (value - alpha) != depth)
        continue;

      glx_renderer->glXGetFBConfigAttrib (dpy,
                                          fbconfigs[i],
                                          GLX_STEREO,
                                          &value);
      if (!!value != !!stereo)
        continue;

      if (glx_renderer->glx_major == 1 && glx_renderer->glx_minor >= 4)
        {
          glx_renderer->glXGetFBConfigAttrib (dpy,
                                              fbconfigs[i],
                                              GLX_SAMPLES,
                                              &value);
          if (value > 1)
            continue;
        }

      value = 0;
      if (depth == 32)
        {
          glx_renderer->glXGetFBConfigAttrib (dpy,
                                              fbconfigs[i],
                                              GLX_BIND_TO_TEXTURE_RGBA_EXT,
                                              &value);
          if (value)
            rgba = 1;
        }

      if (!value)
        {
          if (rgba)
            continue;

          glx_renderer->glXGetFBConfigAttrib (dpy,
                                              fbconfigs[i],
                                              GLX_BIND_TO_TEXTURE_RGB_EXT,
                                              &value);
          if (!value)
            continue;
        }

      glx_renderer->glXGetFBConfigAttrib (dpy,
                                          fbconfigs[i],
                                          GLX_DOUBLEBUFFER,
                                          &value);
      if (value > db)
        continue;

      db = value;

      glx_renderer->glXGetFBConfigAttrib (dpy,
                                          fbconfigs[i],
                                          GLX_STENCIL_SIZE,
                                          &value);
      if (value > stencil)
        continue;

      stencil = value;

      glx_renderer->glXGetFBConfigAttrib (dpy,
                                          fbconfigs[i],
                                          GLX_BIND_TO_MIPMAP_TEXTURE_EXT,
                                          &value);

      if (value < mipmap)
        continue;

      mipmap = value;

      *fbconfig_ret = fbconfigs[i];
      *can_mipmap_ret = mipmap;
      found = TRUE;
    }

  if (n_elements)
    XFree (fbconfigs);

  glx_display->glx_cached_configs[spare_cache_slot].depth = depth;
  glx_display->glx_cached_configs[spare_cache_slot].found = found;
  glx_display->glx_cached_configs[spare_cache_slot].fb_config = *fbconfig_ret;
  glx_display->glx_cached_configs[spare_cache_slot].can_mipmap = mipmap;

  return found;
}

static gboolean
try_create_glx_pixmap (CoglContext *context,
                       CoglTexturePixmapX11 *tex_pixmap,
                       gboolean mipmap)
{
  CoglTexturePixmapGLX *glx_tex_pixmap = tex_pixmap->winsys;
  CoglRenderer *renderer;
  CoglXlibRenderer *xlib_renderer;
  CoglGLXRenderer *glx_renderer;
  Display *dpy;
  /* We have to initialize this *opaque* variable because gcc tries to
   * be too smart for its own good and warns that the variable may be
   * used uninitialized otherwise. */
  GLXFBConfig fb_config = (GLXFBConfig)0;
  int attribs[7];
  int i = 0;

  unsigned int depth = tex_pixmap->depth;
  Visual* visual = tex_pixmap->visual;

  renderer = context->display->renderer;
  xlib_renderer = _cogl_xlib_renderer_get_data (renderer);
  glx_renderer = renderer->winsys;
  dpy = xlib_renderer->xdpy;

  if (!get_fbconfig_for_depth (context, depth,
                               tex_pixmap->stereo_mode != COGL_TEXTURE_PIXMAP_MONO,
                               &fb_config,
                               &glx_tex_pixmap->can_mipmap))
    {
      COGL_NOTE (TEXTURE_PIXMAP, "No suitable FBConfig found for depth %i",
                 depth);
      return FALSE;
    }

  if (!glx_tex_pixmap->can_mipmap)
    mipmap = FALSE;

  attribs[i++] = GLX_TEXTURE_FORMAT_EXT;

  /* Check whether an alpha channel is used by comparing the total
   * number of 1-bits in color masks against the color depth requested
   * by the client.
   */
  if (_cogl_util_popcountl (visual->red_mask |
                            visual->green_mask |
                            visual->blue_mask) == depth)
    attribs[i++] = GLX_TEXTURE_FORMAT_RGB_EXT;
  else
    attribs[i++] = GLX_TEXTURE_FORMAT_RGBA_EXT;

  attribs[i++] = GLX_MIPMAP_TEXTURE_EXT;
  attribs[i++] = mipmap;

  attribs[i++] = GLX_TEXTURE_TARGET_EXT;
  attribs[i++] = GLX_TEXTURE_2D_EXT;

  attribs[i++] = None;

  /* We need to trap errors from glXCreatePixmap because it can
   * sometimes fail during normal usage. For example on NVidia it gets
   * upset if you try to create two GLXPixmaps for the same drawable.
   */

  mtk_x11_error_trap_push (xlib_renderer->xdpy);

  glx_tex_pixmap->glx_pixmap =
    glx_renderer->glXCreatePixmap (dpy,
                                   fb_config,
                                   tex_pixmap->pixmap,
                                   attribs);
  glx_tex_pixmap->has_mipmap_space = mipmap;

  XSync (dpy, False);

  if (mtk_x11_error_trap_pop_with_return (xlib_renderer->xdpy))
    {
      COGL_NOTE (TEXTURE_PIXMAP, "Failed to create pixmap for %p", tex_pixmap);
      mtk_x11_error_trap_push (xlib_renderer->xdpy);
      glx_renderer->glXDestroyPixmap (dpy, glx_tex_pixmap->glx_pixmap);
      XSync (dpy, False);
      mtk_x11_error_trap_pop (xlib_renderer->xdpy);

      glx_tex_pixmap->glx_pixmap = None;
      return FALSE;
    }

  return TRUE;
}

static gboolean
_cogl_winsys_texture_pixmap_x11_create (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglTexturePixmapGLX *glx_tex_pixmap;
  CoglContext *ctx = cogl_texture_get_context (COGL_TEXTURE (tex_pixmap));

  if (!_cogl_winsys_has_feature (COGL_WINSYS_FEATURE_TEXTURE_FROM_PIXMAP))
    {
      tex_pixmap->winsys = NULL;
      return FALSE;
    }

  glx_tex_pixmap = g_new0 (CoglTexturePixmapGLX, 1);

  glx_tex_pixmap->glx_pixmap = None;
  glx_tex_pixmap->can_mipmap = FALSE;
  glx_tex_pixmap->has_mipmap_space = FALSE;

  glx_tex_pixmap->left.glx_tex = NULL;
  glx_tex_pixmap->right.glx_tex = NULL;

  glx_tex_pixmap->left.bind_tex_image_queued = TRUE;
  glx_tex_pixmap->right.bind_tex_image_queued = TRUE;
  glx_tex_pixmap->left.pixmap_bound = FALSE;
  glx_tex_pixmap->right.pixmap_bound = FALSE;

  tex_pixmap->winsys = glx_tex_pixmap;

  if (!try_create_glx_pixmap (ctx, tex_pixmap, FALSE))
    {
      tex_pixmap->winsys = NULL;
      g_free (glx_tex_pixmap);
      return FALSE;
    }

  return TRUE;
}

static void
free_glx_pixmap (CoglContext *context,
                 CoglTexturePixmapGLX *glx_tex_pixmap)
{
  CoglRenderer *renderer;
  CoglXlibRenderer *xlib_renderer;
  CoglGLXRenderer *glx_renderer;

  renderer = context->display->renderer;
  xlib_renderer = _cogl_xlib_renderer_get_data (renderer);
  glx_renderer = renderer->winsys;

  if (glx_tex_pixmap->left.pixmap_bound)
    glx_renderer->glXReleaseTexImage (xlib_renderer->xdpy,
                                      glx_tex_pixmap->glx_pixmap,
                                      GLX_FRONT_LEFT_EXT);
  if (glx_tex_pixmap->right.pixmap_bound)
    glx_renderer->glXReleaseTexImage (xlib_renderer->xdpy,
                                      glx_tex_pixmap->glx_pixmap,
                                      GLX_FRONT_RIGHT_EXT);

  /* FIXME - we need to trap errors and synchronize here because
   * of ordering issues between the XPixmap destruction and the
   * GLXPixmap destruction.
   *
   * If the X pixmap is destroyed, the GLX pixmap is destroyed as
   * well immediately, and thus, when Cogl calls glXDestroyPixmap()
   * it'll cause a BadDrawable error.
   *
   * this is technically a bug in the X server, which should not
   * destroy either pixmaps until the call to glXDestroyPixmap(); so
   * at some point we should revisit this code and remove the
   * trap+sync after verifying that the destruction is indeed safe.
   *
   * for reference, see:
   *   http://bugzilla.clutter-project.org/show_bug.cgi?id=2324
   */
  mtk_x11_error_trap_push (xlib_renderer->xdpy);
  glx_renderer->glXDestroyPixmap (xlib_renderer->xdpy,
                                  glx_tex_pixmap->glx_pixmap);
  XSync (xlib_renderer->xdpy, False);
  mtk_x11_error_trap_pop (xlib_renderer->xdpy);

  glx_tex_pixmap->glx_pixmap = None;
  glx_tex_pixmap->left.pixmap_bound = FALSE;
  glx_tex_pixmap->right.pixmap_bound = FALSE;
}

static void
_cogl_winsys_texture_pixmap_x11_free (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglTexturePixmapGLX *glx_tex_pixmap;

  if (!tex_pixmap->winsys)
    return;

  glx_tex_pixmap = tex_pixmap->winsys;

  free_glx_pixmap (cogl_texture_get_context (COGL_TEXTURE (tex_pixmap)),
                   glx_tex_pixmap);

  if (glx_tex_pixmap->left.glx_tex)
    g_object_unref (glx_tex_pixmap->left.glx_tex);

  if (glx_tex_pixmap->right.glx_tex)
    g_object_unref (glx_tex_pixmap->right.glx_tex);

  tex_pixmap->winsys = NULL;
  g_free (glx_tex_pixmap);
}

static gboolean
_cogl_winsys_texture_pixmap_x11_update (CoglTexturePixmapX11 *tex_pixmap,
                                        CoglTexturePixmapStereoMode stereo_mode,
                                        gboolean needs_mipmap)
{
  CoglTexture *tex = COGL_TEXTURE (tex_pixmap);
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglTexturePixmapGLX *glx_tex_pixmap = tex_pixmap->winsys;
  CoglPixmapTextureEyeGLX *texture_info;
  int buffer;
  CoglGLXRenderer *glx_renderer;

  if (stereo_mode == COGL_TEXTURE_PIXMAP_RIGHT)
    {
      texture_info = &glx_tex_pixmap->right;
      buffer = GLX_FRONT_RIGHT_EXT;
    }
  else
    {
      texture_info = &glx_tex_pixmap->left;
      buffer = GLX_FRONT_LEFT_EXT;
    }

  /* If we don't have a GLX pixmap then fallback */
  if (glx_tex_pixmap->glx_pixmap == None)
    return FALSE;

  glx_renderer = ctx->display->renderer->winsys;

  /* Lazily create a texture to hold the pixmap */
  if (texture_info->glx_tex == NULL)
    {
      CoglPixelFormat texture_format;
      GError *error = NULL;

      texture_format = (tex_pixmap->depth >= 32 ?
                        COGL_PIXEL_FORMAT_RGBA_8888_PRE :
                        COGL_PIXEL_FORMAT_RGB_888);

      texture_info->glx_tex =
        cogl_texture_2d_new_with_size (ctx,
                                       cogl_texture_get_width (tex),
                                       cogl_texture_get_height (tex));

      _cogl_texture_set_internal_format (tex, texture_format);

      if (cogl_texture_allocate (texture_info->glx_tex, &error))
        COGL_NOTE (TEXTURE_PIXMAP, "Created a texture 2d for %p", tex_pixmap);
      else
        {
          COGL_NOTE (TEXTURE_PIXMAP, "Falling back for %p because a "
                     "texture 2d could not be created: %s",
                     tex_pixmap, error->message);
          g_error_free (error);
          free_glx_pixmap (ctx, glx_tex_pixmap);
          return FALSE;
        }
    }

  if (needs_mipmap)
    {
      /* If we can't support mipmapping then temporarily fallback */
      if (!glx_tex_pixmap->can_mipmap)
        return FALSE;

      /* Recreate the GLXPixmap if it wasn't previously created with a
       * mipmap tree */
      if (!glx_tex_pixmap->has_mipmap_space)
        {
          free_glx_pixmap (ctx, glx_tex_pixmap);

          COGL_NOTE (TEXTURE_PIXMAP, "Recreating GLXPixmap with mipmap "
                     "support for %p", tex_pixmap);
          if (!try_create_glx_pixmap (ctx, tex_pixmap, TRUE))

            {
              /* If the pixmap failed then we'll permanently fallback
               * to using XImage. This shouldn't happen. */
              COGL_NOTE (TEXTURE_PIXMAP, "Falling back to XGetImage "
                         "updates for %p because creating the GLXPixmap "
                         "with mipmap support failed", tex_pixmap);

              if (texture_info->glx_tex)
                g_object_unref (texture_info->glx_tex);
              return FALSE;
            }

          glx_tex_pixmap->left.bind_tex_image_queued = TRUE;
          glx_tex_pixmap->right.bind_tex_image_queued = TRUE;
        }
    }

  if (texture_info->bind_tex_image_queued)
    {
      GLuint gl_handle, gl_target;
      CoglXlibRenderer *xlib_renderer =
        _cogl_xlib_renderer_get_data (ctx->display->renderer);

      cogl_texture_get_gl_texture (texture_info->glx_tex,
                                   &gl_handle, &gl_target);

      COGL_NOTE (TEXTURE_PIXMAP, "Rebinding GLXPixmap for %p", tex_pixmap);

      _cogl_bind_gl_texture_transient (gl_target, gl_handle);

      if (texture_info->pixmap_bound)
        glx_renderer->glXReleaseTexImage (xlib_renderer->xdpy,
                                          glx_tex_pixmap->glx_pixmap,
                                          buffer);

      glx_renderer->glXBindTexImage (xlib_renderer->xdpy,
                                     glx_tex_pixmap->glx_pixmap,
                                     buffer,
                                     NULL);

      /* According to the recommended usage in the spec for
       * GLX_EXT_texture_pixmap we should release the texture after
       * we've finished drawing with it and it is undefined what
       * happens if you render to a pixmap that is bound to a texture.
       * However that would require the texture backend to know when
       * Cogl has finished painting and it may be more expensive to
       * keep unbinding the texture. Leaving it bound appears to work
       * on Mesa and NVidia drivers and it is also what Compiz does so
       * it is probably ok */

      texture_info->bind_tex_image_queued = FALSE;
      texture_info->pixmap_bound = TRUE;

      _cogl_texture_2d_externally_modified (texture_info->glx_tex);
    }

  return TRUE;
}

static void
_cogl_winsys_texture_pixmap_x11_damage_notify (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglTexturePixmapGLX *glx_tex_pixmap = tex_pixmap->winsys;

  glx_tex_pixmap->left.bind_tex_image_queued = TRUE;
  glx_tex_pixmap->right.bind_tex_image_queued = TRUE;
}

static CoglTexture *
_cogl_winsys_texture_pixmap_x11_get_texture (CoglTexturePixmapX11 *tex_pixmap,
                                             CoglTexturePixmapStereoMode stereo_mode)
{
  CoglTexturePixmapGLX *glx_tex_pixmap = tex_pixmap->winsys;

  if (stereo_mode == COGL_TEXTURE_PIXMAP_RIGHT)
    return glx_tex_pixmap->right.glx_tex;
  else
    return glx_tex_pixmap->left.glx_tex;
}

void
cogl_context_glx_set_current_drawable (CoglContext *context,
                                       GLXDrawable  drawable)
{
  CoglContextGLX *glx_context = context->winsys;

  glx_context->current_drawable = drawable;
}

GLXDrawable
cogl_context_glx_get_current_drawable (CoglContext *context)
{
  CoglContextGLX *glx_context = context->winsys;

  return glx_context->current_drawable;
}

static CoglWinsysVtable _cogl_winsys_vtable =
  {
    .id = COGL_WINSYS_ID_GLX,
    .name = "GLX",
    .constraints = (COGL_RENDERER_CONSTRAINT_USES_X11 |
                    COGL_RENDERER_CONSTRAINT_USES_XLIB),

    .renderer_get_proc_address = _cogl_winsys_renderer_get_proc_address,
    .renderer_connect = _cogl_winsys_renderer_connect,
    .renderer_disconnect = _cogl_winsys_renderer_disconnect,
    .renderer_outputs_changed = _cogl_winsys_renderer_outputs_changed,
    .renderer_bind_api = _cogl_winsys_renderer_bind_api,
    .display_setup = _cogl_winsys_display_setup,
    .display_destroy = _cogl_winsys_display_destroy,
    .context_init = _cogl_winsys_context_init,
    .context_deinit = _cogl_winsys_context_deinit,

    /* X11 tfp support... */
    /* XXX: instead of having a rather monolithic winsys vtable we could
     * perhaps look for a way to separate these... */
    .texture_pixmap_x11_create =
      _cogl_winsys_texture_pixmap_x11_create,
    .texture_pixmap_x11_free =
      _cogl_winsys_texture_pixmap_x11_free,
    .texture_pixmap_x11_update =
      _cogl_winsys_texture_pixmap_x11_update,
    .texture_pixmap_x11_damage_notify =
      _cogl_winsys_texture_pixmap_x11_damage_notify,
    .texture_pixmap_x11_get_texture =
      _cogl_winsys_texture_pixmap_x11_get_texture,
  };

/* XXX: we use a function because no doubt someone will complain
 * about using c99 member initializers because they aren't portable
 * to windows. We want to avoid having to rigidly follow the real
 * order of members since some members are #ifdefd and we'd have
 * to mirror the #ifdefing to add padding etc. For any winsys that
 * can assume the platform has a sane compiler then we can just use
 * c99 initializers for insane platforms they can initialize
 * the members by name in a function.
 */
COGL_EXPORT const CoglWinsysVtable *
_cogl_winsys_glx_get_vtable (void)
{
  return &_cogl_winsys_vtable;
}
