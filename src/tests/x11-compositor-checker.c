/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2022 Alan Jenkins.
 * Copyright (C) 2023 Canonical Ltd.
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

#include "config.h"

#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

static int
has_compositor (Display *dpy,
                int      screen)
{
  char prop_name[20];

  snprintf (prop_name, 20, "_NET_WM_CM_S%d", screen);
  return XGetSelectionOwner (dpy, XInternAtom (dpy, prop_name, False)) != None;
}

int
main (void)
{
  Display *dpy = XOpenDisplay ("");

  if (has_compositor (dpy, XDefaultScreen (dpy)))
    {
      printf ("X11 Compositor is available for display %s.%d\n",
              DisplayString (dpy), XDefaultScreen (dpy));
      return 0;
    }

  printf ("NO X11 Compositor is available for display %s:%d\n",
          DisplayString (dpy), XDefaultScreen (dpy));

  return 1;
}
