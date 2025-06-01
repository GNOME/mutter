/*
 * Copyright (C) 2022 Red Hat Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "meta-window-tracker.h"

#include "meta-frame.h"
#include "meta-frames-client.h"

#include <gdesktop-enums.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>

struct _MetaWindowTracker
{
  GObject parent_instance;

  GSettings *interface_settings;

  GdkDisplay *display;
  GHashTable *frames;
  GHashTable *client_windows;
  int xinput_opcode;
};

enum {
  PROP_0,
  PROP_DISPLAY,
  N_PROPS
};

static GParamSpec *props[N_PROPS] = { 0, };

G_DEFINE_TYPE (MetaWindowTracker, meta_window_tracker, G_TYPE_OBJECT)

static void
meta_window_tracker_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  MetaWindowTracker *window_tracker = META_WINDOW_TRACKER (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      window_tracker->display = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_window_tracker_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  MetaWindowTracker *window_tracker = META_WINDOW_TRACKER (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, window_tracker->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
update_color_scheme (MetaWindowTracker *window_tracker)
{
  GDesktopColorScheme color_scheme;
  gboolean is_dark;

  g_assert (window_tracker->interface_settings != NULL);

  color_scheme = g_settings_get_enum (window_tracker->interface_settings,
                                      "color-scheme");
  is_dark = color_scheme == G_DESKTOP_COLOR_SCHEME_PREFER_DARK;

  g_object_set (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", is_dark,
                NULL);
}

static void
on_color_scheme_changed_cb (GSettings         *interface_settings,
                            GParamSpec        *pspec,
                            MetaWindowTracker *window_tracker)
{
  update_color_scheme (window_tracker);
}

static void
set_up_frame (MetaWindowTracker *window_tracker,
              Window             xwindow)
{
  GdkDisplay *display = window_tracker->display;
  Display *xdisplay;
  GdkSurface *surface;
  Window xframe;
  unsigned long data[1];
  GtkWidget *frame;

  frame = g_hash_table_lookup (window_tracker->client_windows,
                               GUINT_TO_POINTER (xwindow));
  if (frame)
    return;

  /* Double check it's not a request for a frame of our own. */
  if (g_hash_table_contains (window_tracker->frames,
                             GUINT_TO_POINTER (xwindow)))
    return;

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS

  /* Create a frame window */
  frame = meta_frame_new (xwindow);
  surface = gtk_native_get_surface (GTK_NATIVE (frame));
  xframe = gdk_x11_surface_get_xid (surface);

  xdisplay = gdk_x11_display_get_xdisplay (display);
  gdk_x11_display_error_trap_push (display);

  XAddToSaveSet (xdisplay, xwindow);

  data[0] = xwindow;
  XChangeProperty (xdisplay,
                   xframe,
                   gdk_x11_get_xatom_by_name_for_display (display, "_MUTTER_FRAME_FOR"),
                   XA_WINDOW,
                   32,
                   PropModeReplace,
                   (guchar *) data, 1);

  if (gdk_x11_display_error_trap_pop (display))
    {
      gtk_window_destroy (GTK_WINDOW (frame));
      return;
    }

  G_GNUC_END_IGNORE_DEPRECATIONS

  g_hash_table_insert (window_tracker->frames,
                       GUINT_TO_POINTER (xframe), frame);
  g_hash_table_insert (window_tracker->client_windows,
                       GUINT_TO_POINTER (xwindow), frame);
  gtk_widget_set_visible (frame, TRUE);
}

static void
listen_set_up_frame (MetaWindowTracker *window_tracker,
                     Window             xwindow)
{
  GdkDisplay *display = window_tracker->display;
  Display *xdisplay;
  int format;
  Atom type;
  unsigned long nitems, bytes_after;
  unsigned char *data;

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS

  xdisplay = gdk_x11_display_get_xdisplay (display);
  gdk_x11_display_error_trap_push (display);

  XSelectInput (xdisplay, xwindow,
                PropertyChangeMask | StructureNotifyMask);

  XGetWindowProperty (xdisplay,
                      xwindow,
                      gdk_x11_get_xatom_by_name_for_display (display,
                                                             "_MUTTER_NEEDS_FRAME"),
                      0, 1,
                      False, XA_CARDINAL,
                      &type, &format,
                      &nitems, &bytes_after,
                      (unsigned char **) &data);

  if (gdk_x11_display_error_trap_pop (display))
    return;

  G_GNUC_END_IGNORE_DEPRECATIONS

  if (nitems > 0 && data[0])
    set_up_frame (window_tracker, xwindow);

  XFree (data);
}

static void
remove_frame (MetaWindowTracker *window_tracker,
              Window             xwindow)
{
  GdkDisplay *display = window_tracker->display;
  Display *xdisplay;
  GtkWidget *frame;
  GdkSurface *surface;
  Window xframe;

  frame = g_hash_table_lookup (window_tracker->client_windows,
                               GUINT_TO_POINTER (xwindow));
  if (!frame)
    return;

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS

  xdisplay = gdk_x11_display_get_xdisplay (display);

  surface = gtk_native_get_surface (GTK_NATIVE (frame));
  xframe = gdk_x11_surface_get_xid (surface);

  gdk_x11_display_error_trap_push (display);
  XRemoveFromSaveSet (xdisplay, xwindow);
  gdk_x11_display_error_trap_pop_ignored (display);

  G_GNUC_END_IGNORE_DEPRECATIONS

  g_hash_table_remove (window_tracker->client_windows,
                       GUINT_TO_POINTER (xwindow));
  g_hash_table_remove (window_tracker->frames,
                       GUINT_TO_POINTER (xframe));
}

static gboolean
on_xevent (GdkDisplay *display,
           XEvent     *xevent,
           gpointer    user_data)
{
  Window xroot;
  Window xwindow = xevent->xany.window;
  MetaWindowTracker *window_tracker = user_data;
  GtkWidget *frame;

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS

  xroot = gdk_x11_display_get_xrootwindow (display);

  if (xevent->type == CreateNotify &&
      xevent->xcreatewindow.parent == xroot &&
      !xevent->xcreatewindow.override_redirect &&
      !g_hash_table_contains (window_tracker->frames,
                              GUINT_TO_POINTER (xevent->xcreatewindow.window)))
    {
      xwindow = xevent->xcreatewindow.window;
      listen_set_up_frame (window_tracker, xwindow);
    }
  else if (xevent->type == ConfigureNotify &&
           xevent->xconfigure.event == xroot &&
           xevent->xconfigure.window != xroot &&
           !g_hash_table_contains (window_tracker->frames,
                                   GUINT_TO_POINTER (xevent->xconfigure.window)))
    {
      gboolean has_frame;

      xwindow = xevent->xconfigure.window;
      has_frame =
        g_hash_table_contains (window_tracker->client_windows,
                               GUINT_TO_POINTER (xwindow));

      if (!xevent->xconfigure.override_redirect && !has_frame)
        listen_set_up_frame (window_tracker, xwindow);
      else if (xevent->xconfigure.override_redirect && has_frame)
        remove_frame (window_tracker, xwindow);
    }
  else if (xevent->type == DestroyNotify)
    {
      xwindow = xevent->xdestroywindow.window;
      remove_frame (window_tracker, xwindow);
    }
  else if (xevent->type == PropertyNotify &&
           xevent->xproperty.atom ==
           gdk_x11_get_xatom_by_name_for_display (display, "_MUTTER_NEEDS_FRAME"))
    {
      if (xevent->xproperty.state == PropertyNewValue)
        set_up_frame (window_tracker, xwindow);
      else if (xevent->xproperty.state == PropertyDelete)
        remove_frame (window_tracker, xwindow);
    }
  else if (xevent->type == PropertyNotify)
    {
      frame = g_hash_table_lookup (window_tracker->frames,
                                   GUINT_TO_POINTER (xwindow));

      if (!frame)
        {
          frame = g_hash_table_lookup (window_tracker->client_windows,
                                       GUINT_TO_POINTER (xwindow));
        }

      if (frame)
        meta_frame_handle_xevent (META_FRAME (frame), xwindow, xevent);
    }
  else if (xevent->type == GenericEvent &&
           xevent->xcookie.extension == window_tracker->xinput_opcode)
    {
      Display *xdisplay = gdk_x11_display_get_xdisplay (display);
      XIEvent *xi_event;

      xi_event = (XIEvent *) xevent->xcookie.data;

      if (xi_event->evtype == XI_Leave)
        {
          XILeaveEvent *crossing = (XILeaveEvent *) xi_event;

          xwindow = crossing->event;
          frame = g_hash_table_lookup (window_tracker->frames,
                                       GUINT_TO_POINTER (xwindow));

          /* When crossing from the frame to the client
           * window, we may need to restore the cursor to
           * its default.
           */
          if (frame && crossing->detail == XINotifyInferior)
            {
              gdk_x11_display_error_trap_push (display);
              XIUndefineCursor (xdisplay, crossing->deviceid, xwindow);
              gdk_x11_display_error_trap_pop_ignored (display);
            }
        }
    }
  G_GNUC_END_IGNORE_DEPRECATIONS

  return GDK_EVENT_PROPAGATE;
}

static gboolean
query_xi_extension (MetaWindowTracker *window_tracker,
                    Display           *xdisplay)
{
  int major = 2, minor = 3;
  int unused;

  if (XQueryExtension (xdisplay,
                       "XInputExtension",
                       &window_tracker->xinput_opcode,
                       &unused,
                       &unused))
    {
      if (XIQueryVersion (xdisplay, &major, &minor) == Success)
        return TRUE;
    }

  return FALSE;
}

static void
meta_window_tracker_constructed (GObject *object)
{
  MetaWindowTracker *window_tracker = META_WINDOW_TRACKER (object);
  GdkDisplay *display = window_tracker->display;
  Display *xdisplay;
  Window xroot;
  Window *windows, ignored1, ignored2;
  unsigned int i, n_windows;

  G_OBJECT_CLASS (meta_window_tracker_parent_class)->constructed (object);

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS

  xdisplay = gdk_x11_display_get_xdisplay (display);
  xroot = gdk_x11_display_get_xrootwindow (display);

  query_xi_extension (window_tracker, xdisplay);

  XSelectInput (xdisplay, xroot,
                KeyPressMask |
                PropertyChangeMask |
                StructureNotifyMask |
                SubstructureNotifyMask);

  g_signal_connect (display, "xevent",
                    G_CALLBACK (on_xevent), object);

  gdk_x11_display_error_trap_push (display);

  XQueryTree (xdisplay,
              xroot,
              &ignored1, &ignored2,
              &windows, &n_windows);

  if (gdk_x11_display_error_trap_pop (display))
    {
      g_warning ("Could not query existing windows");
      return;
    }

  for (i = 0; i < n_windows; i++)
    {
      XWindowAttributes attrs;

      gdk_x11_display_error_trap_push (display);

      XGetWindowAttributes (xdisplay,
                            windows[i],
                            &attrs);

      if (gdk_x11_display_error_trap_pop (display))
        continue;

      if (attrs.override_redirect)
        continue;

      listen_set_up_frame (window_tracker, windows[i]);
    }

  G_GNUC_END_IGNORE_DEPRECATIONS

  XFree (windows);
}

static void
meta_window_tracker_finalize (GObject *object)
{
  MetaWindowTracker *window_tracker = META_WINDOW_TRACKER (object);

  g_clear_object (&window_tracker->interface_settings);
  g_clear_pointer (&window_tracker->frames,
                   g_hash_table_unref);
  g_clear_pointer (&window_tracker->client_windows,
                   g_hash_table_unref);

  G_OBJECT_CLASS (meta_window_tracker_parent_class)->finalize (object);
}

static void
meta_window_tracker_class_init (MetaWindowTrackerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_window_tracker_set_property;
  object_class->get_property = meta_window_tracker_get_property;
  object_class->constructed = meta_window_tracker_constructed;
  object_class->finalize = meta_window_tracker_finalize;

  props[PROP_DISPLAY] = g_param_spec_object ("display", NULL, NULL,
                                             GDK_TYPE_DISPLAY,
                                             G_PARAM_READWRITE |
                                             G_PARAM_CONSTRUCT_ONLY |
                                             G_PARAM_STATIC_NAME |
                                             G_PARAM_STATIC_NICK |
                                             G_PARAM_STATIC_BLURB);

  g_object_class_install_properties (object_class,
                                     G_N_ELEMENTS (props),
                                     props);
}

static void
meta_window_tracker_init (MetaWindowTracker *window_tracker)
{
  if (meta_frames_client_should_monitor_color_scheme ())
    {
      window_tracker->interface_settings = g_settings_new ("org.gnome.desktop.interface");
      g_signal_connect (window_tracker->interface_settings,
                        "changed::color-scheme",
                        G_CALLBACK (on_color_scheme_changed_cb),
                        window_tracker);
      update_color_scheme (window_tracker);
    }

  window_tracker->frames =
    g_hash_table_new_full (NULL, NULL, NULL,
                           (GDestroyNotify) gtk_window_destroy);
  window_tracker->client_windows = g_hash_table_new (NULL, NULL);
}

MetaWindowTracker *
meta_window_tracker_new (GdkDisplay *display)
{
  return g_object_new (META_TYPE_WINDOW_TRACKER,
                       "display", display,
                       NULL);
}
