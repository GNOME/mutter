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

#include <glib-unix.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>

#include "x11-test-client-utils.h"

static gboolean
on_xevent (XEvent   *xevent,
           gpointer  user_data)
{
  return G_SOURCE_CONTINUE;
}

static gboolean
on_sigterm (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

int
main (void)
{
  g_autoptr (GMainLoop) loop = NULL;
  Display *xdisplay;
  Window window;
  int screen;
  XSizeHints *hints;
  GSource *source;

  xdisplay = XOpenDisplay (NULL);
  if (!xdisplay)
    {
      fprintf (stderr, "Failed to open display\n");
      return EXIT_FAILURE;
    }

  loop = g_main_loop_new (NULL, FALSE);
  g_unix_signal_add (SIGTERM, on_sigterm, loop);

  screen = DefaultScreen (xdisplay);

  window = XCreateSimpleWindow (xdisplay, RootWindow (xdisplay, screen),
                                50, 125, 200, 300, 1,
                                BlackPixel (xdisplay, screen),
                                WhitePixel (xdisplay, screen));

  hints = XAllocSizeHints ();
  hints->flags = USPosition;

  XSetWMNormalHints (xdisplay, window, hints);
  XFree (hints);

  set_x11_window_title (xdisplay, window, "user-position");

  XSelectInput (xdisplay, window, KeyPressMask);
  XMapWindow (xdisplay, window);

  source = x11_event_source_new (xdisplay, on_xevent, NULL);

  g_main_loop_run (loop);

  g_source_destroy (source);
  g_source_unref (source);
  XDestroyWindow (xdisplay, window);
  XCloseDisplay (xdisplay);

  return EXIT_SUCCESS;
}
