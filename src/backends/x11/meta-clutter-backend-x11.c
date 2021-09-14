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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#include "config.h"

#include <glib-object.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-renderer.h"
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

typedef struct _MetaX11EventFilter MetaX11EventFilter;

struct _MetaX11EventFilter
{
  MetaX11FilterFunc func;
  gpointer data;
};

G_DEFINE_TYPE (MetaClutterBackendX11, meta_clutter_backend_x11,
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
static Display  *_foreign_dpy = NULL;

/* options */
static gboolean clutter_synchronise = FALSE;

/* X error trap */
static int TrappedErrorCode = 0;
static int (* old_error_handler) (Display *, XErrorEvent *);

static MetaX11FilterReturn
cogl_xlib_filter (XEvent       *xevent,
                  ClutterEvent *event,
                  gpointer      data)
{
  ClutterBackend *backend = data;
  MetaX11FilterReturn retval;
  CoglFilterReturn ret;

  ret = cogl_xlib_renderer_handle_event (backend->cogl_renderer, xevent);
  switch (ret)
    {
    case COGL_FILTER_REMOVE:
      retval = META_X11_FILTER_REMOVE;
      break;

    case COGL_FILTER_CONTINUE:
    default:
      retval = META_X11_FILTER_CONTINUE;
      break;
    }

  return retval;
}

static gboolean
meta_clutter_backend_x11_finish_init (ClutterBackend  *backend,
                                      GError         **error)
{
  MetaClutterBackendX11 *backend_x11 = META_CLUTTER_BACKEND_X11 (backend);
  Atom atoms[N_ATOM_NAMES];

  if (_foreign_dpy)
    backend_x11->xdisplay = _foreign_dpy;

  /* Only open connection if not already set by prior call to
   * clutter_x11_set_display()
   */
  if (backend_x11->xdisplay == NULL)
    {
      const char *display_name;

      display_name = g_getenv ("DISPLAY");
      if (display_name && *display_name != '\0')
	{
	  g_debug ("XOpenDisplay on '%s'", display_name);

	  backend_x11->xdisplay = XOpenDisplay (display_name);
          if (backend_x11->xdisplay == NULL)
            {
              g_set_error (error, CLUTTER_INIT_ERROR,
                           CLUTTER_INIT_ERROR_BACKEND,
                           "Unable to open display '%s'",
                           display_name);
              return FALSE;
            }
	}
      else
	{
	  g_set_error_literal (error, CLUTTER_INIT_ERROR,
                               CLUTTER_INIT_ERROR_BACKEND,
                               "Unable to open display. You have to set the "
                               "DISPLAY environment variable, or use the "
                               "--display command line argument");
	  return FALSE;
	}
    }

  g_assert (backend_x11->xdisplay != NULL);

  g_debug ("Getting the X screen");

  /* add event filter for Cogl events */
  meta_clutter_x11_add_filter (cogl_xlib_filter, backend);

  backend_x11->xscreen = DefaultScreenOfDisplay (backend_x11->xdisplay);
  backend_x11->xscreen_num = XScreenNumberOfScreen (backend_x11->xscreen);
  backend_x11->xscreen_width = WidthOfScreen (backend_x11->xscreen);
  backend_x11->xscreen_height = HeightOfScreen (backend_x11->xscreen);

  backend_x11->xwin_root = RootWindow (backend_x11->xdisplay,
                                       backend_x11->xscreen_num);

  if (clutter_synchronise)
    XSynchronize (backend_x11->xdisplay, True);

  XInternAtoms (backend_x11->xdisplay,
                (char **) atom_names, N_ATOM_NAMES,
                False, atoms);

  backend_x11->atom_NET_WM_PID = atoms[0];
  backend_x11->atom_NET_WM_PING = atoms[1];
  backend_x11->atom_NET_WM_STATE = atoms[2];
  backend_x11->atom_NET_WM_USER_TIME = atoms[3];
  backend_x11->atom_WM_PROTOCOLS = atoms[4];
  backend_x11->atom_WM_DELETE_WINDOW = atoms[5];
  backend_x11->atom_XEMBED = atoms[6];
  backend_x11->atom_XEMBED_INFO = atoms[7];
  backend_x11->atom_NET_WM_NAME = atoms[8];
  backend_x11->atom_UTF8_STRING = atoms[9];

  g_debug ("X Display '%s'[%p] opened (screen:%d, root:%u, dpi:%f)",
           g_getenv ("DISPLAY"),
           backend_x11->xdisplay,
           backend_x11->xscreen_num,
           (unsigned int) backend_x11->xwin_root,
           clutter_backend_get_resolution (backend));

  return TRUE;
}

static void
meta_clutter_backend_x11_finalize (GObject *gobject)
{
  MetaClutterBackendX11 *backend_x11 = META_CLUTTER_BACKEND_X11 (gobject);

  meta_clutter_x11_remove_filter (cogl_xlib_filter, gobject);

  XCloseDisplay (backend_x11->xdisplay);

  G_OBJECT_CLASS (meta_clutter_backend_x11_parent_class)->finalize (gobject);
}

static ClutterFeatureFlags
meta_clutter_backend_x11_get_features (ClutterBackend *backend)
{
  ClutterFeatureFlags flags = CLUTTER_FEATURE_STAGE_CURSOR;

  flags |=
    CLUTTER_BACKEND_CLASS (meta_clutter_backend_x11_parent_class)->get_features (backend);

  return flags;
}

static void
update_last_event_time (MetaClutterBackendX11 *backend_x11,
                        XEvent                *xevent)
{
  Time current_time = CurrentTime;
  Time last_time = backend_x11->last_event_time;

  switch (xevent->type)
    {
    case KeyPress:
    case KeyRelease:
      current_time = xevent->xkey.time;
      break;

    case ButtonPress:
    case ButtonRelease:
      current_time = xevent->xbutton.time;
      break;

    case MotionNotify:
      current_time = xevent->xmotion.time;
      break;

    case EnterNotify:
    case LeaveNotify:
      current_time = xevent->xcrossing.time;
      break;

    case PropertyNotify:
      current_time = xevent->xproperty.time;
      break;

    default:
      break;
    }

  /* only change the current event time if it's after the previous event
   * time, or if it is at least 30 seconds earlier - in case the system
   * clock was changed
   */
  if ((current_time != CurrentTime) &&
      (current_time > last_time || (last_time - current_time > (30 * 1000))))
    backend_x11->last_event_time = current_time;
}

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
      g_set_error_literal (error, CLUTTER_INIT_ERROR,
                           CLUTTER_INIT_ERROR_BACKEND,
                           internal_error != NULL
                           ? internal_error->message
                           : "Creation of a CoglDisplay failed");

      g_clear_error (&internal_error);

      return FALSE;
    }
}

static CoglDisplay *
meta_clutter_backend_x11_get_display (ClutterBackend  *backend,
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

  cogl_object_unref (onscreen_template);

  return display;
}

static CoglRenderer *
meta_clutter_backend_x11_get_renderer (ClutterBackend  *clutter_backend,
                                       GError         **error)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return meta_renderer_create_cogl_renderer (renderer);
}

static ClutterStageWindow *
meta_clutter_backend_x11_create_stage (ClutterBackend  *backend,
                                       ClutterStage    *wrapper,
                                       GError         **error)
{
  ClutterStageWindow *stage;
  GType stage_type;

  if (meta_is_wayland_compositor ())
    stage_type = META_TYPE_STAGE_X11_NESTED;
  else
    stage_type  = META_TYPE_STAGE_X11;

  stage = g_object_new (stage_type,
			"backend", backend,
			"wrapper", wrapper,
			NULL);
  return stage;
}

static gboolean
meta_clutter_backend_x11_process_event_filters (MetaClutterBackendX11 *backend_x11,
                                                gpointer               native,
                                                ClutterEvent          *event)
{
  XEvent *xevent = native;

  /* X11 filter functions have a higher priority */
  if (backend_x11->event_filters != NULL)
    {
      GSList *node = backend_x11->event_filters;

      while (node != NULL)
        {
          MetaX11EventFilter *filter = node->data;

          switch (filter->func (xevent, event, filter->data))
            {
            case META_X11_FILTER_CONTINUE:
              break;

            case META_X11_FILTER_TRANSLATE:
              return TRUE;

            case META_X11_FILTER_REMOVE:
              return FALSE;

            default:
              break;
            }

          node = node->next;
        }
    }

  return FALSE;
}

static gboolean
meta_clutter_backend_x11_translate_event (ClutterBackend *clutter_backend,
                                          gpointer        native,
                                          ClutterEvent   *event)
{
  MetaClutterBackendX11 *backend_x11 =
    META_CLUTTER_BACKEND_X11 (clutter_backend);
  MetaBackend *backend = meta_get_backend ();
  MetaStageX11 *stage_x11;
  ClutterSeat *seat;

  if (meta_clutter_backend_x11_process_event_filters (backend_x11,
                                                      native,
                                                      event))
    return TRUE;

  /* we update the event time only for events that can
   * actually reach Clutter's event queue
   */
  update_last_event_time (backend_x11, native);

  stage_x11 =
    META_STAGE_X11 (clutter_backend_get_stage_window (clutter_backend));
  if (meta_stage_x11_translate_event (stage_x11, native, event))
    return TRUE;

  seat = meta_backend_get_default_seat (backend);
  if (meta_seat_x11_translate_event (META_SEAT_X11 (seat), native, event))
    return TRUE;

  return FALSE;
}

static ClutterSeat *
meta_clutter_backend_x11_get_default_seat (ClutterBackend *clutter_backend)
{
  MetaBackend *backend = meta_get_backend ();

  return meta_backend_get_default_seat (backend);
}

static gboolean
meta_clutter_backend_x11_is_display_server (ClutterBackend *backend)
{
  return meta_is_wayland_compositor ();
}

static void
meta_clutter_backend_x11_init (MetaClutterBackendX11 *clutter_backend_x11)
{
  clutter_backend_x11->last_event_time = CurrentTime;
}

static void
meta_clutter_backend_x11_class_init (MetaClutterBackendX11Class *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterBackendClass *clutter_backend_class = CLUTTER_BACKEND_CLASS (klass);

  gobject_class->finalize = meta_clutter_backend_x11_finalize;

  clutter_backend_class->finish_init = meta_clutter_backend_x11_finish_init;
  clutter_backend_class->get_features = meta_clutter_backend_x11_get_features;

  clutter_backend_class->get_display = meta_clutter_backend_x11_get_display;
  clutter_backend_class->get_renderer = meta_clutter_backend_x11_get_renderer;
  clutter_backend_class->create_stage = meta_clutter_backend_x11_create_stage;
  clutter_backend_class->translate_event = meta_clutter_backend_x11_translate_event;
  clutter_backend_class->get_default_seat = meta_clutter_backend_x11_get_default_seat;
  clutter_backend_class->is_display_server = meta_clutter_backend_x11_is_display_server;
}

static int
error_handler (Display     *xdisplay,
               XErrorEvent *error)
{
  TrappedErrorCode = error->error_code;
  return 0;
}

void
meta_clutter_x11_trap_x_errors (void)
{
  TrappedErrorCode  = 0;
  old_error_handler = XSetErrorHandler (error_handler);
}

gint
meta_clutter_x11_untrap_x_errors (void)
{
  XSetErrorHandler (old_error_handler);

  return TrappedErrorCode;
}

Display *
meta_clutter_x11_get_default_display (void)
{
  ClutterBackend *backend = clutter_get_default_backend ();

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return NULL;
    }

  if (!META_IS_CLUTTER_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return NULL;
    }

  return META_CLUTTER_BACKEND_X11 (backend)->xdisplay;
}

void
meta_clutter_x11_set_display (Display *xdisplay)
{
  if (_clutter_context_is_initialized ())
    {
      g_warning ("%s() can only be used before calling clutter_init()",
                 G_STRFUNC);
      return;
    }

  _foreign_dpy= xdisplay;
}

int
meta_clutter_x11_get_default_screen (void)
{
 ClutterBackend *backend = clutter_get_default_backend ();

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return 0;
    }

  if (!META_IS_CLUTTER_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return 0;
    }

  return META_CLUTTER_BACKEND_X11 (backend)->xscreen_num;
}

Window
meta_clutter_x11_get_root_window (void)
{
 ClutterBackend *backend = clutter_get_default_backend ();

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return None;
    }

  if (!META_IS_CLUTTER_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return None;
    }

  return META_CLUTTER_BACKEND_X11 (backend)->xwin_root;
}

void
meta_clutter_x11_add_filter (MetaX11FilterFunc func,
                             gpointer             data)
{
  MetaX11EventFilter *filter;
  ClutterBackend *backend = clutter_get_default_backend ();
  MetaClutterBackendX11 *backend_x11;

  g_return_if_fail (func != NULL);

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return;
    }

  if (!META_IS_CLUTTER_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return;
    }

  backend_x11 = META_CLUTTER_BACKEND_X11 (backend);

  filter = g_new0 (MetaX11EventFilter, 1);
  filter->func = func;
  filter->data = data;

  backend_x11->event_filters =
    g_slist_append (backend_x11->event_filters, filter);

  return;
}

void
meta_clutter_x11_remove_filter (MetaX11FilterFunc func,
                                gpointer          data)
{
  GSList *tmp_list, *this;
  MetaX11EventFilter *filter;
  ClutterBackend *backend = clutter_get_default_backend ();
  MetaClutterBackendX11 *backend_x11;

  g_return_if_fail (func != NULL);

  if (backend == NULL)
    {
      g_critical ("The Clutter backend has not been initialised");
      return;
    }

  if (!META_IS_CLUTTER_BACKEND_X11 (backend))
    {
      g_critical ("The Clutter backend is not a X11 backend");
      return;
    }

  backend_x11 = META_CLUTTER_BACKEND_X11 (backend);

  tmp_list = backend_x11->event_filters;

  while (tmp_list)
    {
      filter   = tmp_list->data;
      this     =  tmp_list;
      tmp_list = tmp_list->next;

      if (filter->func == func && filter->data == data)
        {
          backend_x11->event_filters =
            g_slist_remove_link (backend_x11->event_filters, this);

          g_slist_free_1 (this);
          g_free (filter);

          return;
        }
    }
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
