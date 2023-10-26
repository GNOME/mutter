/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011,2013 Intel Corporation.
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
 *   Neil Roberts <neil@linux.intel.com>
 */

#include "config.h"

#include <X11/Xlib.h>

#include "cogl/cogl-xlib-renderer-private.h"
#include "cogl/cogl-xlib-renderer.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-onscreen-private.h"
#include "cogl/cogl-display-private.h"
#include "cogl/cogl-renderer-private.h"
#include "cogl/winsys/cogl-texture-pixmap-x11-private.h"
#include "cogl/cogl-texture-2d-private.h"
#include "cogl/driver/gl/cogl-texture-2d-gl-private.h"
#include "cogl/cogl-texture-2d.h"
#include "cogl/cogl-poll-private.h"
#include "cogl/winsys/cogl-onscreen-egl.h"
#include "cogl/winsys/cogl-onscreen-xlib.h"
#include "cogl/winsys/cogl-winsys-egl-x11-private.h"
#include "cogl/winsys/cogl-winsys-egl-private.h"

static const CoglWinsysEGLVtable _cogl_winsys_egl_vtable;

typedef struct _CoglDisplayXlib
{
  Window dummy_xwin;
} CoglDisplayXlib;

#ifdef EGL_KHR_image_pixmap
typedef struct _CoglTexturePixmapEGL
{
  EGLImageKHR image;
  CoglTexture *texture;
  gboolean bind_tex_image_queued;
} CoglTexturePixmapEGL;
#endif

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
      if (cogl_onscreen_xlib_is_for_window (onscreen, (Window) xid))
        return onscreen;
    }

  return NULL;
}

static void
notify_resize (CoglContext *context,
               Window drawable,
               int width,
               int height)
{
  CoglOnscreen *onscreen;

  onscreen = find_onscreen_for_xid (context, drawable);
  if (!onscreen)
    return;

  cogl_onscreen_xlib_resize (onscreen, width, height);
}

static CoglFilterReturn
event_filter_cb (XEvent *xevent, void *data)
{
  CoglContext *context = data;

  if (xevent->type == ConfigureNotify)
    {
      notify_resize (context,
                     xevent->xconfigure.window,
                     xevent->xconfigure.width,
                     xevent->xconfigure.height);
    }
  else if (xevent->type == Expose)
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
    }

  return COGL_FILTER_CONTINUE;
}

XVisualInfo *
cogl_display_xlib_get_visual_info (CoglDisplay *display,
                                   EGLConfig    egl_config)
{
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (display->renderer);
  CoglRendererEGL *egl_renderer = display->renderer->winsys;
  XVisualInfo visinfo_template;
  int template_mask = 0;
  XVisualInfo *visinfo = NULL;
  int visinfos_count;
  EGLint visualid, red_size, green_size, blue_size, alpha_size;

  eglGetConfigAttrib (egl_renderer->edpy, egl_config,
                      EGL_NATIVE_VISUAL_ID, &visualid);

  if (visualid != 0)
    {
      visinfo_template.visualid = visualid;
      template_mask |= VisualIDMask;
    }
  else
    {
      /* some EGL drivers don't implement the EGL_NATIVE_VISUAL_ID
       * attribute, so attempt to find the closest match. */

      eglGetConfigAttrib (egl_renderer->edpy, egl_config,
                          EGL_RED_SIZE, &red_size);
      eglGetConfigAttrib (egl_renderer->edpy, egl_config,
                          EGL_GREEN_SIZE, &green_size);
      eglGetConfigAttrib (egl_renderer->edpy, egl_config,
                          EGL_BLUE_SIZE, &blue_size);
      eglGetConfigAttrib (egl_renderer->edpy, egl_config,
                          EGL_ALPHA_SIZE, &alpha_size);

      visinfo_template.depth = red_size + green_size + blue_size + alpha_size;
      template_mask |= VisualDepthMask;

      visinfo_template.screen = DefaultScreen (xlib_renderer->xdpy);
      template_mask |= VisualScreenMask;
    }

  visinfo = XGetVisualInfo (xlib_renderer->xdpy,
                            template_mask,
                            &visinfo_template,
                            &visinfos_count);

  return visinfo;
}

static void
_cogl_winsys_renderer_disconnect (CoglRenderer *renderer)
{
  CoglRendererEGL *egl_renderer = renderer->winsys;

  _cogl_xlib_renderer_disconnect (renderer);

  eglTerminate (egl_renderer->edpy);

  g_free (egl_renderer);
}

static EGLDisplay
_cogl_winsys_egl_get_display (void *native)
{
  EGLDisplay dpy = NULL;
  const char *client_exts = eglQueryString (NULL, EGL_EXTENSIONS);

  if (g_strstr_len (client_exts, -1, "EGL_KHR_platform_base"))
    {
      PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
	(void *) eglGetProcAddress ("eglGetPlatformDisplay");

      if (get_platform_display)
	dpy = get_platform_display (EGL_PLATFORM_X11_KHR, native, NULL);

      if (dpy)
	return dpy;
    }

  if (g_strstr_len (client_exts, -1, "EGL_EXT_platform_base"))
    {
      PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
	(void *) eglGetProcAddress ("eglGetPlatformDisplayEXT");

      if (get_platform_display)
	dpy = get_platform_display (EGL_PLATFORM_X11_KHR, native, NULL);

      if (dpy)
	return dpy;
    }

  return eglGetDisplay ((EGLNativeDisplayType) native);
}

static gboolean
_cogl_winsys_renderer_connect (CoglRenderer *renderer,
                               GError **error)
{
  CoglRendererEGL *egl_renderer;
  CoglXlibRenderer *xlib_renderer;

  renderer->winsys = g_new0 (CoglRendererEGL, 1);
  egl_renderer = renderer->winsys;
  xlib_renderer = _cogl_xlib_renderer_get_data (renderer);

  egl_renderer->platform_vtable = &_cogl_winsys_egl_vtable;
  egl_renderer->sync = EGL_NO_SYNC_KHR;

  if (!_cogl_xlib_renderer_connect (renderer, error))
    goto error;

  egl_renderer->edpy = _cogl_winsys_egl_get_display (xlib_renderer->xdpy);

  if (!_cogl_winsys_egl_renderer_connect_common (renderer, error))
    goto error;

  return TRUE;

error:
  _cogl_winsys_renderer_disconnect (renderer);
  return FALSE;
}

static int
_cogl_winsys_egl_add_config_attributes (CoglDisplay                 *display,
                                        const CoglFramebufferConfig *config,
                                        EGLint                      *attributes)
{
  int i = 0;

  attributes[i++] = EGL_SURFACE_TYPE;
  attributes[i++] = EGL_WINDOW_BIT;

  return i;
}

static gboolean
_cogl_winsys_egl_choose_config (CoglDisplay *display,
                                EGLint *attributes,
                                EGLConfig *out_config,
                                GError **error)
{
  CoglRenderer *renderer = display->renderer;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  EGLint config_count = 0;
  EGLBoolean status;

  status = eglChooseConfig (egl_renderer->edpy,
                            attributes,
                            out_config, 1,
                            &config_count);
  if (status != EGL_TRUE || config_count == 0)
    {
      g_set_error (error, COGL_WINSYS_ERROR,
                   COGL_WINSYS_ERROR_CREATE_CONTEXT,
                   "No compatible EGL configs found");
      return FALSE;
    }

  return TRUE;
}

static gboolean
_cogl_winsys_egl_display_setup (CoglDisplay *display,
                                GError **error)
{
  CoglDisplayEGL *egl_display = display->winsys;
  CoglDisplayXlib *xlib_display;

  xlib_display = g_new0 (CoglDisplayXlib, 1);
  egl_display->platform = xlib_display;

  return TRUE;
}

static void
_cogl_winsys_egl_display_destroy (CoglDisplay *display)
{
  CoglDisplayEGL *egl_display = display->winsys;

  g_free (egl_display->platform);
}

static gboolean
_cogl_winsys_egl_context_init (CoglContext *context,
                               GError **error)
{
  cogl_xlib_renderer_add_filter (context->display->renderer,
                                 event_filter_cb,
                                 context);

  /* We'll manually handle queueing dirty events in response to
   * Expose events from X */
  COGL_FLAGS_SET (context->private_features,
                  COGL_PRIVATE_FEATURE_DIRTY_EVENTS,
                  TRUE);

  return TRUE;
}

static void
_cogl_winsys_egl_context_deinit (CoglContext *context)
{
  cogl_xlib_renderer_remove_filter (context->display->renderer,
                                    event_filter_cb,
                                    context);
}

static gboolean
_cogl_winsys_egl_context_created (CoglDisplay *display,
                                  GError **error)
{
  CoglRenderer *renderer = display->renderer;
  CoglDisplayEGL *egl_display = display->winsys;
  CoglRendererEGL *egl_renderer = renderer->winsys;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);
  CoglDisplayXlib *xlib_display = egl_display->platform;
  XVisualInfo *xvisinfo;
  XSetWindowAttributes attrs;
  const char *error_message;

  xvisinfo = cogl_display_xlib_get_visual_info (display,
                                                egl_display->egl_config);
  if (xvisinfo == NULL)
    {
      error_message = "Unable to find suitable X visual";
      goto fail;
    }

  attrs.override_redirect = True;
  attrs.colormap = XCreateColormap (xlib_renderer->xdpy,
                                    DefaultRootWindow (xlib_renderer->xdpy),
                                    xvisinfo->visual,
                                    AllocNone);
  attrs.border_pixel = 0;

  if ((egl_renderer->private_features &
       COGL_EGL_WINSYS_FEATURE_SURFACELESS_CONTEXT) == 0)
    {
      xlib_display->dummy_xwin =
        XCreateWindow (xlib_renderer->xdpy,
                       DefaultRootWindow (xlib_renderer->xdpy),
                       -100, -100, 1, 1,
                       0,
                       xvisinfo->depth,
                       CopyFromParent,
                       xvisinfo->visual,
                       CWOverrideRedirect |
                       CWColormap |
                       CWBorderPixel,
                       &attrs);

      egl_display->dummy_surface =
        eglCreateWindowSurface (egl_renderer->edpy,
                                egl_display->egl_config,
                                (EGLNativeWindowType) xlib_display->dummy_xwin,
                                NULL);

      if (egl_display->dummy_surface == EGL_NO_SURFACE)
        {
          error_message = "Unable to create an EGL surface";
          XFree (xvisinfo);
          goto fail;
        }
    }

  xlib_renderer->xvisinfo = xvisinfo;

  if (!_cogl_winsys_egl_make_current (display,
                                      egl_display->dummy_surface,
                                      egl_display->dummy_surface,
                                      egl_display->egl_context))
    {
      if (egl_display->dummy_surface == EGL_NO_SURFACE)
        error_message = "Unable to eglMakeCurrent with no surface";
      else
        error_message = "Unable to eglMakeCurrent with dummy surface";
      goto fail;
    }

  return TRUE;

fail:
  g_set_error (error, COGL_WINSYS_ERROR,
               COGL_WINSYS_ERROR_CREATE_CONTEXT,
               "%s", error_message);
  return FALSE;
}

static void
_cogl_winsys_egl_cleanup_context (CoglDisplay *display)
{
  CoglDisplayEGL *egl_display = display->winsys;
  CoglDisplayXlib *xlib_display = egl_display->platform;
  CoglRenderer *renderer = display->renderer;
  CoglXlibRenderer *xlib_renderer =
    _cogl_xlib_renderer_get_data (renderer);
  CoglRendererEGL *egl_renderer = renderer->winsys;

  if (egl_display->dummy_surface != EGL_NO_SURFACE)
    {
      eglDestroySurface (egl_renderer->edpy, egl_display->dummy_surface);
      egl_display->dummy_surface = EGL_NO_SURFACE;
    }

  if (xlib_display->dummy_xwin)
    {
      XDestroyWindow (xlib_renderer->xdpy, xlib_display->dummy_xwin);
      xlib_display->dummy_xwin = None;
    }
}

#ifdef EGL_KHR_image_pixmap

static gboolean
_cogl_winsys_texture_pixmap_x11_create (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglTexture *tex = COGL_TEXTURE (tex_pixmap);
  CoglContext *ctx = cogl_texture_get_context (tex);
  CoglTexturePixmapEGL *egl_tex_pixmap;
  EGLint attribs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
  CoglPixelFormat texture_format;
  CoglRendererEGL *egl_renderer;

  egl_renderer = ctx->display->renderer->winsys;

  if (!(egl_renderer->private_features &
        COGL_EGL_WINSYS_FEATURE_EGL_IMAGE_FROM_X11_PIXMAP) ||
      !_cogl_has_private_feature
      (ctx, COGL_PRIVATE_FEATURE_TEXTURE_2D_FROM_EGL_IMAGE))
    {
      tex_pixmap->winsys = NULL;
      return FALSE;
    }

  egl_tex_pixmap = g_new0 (CoglTexturePixmapEGL, 1);

  egl_tex_pixmap->image =
    _cogl_egl_create_image (ctx,
                            EGL_NATIVE_PIXMAP_KHR,
                            (EGLClientBuffer)tex_pixmap->pixmap,
                            attribs);
  if (egl_tex_pixmap->image == EGL_NO_IMAGE_KHR)
    {
      g_free (egl_tex_pixmap);
      return FALSE;
    }

  texture_format = (tex_pixmap->depth >= 32 ?
                    COGL_PIXEL_FORMAT_RGBA_8888_PRE :
                    COGL_PIXEL_FORMAT_RGB_888);

  egl_tex_pixmap->texture =
    cogl_egl_texture_2d_new_from_image (ctx,
                                        cogl_texture_get_width (tex),
                                        cogl_texture_get_height (tex),
                                        texture_format,
                                        egl_tex_pixmap->image,
                                        COGL_EGL_IMAGE_FLAG_NONE,
                                        NULL);

  /* The image is initially bound as part of the creation */
  egl_tex_pixmap->bind_tex_image_queued = FALSE;

  tex_pixmap->winsys = egl_tex_pixmap;

  return TRUE;
}

static void
_cogl_winsys_texture_pixmap_x11_free (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglTexturePixmapEGL *egl_tex_pixmap;
  CoglContext *ctx;

  ctx = cogl_texture_get_context (COGL_TEXTURE (tex_pixmap));

  if (!tex_pixmap->winsys)
    return;

  egl_tex_pixmap = tex_pixmap->winsys;

  if (egl_tex_pixmap->texture)
    g_object_unref (egl_tex_pixmap->texture);

  if (egl_tex_pixmap->image != EGL_NO_IMAGE_KHR)
    _cogl_egl_destroy_image (ctx, egl_tex_pixmap->image);

  tex_pixmap->winsys = NULL;
  g_free (egl_tex_pixmap);
}

static gboolean
_cogl_winsys_texture_pixmap_x11_update (CoglTexturePixmapX11 *tex_pixmap,
                                        CoglTexturePixmapStereoMode stereo_mode,
                                        gboolean needs_mipmap)
{
  CoglTexturePixmapEGL *egl_tex_pixmap = tex_pixmap->winsys;
  CoglTexture2D *tex_2d;
  GError *error = NULL;

  if (needs_mipmap)
    return FALSE;

  if (egl_tex_pixmap->bind_tex_image_queued)
    {
      COGL_NOTE (TEXTURE_PIXMAP, "Rebinding GLXPixmap for %p", tex_pixmap);

      tex_2d = COGL_TEXTURE_2D (egl_tex_pixmap->texture);

      if (cogl_texture_2d_gl_bind_egl_image (tex_2d,
                                             egl_tex_pixmap->image,
                                             &error))
        {
          egl_tex_pixmap->bind_tex_image_queued = FALSE;
        }
      else
        {
          g_warning ("Failed to rebind EGLImage to CoglTexture2D: %s",
                     error->message);
          g_error_free (error);
        }
    }

  return TRUE;
}

static void
_cogl_winsys_texture_pixmap_x11_damage_notify (CoglTexturePixmapX11 *tex_pixmap)
{
  CoglTexturePixmapEGL *egl_tex_pixmap = tex_pixmap->winsys;

  egl_tex_pixmap->bind_tex_image_queued = TRUE;
}

static CoglTexture *
_cogl_winsys_texture_pixmap_x11_get_texture (CoglTexturePixmapX11 *tex_pixmap,
                                             CoglTexturePixmapStereoMode stereo_mode)
{
  CoglTexturePixmapEGL *egl_tex_pixmap = tex_pixmap->winsys;

  return egl_tex_pixmap->texture;
}

#endif /* EGL_KHR_image_pixmap */

static const CoglWinsysEGLVtable
_cogl_winsys_egl_vtable =
  {
    .add_config_attributes = _cogl_winsys_egl_add_config_attributes,
    .choose_config = _cogl_winsys_egl_choose_config,
    .display_setup = _cogl_winsys_egl_display_setup,
    .display_destroy = _cogl_winsys_egl_display_destroy,
    .context_created = _cogl_winsys_egl_context_created,
    .cleanup_context = _cogl_winsys_egl_cleanup_context,
    .context_init = _cogl_winsys_egl_context_init,
    .context_deinit = _cogl_winsys_egl_context_deinit,
  };

COGL_EXPORT const CoglWinsysVtable *
_cogl_winsys_egl_xlib_get_vtable (void)
{
  static gboolean vtable_inited = FALSE;
  static CoglWinsysVtable vtable;

  if (!vtable_inited)
    {
      /* The EGL_X11 winsys is a subclass of the EGL winsys so we
         start by copying its vtable */

      vtable = *_cogl_winsys_egl_get_vtable ();

      vtable.id = COGL_WINSYS_ID_EGL_XLIB;
      vtable.name = "EGL_XLIB";
      vtable.constraints |= (COGL_RENDERER_CONSTRAINT_USES_X11 |
                             COGL_RENDERER_CONSTRAINT_USES_XLIB);

      vtable.renderer_connect = _cogl_winsys_renderer_connect;
      vtable.renderer_disconnect = _cogl_winsys_renderer_disconnect;

#ifdef EGL_KHR_image_pixmap
      /* X11 tfp support... */
      /* XXX: instead of having a rather monolithic winsys vtable we could
       * perhaps look for a way to separate these... */
      vtable.texture_pixmap_x11_create =
        _cogl_winsys_texture_pixmap_x11_create;
      vtable.texture_pixmap_x11_free =
        _cogl_winsys_texture_pixmap_x11_free;
      vtable.texture_pixmap_x11_update =
        _cogl_winsys_texture_pixmap_x11_update;
      vtable.texture_pixmap_x11_damage_notify =
        _cogl_winsys_texture_pixmap_x11_damage_notify;
      vtable.texture_pixmap_x11_get_texture =
        _cogl_winsys_texture_pixmap_x11_get_texture;
#endif /* EGL_KHR_image_pixmap) */

      vtable_inited = TRUE;
    }

  return &vtable;
}
