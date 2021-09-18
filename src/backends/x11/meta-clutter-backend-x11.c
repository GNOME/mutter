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
#include "backends/meta-renderer.h"
#include "backends/x11/meta-backend-x11.h"
#include "backends/x11/meta-clutter-backend-x11.h"
#include "backends/x11/meta-keymap-x11.h"
#include "backends/x11/meta-seat-x11.h"
#include "backends/x11/meta-xkb-a11y-x11.h"
#include "backends/x11/nested/meta-stage-x11-nested.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter.h"
#include "cogl/cogl-xlib.h"
#include "core/bell.h"
#include "meta/meta-backend.h"

typedef struct _MetaClutterBackendX11Private
{
  MetaBackend *backend;
} MetaClutterBackendX11Private;

G_DEFINE_TYPE_WITH_PRIVATE (MetaClutterBackendX11, meta_clutter_backend_x11,
                            CLUTTER_TYPE_BACKEND)

/* atoms; remember to add the code that assigns the atom value to
 * the member of the MetaClutterBackendX11 structure if you add an
 * atom name here. do not change the order!
 */
static const gchar *atom_names[] = {
  "_NET_WM_PID",
  "_NET_WM_PING",
  "_NET_WM_STATE",
  "_NET_WM_USER_TIME",
  "WM_PROTOCOLS",
  "WM_DELETE_WINDOW",
  "_XEMBED",
  "_XEMBED_INFO",
  "_NET_WM_NAME",
  "UTF8_STRING",
};

#define N_ATOM_NAMES G_N_ELEMENTS (atom_names)

/* various flags corresponding to pre init setup calls */
static gboolean clutter_enable_stereo = FALSE;

static gboolean
check_onscreen_template (CoglRenderer         *renderer,
                         CoglOnscreenTemplate *onscreen_template,
                         gboolean              enable_stereo,
                         GError              **error)
{
  GError *internal_error = NULL;

  cogl_onscreen_template_set_stereo_enabled (onscreen_template,
					     clutter_enable_stereo);

  /* cogl_renderer_check_onscreen_template() is actually just a
   * shorthand for creating a CoglDisplay, and calling
   * cogl_display_setup() on it, then throwing the display away. If we
   * could just return that display, then it would be more efficient
   * not to use cogl_renderer_check_onscreen_template(). However, the
   * backend API requires that we return an CoglDisplay that has not
   * yet been setup, so one way or the other we'll have to discard the
   * first display and make a new fresh one.
   */
  if (cogl_renderer_check_onscreen_template (renderer, onscreen_template, &internal_error))
    {
      clutter_enable_stereo = enable_stereo;

      return TRUE;
    }
  else
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           internal_error != NULL
                           ? internal_error->message
                           : "Creation of a CoglDisplay failed");

      g_clear_error (&internal_error);

      return FALSE;
    }
}

static CoglDisplay *
meta_clutter_backend_x11_get_display (ClutterBackend  *clutter_backend,
                                      CoglRenderer    *renderer,
                                      CoglSwapChain   *swap_chain,
                                      GError         **error)
{
  CoglOnscreenTemplate *onscreen_template;
  CoglDisplay *display = NULL;
  gboolean res = FALSE;

  onscreen_template = cogl_onscreen_template_new (swap_chain);

  /* It's possible that the current renderer doesn't support transparency
   * or doesn't support stereo, so we try the different combinations.
   */
  if (clutter_enable_stereo)
    res = check_onscreen_template (renderer, onscreen_template,
                                   TRUE, error);

  if (!res)
    res = check_onscreen_template (renderer, onscreen_template,
                                   FALSE, error);

  if (res)
    display = cogl_display_new (renderer, onscreen_template);

  g_object_unref (onscreen_template);

  return display;
}

static CoglRenderer *
meta_clutter_backend_x11_get_renderer (ClutterBackend  *clutter_backend,
                                       GError         **error)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);
  MetaRenderer *renderer = meta_backend_get_renderer (priv->backend);

  return meta_renderer_create_cogl_renderer (renderer);
}

static ClutterStageWindow *
meta_clutter_backend_x11_create_stage (ClutterBackend  *clutter_backend,
                                       ClutterStage    *wrapper,
                                       GError         **error)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);
  ClutterStageWindow *stage;
  GType stage_type;

  if (meta_is_wayland_compositor ())
    stage_type = META_TYPE_STAGE_X11_NESTED;
  else
    stage_type = META_TYPE_STAGE_X11;

  stage = g_object_new (stage_type,
			"backend", priv->backend,
			"wrapper", wrapper,
			NULL);
  return stage;
}

static ClutterSeat *
meta_clutter_backend_x11_get_default_seat (ClutterBackend *clutter_backend)
{
  MetaClutterBackendX11 *clutter_backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaClutterBackendX11Private *priv =
    meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);

  return meta_backend_get_default_seat (priv->backend);
}

static gboolean
meta_clutter_backend_x11_is_display_server (ClutterBackend *clutter_backend)
{
  return meta_is_wayland_compositor ();
}

static void
meta_clutter_backend_x11_init (MetaClutterBackendX11 *clutter_backend_x11)
{
}

static void
meta_clutter_backend_x11_class_init (MetaClutterBackendX11Class *klass)
{
  ClutterBackendClass *clutter_backend_class = CLUTTER_BACKEND_CLASS (klass);

  clutter_backend_class->get_display = meta_clutter_backend_x11_get_display;
  clutter_backend_class->get_renderer = meta_clutter_backend_x11_get_renderer;
  clutter_backend_class->create_stage = meta_clutter_backend_x11_create_stage;
  clutter_backend_class->get_default_seat = meta_clutter_backend_x11_get_default_seat;
  clutter_backend_class->is_display_server = meta_clutter_backend_x11_is_display_server;
}

MetaClutterBackendX11 *
meta_clutter_backend_x11_new (MetaBackend *backend)
{
  MetaBackendX11 *backend_x11 = META_BACKEND_X11 (backend);
  Atom atoms[N_ATOM_NAMES];
  MetaClutterBackendX11 *clutter_backend_x11;
  MetaClutterBackendX11Private *priv;

  clutter_backend_x11 = g_object_new (META_TYPE_CLUTTER_BACKEND_X11, NULL);
  priv = meta_clutter_backend_x11_get_instance_private (clutter_backend_x11);
  priv->backend = backend;

  clutter_backend_x11->xdisplay = meta_backend_x11_get_xdisplay (backend_x11);

  XInternAtoms (clutter_backend_x11->xdisplay,
                (char **) atom_names, N_ATOM_NAMES,
                False, atoms);

  clutter_backend_x11->atom_NET_WM_PID = atoms[0];
  clutter_backend_x11->atom_NET_WM_PING = atoms[1];
  clutter_backend_x11->atom_NET_WM_STATE = atoms[2];
  clutter_backend_x11->atom_NET_WM_USER_TIME = atoms[3];
  clutter_backend_x11->atom_WM_PROTOCOLS = atoms[4];
  clutter_backend_x11->atom_WM_DELETE_WINDOW = atoms[5];
  clutter_backend_x11->atom_XEMBED = atoms[6];
  clutter_backend_x11->atom_XEMBED_INFO = atoms[7];
  clutter_backend_x11->atom_NET_WM_NAME = atoms[8];
  clutter_backend_x11->atom_UTF8_STRING = atoms[9];

  return clutter_backend_x11;
}

void
meta_clutter_x11_set_use_stereo_stage (gboolean use_stereo)
{
  if (_clutter_context_is_initialized ())
    {
      g_warning ("%s() can only be used before calling clutter_init()",
                 G_STRFUNC);
      return;
    }

  g_debug ("STEREO stages are %s",
           use_stereo ? "enabled" : "disabled");

  clutter_enable_stereo = use_stereo;
}

gboolean
meta_clutter_x11_get_use_stereo_stage (void)
{
  return clutter_enable_stereo;
}
