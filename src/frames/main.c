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

#include <gdk/x11/gdkx.h>
#include <glib-unix.h>
#include <X11/extensions/Xfixes.h>

static gboolean
on_sigterm (gpointer user_data)
{
  exit (0);

  return G_SOURCE_REMOVE;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaWindowTracker) window_tracker = NULL;
  GdkDisplay *display;
  GMainLoop *loop;
  Display *xdisplay;

  g_setenv ("GSK_RENDERER", "cairo", TRUE);

  /* We do know the desired GDK backend, don't let
   * anyone tell us otherwise.
   */
  g_unsetenv ("GDK_BACKEND");

  gdk_set_allowed_backends ("x11");

  g_set_prgname ("mutter-x11-frames");

  gtk_init ();

  display = gdk_display_get_default ();

  xdisplay = gdk_x11_display_get_xdisplay (display);
  XFixesSetClientDisconnectMode (xdisplay,
                                 XFixesClientDisconnectFlagTerminate);

  window_tracker = meta_window_tracker_new (display);

  loop = g_main_loop_new (NULL, FALSE);
  g_unix_signal_add (SIGTERM, on_sigterm, NULL);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  return 0;
}
