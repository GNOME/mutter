/*
 * Copyright (C) 2017 Red Hat
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
 */

#include "config.h"

#include "x11/window-x11.h"
#include "x11/window-x11-private.h"
#include "wayland/meta-window-xwayland.h"
#include "wayland/meta-wayland.h"

#include <X11/extensions/Xrandr.h>

#include "meta/meta-x11-errors.h"

enum
{
  PROP_0,

  PROP_XWAYLAND_MAY_GRAB_KEYBOARD,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

struct _MetaWindowXwayland
{
  MetaWindowX11 parent;

  gboolean xwayland_may_grab_keyboard;
};

struct _MetaWindowXwaylandClass
{
  MetaWindowX11Class parent_class;
};

G_DEFINE_TYPE (MetaWindowXwayland, meta_window_xwayland, META_TYPE_WINDOW_X11)

static void
meta_window_xwayland_init (MetaWindowXwayland *window_xwayland)
{
}

/* Get resolution from xrandr for window's monitor, also see comment below. */
static gboolean
meta_window_xwayland_get_randr_monitor_resolution (MetaWindow *window,
                                                   int        *width,
                                                   int        *height)
{
  MetaRectangle monitor_rect;
  gboolean success = FALSE;
  Display *xdisplay = window->display->x11_display->xdisplay;
  XRRScreenResources *resources;
  XRROutputInfo *output;
  XRRCrtcInfo *crtc;
  int i;

  if (!window->monitor)
    {
      g_warning ("MetaWindow does not have a monitor");
      return FALSE;
    }

  meta_display_get_monitor_geometry (window->display,
                                     window->monitor->number,
                                     &monitor_rect);

  resources = XRRGetScreenResourcesCurrent (xdisplay,
                                            DefaultRootWindow (xdisplay));
  if (!resources)
    {
      g_warning ("XRRGetScreenResourcesCurrent failed");
      return FALSE;
    }

  for (i = 0; !success && i < resources->noutput; i++)
    {
      output = XRRGetOutputInfo (xdisplay, resources, resources->outputs[i]);
      if (!output)
        {
          g_warning ("XRRGetOutputInfo failed");
          continue;
        }

      if (output->connection == RR_Disconnected || output->crtc == None)
        goto free_output;

      crtc = XRRGetCrtcInfo (xdisplay, resources, output->crtc);
      if (!crtc)
        {
          g_warning ("XRRGetCrtcInfo failed");
          goto free_output;
        }

      if (monitor_rect.x == crtc->x && monitor_rect.y == crtc->y)
        {
          *width  = crtc->width;
          *height = crtc->height;
          success = TRUE;
        }

      XRRFreeCrtcInfo (crtc);
free_output:
      XRRFreeOutputInfo (output);
    }

  XRRFreeScreenResources (resources);

  if (!success)
    g_warning ("Randr output matching window monitor not found");

  return success;
}

/* Is this window likely to be the window of a (fullscreen) game? */
static gboolean
meta_window_xwayland_likely_is_game (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  XClassHint class_hint;
  Status success;

  /* most games / gaming libs set a full set of hints including setting
   * _NET_WM_BYPASS_COMPOSITOR to _NET_WM_BYPASS_COMPOSITOR_HINT_ON, so
   * we check this first
   */
  if (window->bypass_compositor == _NET_WM_BYPASS_COMPOSITOR_HINT_ON)
    return TRUE;

  /* Some other games / gaming libs (e.g. OGRE) set as little hints as possible,
   * these do not even set the WM_CLASS hints, which is somewhat unusual.
   */
  meta_x11_error_trap_push (x11_display);
  success = XGetClassHint (x11_display->xdisplay, window->xwindow, &class_hint);
  meta_x11_error_trap_pop (x11_display);

  if (success)
    {
      XFree (class_hint.res_name);
      XFree (class_hint.res_class);
    }

  /* If the WM_CLASS hints were *not* set it may very well be a game. */
  return !success;
}

/* This is a workaround for X11 games which use xrandr to change the resolution
 * in combination with NET_WM_STATE_FULLSCREEN when going fullscreen.
 *
 * Newer versions of Xwayland support the xrandr part of this by supporting
 * "fake" xrandr resolution changes in combination with using WPviewport to
 * scale the app's window (at the fake resolution) to fill the entire monitor.
 *
 * Apps using xrandr in combination with NET_WM_STATE_FULLSCREEN expect the
 * fullscreen window to have the size of the (fake) xrandr resolution since
 * when running on regular Xorg the resolution will actually be changed and
 * after that going fullscreen through NET_WM_STATE_FULLSCREEN will size
 * the window to be equal to the new resolution.
 *
 * We need to emulate this behavior for these games to work correctly, so
 * when Xwayland is used, we query the Window's monitor fake xrandr
 * resolution and "fullscreen" to that size.
 */
static void
meta_window_xwayland_adjust_fullscreen_monitor_rect (MetaWindow *window,
                                                     MetaRectangle *monitor_rect)
{
  int width, height;

  if (!meta_window_xwayland_likely_is_game (window))
    return;

  if (!meta_window_xwayland_get_randr_monitor_resolution (window, &width, &height))
    return;

  monitor_rect->width = width;
  monitor_rect->height = height;
}

static void
meta_window_xwayland_force_restore_shortcuts (MetaWindow         *window,
                                              ClutterInputDevice *source)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();

  meta_wayland_compositor_restore_shortcuts (compositor, source);
}

static gboolean
meta_window_xwayland_shortcuts_inhibited (MetaWindow         *window,
                                          ClutterInputDevice *source)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();

  return meta_wayland_compositor_is_shortcuts_inhibited (compositor, source);
}

static void
meta_window_xwayland_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaWindowXwayland *window = META_WINDOW_XWAYLAND (object);

  switch (prop_id)
    {
    case PROP_XWAYLAND_MAY_GRAB_KEYBOARD:
      g_value_set_boolean (value, window->xwayland_may_grab_keyboard);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_xwayland_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaWindowXwayland *window = META_WINDOW_XWAYLAND (object);

  switch (prop_id)
    {
    case PROP_XWAYLAND_MAY_GRAB_KEYBOARD:
      window->xwayland_may_grab_keyboard = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_xwayland_class_init (MetaWindowXwaylandClass *klass)
{
  MetaWindowClass *window_class = META_WINDOW_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  window_class->adjust_fullscreen_monitor_rect = meta_window_xwayland_adjust_fullscreen_monitor_rect;
  window_class->force_restore_shortcuts = meta_window_xwayland_force_restore_shortcuts;
  window_class->shortcuts_inhibited = meta_window_xwayland_shortcuts_inhibited;

  gobject_class->get_property = meta_window_xwayland_get_property;
  gobject_class->set_property = meta_window_xwayland_set_property;

  obj_props[PROP_XWAYLAND_MAY_GRAB_KEYBOARD] =
    g_param_spec_boolean ("xwayland-may-grab-keyboard",
                          "Xwayland may use keyboard grabs",
                          "Whether the client may use Xwayland keyboard grabs on this window",
                          FALSE,
                          G_PARAM_READWRITE);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}
