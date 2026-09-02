/*
 * Copyright (C) 2026 Zhiyi Zhang for CodeWeavers
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

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/sync.h>

#include <glib-unix.h>
#include <glib.h>
#include <stdint.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "x11-test-client-utils.h"

#define MWM_HINTS_DECORATIONS (1L << 1)

typedef struct
{
  unsigned long flags;
  unsigned long functions;
  unsigned long decorations;
  long input_mode;
  unsigned long status;
} MotifWmHints;

typedef struct
{
  Display *xdisplay;
  Window window;
  XColor green;
} TestData;

static Atom atom_motif_wm_hints;
static Atom atom_test_done;

static void
sleep_ms (int ms)
{
  usleep ((useconds_t)ms * 1000);
}

static void
fill_window (Display      *xdisplay,
             Window        window,
             unsigned long pixel)
{
  GC gc;
  XWindowAttributes attrs;

  if (!XGetWindowAttributes (xdisplay, window, &attrs))
    return;

  gc = XCreateGC (xdisplay, window, 0, NULL);
  XSetForeground (xdisplay, gc, pixel);
  XFillRectangle (xdisplay, window, gc, 0, 0, attrs.width, attrs.height);
  XFreeGC (xdisplay, gc);
  XFlush (xdisplay);
}

static void
remove_mwm_decorations (Display *xdisplay,
                        Window   window)
{
  MotifWmHints hints = {0};

  hints.flags = MWM_HINTS_DECORATIONS;
  hints.decorations = 0;

  XChangeProperty (xdisplay, window,
                   atom_motif_wm_hints,
                   atom_motif_wm_hints,
                   32,
                   PropModeReplace,
                   (unsigned char *) &hints,
                   sizeof (hints) / sizeof (unsigned long));
  XFlush (xdisplay);
}

static gboolean
on_sigterm (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static gboolean
on_xevent (XEvent   *xevent,
           gpointer  user_data)
{
  TestData *data = user_data;

  if (xevent->type == Expose ||
      xevent->type == GraphicsExpose)
    fill_window (data->xdisplay, data->window, data->green.pixel);

  return G_SOURCE_CONTINUE;
}

int
main (void)
{
  g_autoptr (GMainLoop) loop = NULL;
  Display *xdisplay;
  int screen;
  Window root, window;
  XSetWindowAttributes attrs;
  XWindowChanges changes;
  Colormap cmap;
  XColor red, green, dummy;
  TestData data;
  GSource *source;
  long value;

  loop = g_main_loop_new (NULL, FALSE);

  xdisplay = XOpenDisplay (NULL);
  if (!xdisplay)
    {
      fprintf (stderr, "Failed to open display\n");
      return 1;
    }

  g_unix_signal_add (SIGTERM, on_sigterm, loop);

  /* If the bug reproduces, you don't see anything on the screen.
   * If the bug doesn't reproduces, you see a green rectangle on the screen. */

  atom_motif_wm_hints = XInternAtom (xdisplay, "_MOTIF_WM_HINTS", False);
  atom_test_done = XInternAtom (xdisplay, "_TEST_DONE", False);

  screen = DefaultScreen (xdisplay);
  root = RootWindow (xdisplay, screen);
  cmap = DefaultColormap (xdisplay, screen);
  XAllocNamedColor (xdisplay, cmap, "red", &red, &dummy);
  XAllocNamedColor (xdisplay, cmap, "green", &green, &dummy);

  attrs.background_pixmap = None;
  attrs.event_mask = StructureNotifyMask | ExposureMask | PropertyChangeMask;
  window = XCreateWindow (xdisplay, root, 100, 100, 100, 100, 0,
                          CopyFromParent, InputOutput,
                          CopyFromParent, CWBackPixmap | CWEventMask, &attrs);

  set_x11_window_title (xdisplay, window, "xwayland-allow-commits-test");

  remove_mwm_decorations (xdisplay, window);
  XMapWindow (xdisplay, window);
  fill_window (xdisplay, window, red.pixel);
  XSync (xdisplay, 0);

  /* Wait a bit to let WM finish handling events */
  sleep_ms (100);

  /* Change size */
  changes.x = 100;
  changes.y = 100;
  changes.width = 400;
  changes.height = 400;
  XReconfigureWMWindow (xdisplay, window, screen,
                        CWX | CWY | CWWidth | CWHeight, &changes);

  /* Make clutter actor destroyed */
  XWithdrawWindow (xdisplay, window, screen);

  /* Map window */
  XMapWindow (xdisplay, window);


  data.xdisplay = xdisplay;
  data.window = window;
  data.green = green;
  source = x11_event_source_new (xdisplay, on_xevent, &data);

  /* Paint and mark window as ready for ref testing. */
  fill_window (xdisplay, window, green.pixel);
  value = 1;
  XChangeProperty (xdisplay, window, atom_test_done, XA_CARDINAL, 32,
                   PropModeReplace, (unsigned char*) &value, 1);
  XFlush (xdisplay);

  g_main_loop_run (loop);

  g_source_destroy (source);
  g_source_unref (source);
  XDestroyWindow (xdisplay, window);
  XCloseDisplay (xdisplay);

  return EXIT_SUCCESS;
}
