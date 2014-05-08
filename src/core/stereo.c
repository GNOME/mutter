/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2014 Red Hat, Inc.
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
 */

/*
 * SECTION:stereo
 * @short_description: Keep track of whether we are a stereo compositor
 *
 * With GLX, we need to use a different GL context for stereo and
 * non-stereo support. Support for multiple GL contexts is unfinished
 * in Cogl and entirely lacking in Clutter, so it's by far easier
 * to just restart Mutter when we detect a stereo window.
 *
 * A property _MUTTER_ENABLE_STEREO is maintained on the root window
 * to know whether we should initialize clutter for stereo or not.
 * When the presence or absence of stereo windows mismatches the
 * stereo-enabled state for a sufficiently long period of time,
 * we restart Mutter.
 */

#include <config.h>

#include <clutter/x11/clutter-x11.h>
#include <gio/gunixinputstream.h>
#include <X11/Xatom.h>

#include <meta/main.h>
#include "ui.h"
#include <meta/util.h>
#include "display-private.h"
#include "stereo.h"
#include "util-private.h"

static guint stereo_switch_id = 0;
static gboolean stereo_enabled = FALSE;
/* -1 so the first time meta_stereo_set_have_stereo_windows() is called
 * we avoid the short-circuit and set up a timeout to restart
 * if necessary */
static gboolean stereo_have_windows = (gboolean)-1;
static gboolean stereo_restart = FALSE;

#define STEREO_ENABLE_WAIT 1000
#define STEREO_DISABLE_WAIT 5000

void
meta_stereo_init (void)
{
  Display *xdisplay;
  Window root;
  Atom atom_enable_stereo;
  Atom type;
  int format;
  unsigned long n_items, bytes_after;
  guchar *data;

  xdisplay = XOpenDisplay (NULL);
  if (xdisplay == NULL)
    meta_fatal ("Unable to open X display %s\n", XDisplayName (NULL));

  root = DefaultRootWindow (xdisplay);
  atom_enable_stereo = XInternAtom (xdisplay, "_MUTTER_ENABLE_STEREO", False);

  XGetWindowProperty (xdisplay, root, atom_enable_stereo,
                      0, 1, False, XA_INTEGER,
                      &type, &format, &n_items, &bytes_after, &data);
  if (type == XA_INTEGER)
    {
      if (format == 32 && n_items == 1 && bytes_after == 0)
        {
          stereo_enabled = *(long *)data;
        }
      else
        {
          meta_warning ("Bad value for _MUTTER_ENABLE_STEREO property\n");
        }

      XFree (data);
    }
  else if (type != None)
    {
      meta_warning ("Bad type for _MUTTER_ENABLE_STEREO property\n");
    }

  meta_verbose ("On startup, _MUTTER_ENABLE_STEREO=%s",
                stereo_enabled ? "yes" : "no");
  clutter_x11_set_use_stereo_stage (stereo_enabled);
  XCloseDisplay (xdisplay);
}

static gboolean
meta_stereo_switch (gpointer data)
{
  stereo_switch_id = 0;
  stereo_restart = TRUE;

  meta_restart (stereo_have_windows ?
                _("Enabling stereo...") :
                _("Disabling stereo..."));

  return FALSE;
}

void
meta_stereo_set_have_stereo_windows (gboolean have_windows)
{
  have_windows = have_windows != FALSE;

  if (!stereo_restart && have_windows != stereo_have_windows)
    {
      MetaDisplay *display = meta_get_display ();
      Display *xdisplay = meta_display_get_xdisplay (display);
      Window root = DefaultRootWindow (xdisplay);
      Atom atom_enable_stereo = XInternAtom (xdisplay, "_MUTTER_ENABLE_STEREO", False);
      long value;

      stereo_have_windows = have_windows;

      if (stereo_have_windows)
        meta_verbose ("Detected stereo windows\n");
      else
        meta_verbose ("No stereo windows detected\n");

      value = stereo_have_windows;
      XChangeProperty (xdisplay, root,
                       atom_enable_stereo, XA_INTEGER, 32,
                       PropModeReplace, (guchar *)&value, 1);

      if (stereo_switch_id != 0)
        {
          g_source_remove (stereo_switch_id);
          stereo_switch_id = 0;
        }

      if (stereo_have_windows != stereo_enabled)
        stereo_switch_id = g_timeout_add (stereo_have_windows ? STEREO_ENABLE_WAIT : STEREO_DISABLE_WAIT,
                                          meta_stereo_switch, NULL);
    }
}
