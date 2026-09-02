/*
 * Copyright (C) 2026 Red Hat Inc.
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

#include "x11-test-client-utils.h"

typedef struct _X11EventSource
{
  GSource base;
  GPollFD event_poll_fd;
  Display *xdisplay;
} X11EventSource;

static gboolean
x11_event_source_prepare (GSource *source,
                          int     *timeout)
{
  Display *xdisplay = ((X11EventSource*) source)->xdisplay;

  *timeout = -1;

  return XPending (xdisplay);
}

static gboolean
x11_event_source_dispatch (GSource     *source,
                           GSourceFunc  callback,
                           gpointer     user_data)
{
  X11EventSource *x11_source = ((X11EventSource *) source);
  Display *xdisplay = x11_source->xdisplay;
  XEvent xevent;

  XNextEvent (xdisplay, &xevent);
  return ((X11EventSourceCallback) callback) (&xevent, user_data);
}

static GSourceFuncs event_funcs = {
  .prepare = x11_event_source_prepare,
  .dispatch = x11_event_source_dispatch,
};

GSource *
x11_event_source_new (Display               *xdisplay,
                      X11EventSourceCallback callback,
                      gpointer               user_data)
{
  GSource *source;
  X11EventSource *x11_event_source;

  source = g_source_new (&event_funcs, sizeof (X11EventSource));
  x11_event_source = (X11EventSource *) source;
  x11_event_source->xdisplay = xdisplay;

  x11_event_source->event_poll_fd.fd = ConnectionNumber (xdisplay);
  x11_event_source->event_poll_fd.events = G_IO_IN;
  g_source_add_poll (source, &x11_event_source->event_poll_fd);

  g_source_set_callback (source, (GSourceFunc) callback, user_data, NULL);
  g_source_attach (source, NULL);

  return source;
}

static void
ensure_atom (Atom       *atom,
             Display    *xdisplay,
             const char *name)
{
  if (*atom)
    return;

  *atom = XInternAtom (xdisplay, name, False);
}

void
set_x11_window_title (Display    *xdisplay,
                      Window      window,
                      const char *title)
{
  static Atom atom_net_wm_name;
  static Atom atom_utf8_string;

  ensure_atom (&atom_net_wm_name, xdisplay, "_NET_WM_NAME");
  ensure_atom (&atom_utf8_string, xdisplay, "UTF8_STRING");

  XChangeProperty (xdisplay, window, atom_net_wm_name, atom_utf8_string, 8,
                   PropModeReplace, (unsigned char*) title, strlen (title));
  XFlush (xdisplay);
}
