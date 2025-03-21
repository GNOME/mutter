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

#include "meta-frames-client.h"
#include "meta-window-tracker.h"

#include <gdk/x11/gdkx.h>
#include <glib-unix.h>
#include <gmodule.h>
#include <X11/extensions/Xfixes.h>

static gboolean should_monitor_color_scheme = TRUE;

typedef void (* InitFunc) (void);

static gboolean
should_load_libadwaita (void)
{
  g_auto(GStrv) desktops = NULL;
  const char *current_desktop;
  const char *platform_library;

  platform_library = g_getenv ("MUTTER_FRAMES_PLATFORM_LIBRARY");

  if (g_strcmp0 (platform_library, "none") == 0)
    return FALSE;

  if (g_strcmp0 (platform_library, "adwaita") == 0)
    return TRUE;

  current_desktop = g_getenv ("XDG_CURRENT_DESKTOP");
  if (current_desktop != NULL)
    desktops = g_strsplit (current_desktop, ":", -1);

  return desktops && g_strv_contains ((const char * const *) desktops, "GNOME");
}

static void
load_libadwaita (void)
{
  GModule *libadwaita;
  InitFunc adw_init;

  libadwaita = g_module_open ("libadwaita-1.so.0", G_MODULE_BIND_LAZY);
  if (!libadwaita)
    return;

  if (!g_module_symbol (libadwaita, "adw_init", (gpointer *) &adw_init))
    return;

  should_monitor_color_scheme = FALSE;
  adw_init ();
}

static gboolean
on_sigterm (gpointer user_data)
{
  exit (0);

  return G_SOURCE_REMOVE;
}

gboolean
meta_frames_client_should_monitor_color_scheme (void)
{
  return should_monitor_color_scheme;
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

  if (should_load_libadwaita ())
    load_libadwaita ();

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  xdisplay = gdk_x11_display_get_xdisplay (display);
  G_GNUC_END_IGNORE_DEPRECATIONS
  XFixesSetClientDisconnectMode (xdisplay,
                                 XFixesClientDisconnectFlagTerminate);

  window_tracker = meta_window_tracker_new (display);

  loop = g_main_loop_new (NULL, FALSE);
  g_unix_signal_add (SIGTERM, on_sigterm, NULL);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  return 0;
}
