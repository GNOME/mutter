/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-renderer-view.h"
#include "backends/meta-renderer.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-clutter-backend-x11.h"
#include "backends/x11/meta-renderer-x11.h"
#include "cogl/cogl-xlib.h"
#include "cogl/cogl.h"
#include "core/boxes-private.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

#ifdef HAVE_EGL
#include "cogl/winsys/cogl-winsys-egl-x11-private.h"
#endif
#ifdef HAVE_GLX
#include "cogl/winsys/cogl-winsys-glx-private.h"
#endif

G_DEFINE_TYPE (MetaRendererX11, meta_renderer_x11, META_TYPE_RENDERER)

static const CoglWinsysVtable *
get_x11_cogl_winsys_vtable (CoglRenderer *renderer)
{
#ifdef HAVE_EGL_PLATFORM_XLIB
  if (meta_is_wayland_compositor ())
    return _cogl_winsys_egl_xlib_get_vtable ();
#endif

  switch (renderer->driver)
    {
    case COGL_DRIVER_GLES2:
#ifdef HAVE_EGL_PLATFORM_XLIB
      return _cogl_winsys_egl_xlib_get_vtable ();
#else
      break;
#endif
    case COGL_DRIVER_GL3:
#ifdef HAVE_GLX
      return _cogl_winsys_glx_get_vtable ();
#else
      break;
#endif
    case COGL_DRIVER_ANY:
    case COGL_DRIVER_NOP:
      break;
    }
  g_assert_not_reached ();
  return NULL;
}

static CoglRenderer *
meta_renderer_x11_create_cogl_renderer (MetaRenderer *renderer)
{
  MetaBackend *backend = meta_renderer_get_backend (renderer);
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  Display *xdisplay = meta_backend_x11_get_xdisplay (backend_x11);
  CoglRenderer *cogl_renderer;

  cogl_renderer = cogl_renderer_new ();
  cogl_renderer_set_custom_winsys (cogl_renderer, get_x11_cogl_winsys_vtable,
                                   NULL);
  cogl_xlib_renderer_set_foreign_display (cogl_renderer, xdisplay);
  cogl_xlib_renderer_request_reset_on_video_memory_purge (cogl_renderer, TRUE);

  return cogl_renderer;
}

static void
meta_renderer_x11_init (MetaRendererX11 *renderer_x11)
{
}

static void
meta_renderer_x11_class_init (MetaRendererX11Class *klass)
{
  MetaRendererClass *renderer_class = META_RENDERER_CLASS (klass);

  renderer_class->create_cogl_renderer = meta_renderer_x11_create_cogl_renderer;
}
