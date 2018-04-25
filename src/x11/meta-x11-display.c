/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2002, 2003, 2004 Red Hat, Inc.
 * Copyright (C) 2003, 2004 Rob Adams
 * Copyright (C) 2004-2006 Elijah Newren
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

/**
 * SECTION:display
 * @title: MetaX11Display
 * @short_description: Mutter X display handler
 *
 * The X11 display is represented as a #MetaX11Display struct.
 */

#include "config.h"

#include "core/display-private.h"
#include "x11/meta-x11-display-private.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xatom.h>
#ifdef HAVE_RANDR
#include <X11/extensions/Xrandr.h>
#endif
#include <X11/extensions/shape.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>


#include "core/util-private.h"
#include "meta/errors.h"

#ifdef HAVE_WAYLAND
#include "wayland/meta-xwayland-private.h"
#endif

G_DEFINE_TYPE (MetaX11Display, meta_x11_display, G_TYPE_OBJECT)

static char *get_screen_name (Display *xdisplay,
                              int      number);

static void
meta_x11_display_dispose (GObject *object)
{
  MetaX11Display *x11_display = META_X11_DISPLAY (object);

  if (x11_display->xroot != None)
    {
      meta_error_trap_push (x11_display);
      XSelectInput (x11_display->xdisplay, x11_display->xroot, 0);
      if (meta_error_trap_pop_with_return (x11_display) != Success)
        meta_warning ("Could not release screen %d on display \"%s\"\n",
                      meta_ui_get_screen_number (), x11_display->name);

      x11_display->xroot = None;
    }


  if (x11_display->xdisplay)
    {
      XFlush (x11_display->xdisplay);
      XCloseDisplay (x11_display->xdisplay);
      x11_display->xdisplay = NULL;
    }

  g_free (x11_display->name);
  x11_display->name = NULL;

  g_free (x11_display->screen_name);
  x11_display->screen_name = NULL;

  G_OBJECT_CLASS (meta_x11_display_parent_class)->dispose (object);
}

static void
meta_x11_display_class_init (MetaX11DisplayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_x11_display_dispose;
}

static void
meta_x11_display_init (MetaX11Display *x11_display)
{
}

static void
query_xsync_extension (MetaX11Display *x11_display)
{
  int major, minor;

  x11_display->have_xsync = FALSE;

  x11_display->xsync_error_base = 0;
  x11_display->xsync_event_base = 0;

  /* I don't think we really have to fill these in */
  major = SYNC_MAJOR_VERSION;
  minor = SYNC_MINOR_VERSION;

  if (!XSyncQueryExtension (x11_display->xdisplay,
                            &x11_display->xsync_event_base,
                            &x11_display->xsync_error_base) ||
      !XSyncInitialize (x11_display->xdisplay,
                        &major, &minor))
    {
      x11_display->xsync_error_base = 0;
      x11_display->xsync_event_base = 0;
    }
  else
    {
      x11_display->have_xsync = TRUE;
      XSyncSetPriority (x11_display->xdisplay, None, 10);
    }

  meta_verbose ("Attempted to init Xsync, found version %d.%d error base %d event base %d\n",
                major, minor,
                x11_display->xsync_error_base,
                x11_display->xsync_event_base);
}

static void
query_xshape_extension (MetaX11Display *x11_display)
{
  x11_display->have_shape = FALSE;

  x11_display->shape_error_base = 0;
  x11_display->shape_event_base = 0;

  if (!XShapeQueryExtension (x11_display->xdisplay,
                             &x11_display->shape_event_base,
                             &x11_display->shape_error_base))
    {
      x11_display->shape_error_base = 0;
      x11_display->shape_event_base = 0;
    }
  else
    x11_display->have_shape = TRUE;

  meta_verbose ("Attempted to init Shape, found error base %d event base %d\n",
                x11_display->shape_error_base,
                x11_display->shape_event_base);
}

static void
query_xcomposite_extension (MetaX11Display *x11_display)
{
  x11_display->have_composite = FALSE;

  x11_display->composite_error_base = 0;
  x11_display->composite_event_base = 0;

  if (!XCompositeQueryExtension (x11_display->xdisplay,
                                 &x11_display->composite_event_base,
                                 &x11_display->composite_error_base))
    {
      x11_display->composite_error_base = 0;
      x11_display->composite_event_base = 0;
    }
  else
    {
      x11_display->composite_major_version = 0;
      x11_display->composite_minor_version = 0;
      if (XCompositeQueryVersion (x11_display->xdisplay,
                                  &x11_display->composite_major_version,
                                  &x11_display->composite_minor_version))
        {
          x11_display->have_composite = TRUE;
        }
      else
        {
          x11_display->composite_major_version = 0;
          x11_display->composite_minor_version = 0;
        }
    }

  meta_verbose ("Attempted to init Composite, found error base %d event base %d "
                "extn ver %d %d\n",
                x11_display->composite_error_base,
                x11_display->composite_event_base,
                x11_display->composite_major_version,
                x11_display->composite_minor_version);
}

static void
query_xdamage_extension (MetaX11Display *x11_display)
{
  x11_display->have_damage = FALSE;

  x11_display->damage_error_base = 0;
  x11_display->damage_event_base = 0;

  if (!XDamageQueryExtension (x11_display->xdisplay,
                              &x11_display->damage_event_base,
                              &x11_display->damage_error_base))
    {
      x11_display->damage_error_base = 0;
      x11_display->damage_event_base = 0;
    }
  else
    x11_display->have_damage = TRUE;

  meta_verbose ("Attempted to init Damage, found error base %d event base %d\n",
                x11_display->damage_error_base,
                x11_display->damage_event_base);
}

static void
query_xfixes_extension (MetaX11Display *x11_display)
{
  x11_display->xfixes_error_base = 0;
  x11_display->xfixes_event_base = 0;

  if (XFixesQueryExtension (x11_display->xdisplay,
                            &x11_display->xfixes_event_base,
                            &x11_display->xfixes_error_base))
    {
      int xfixes_major, xfixes_minor;

      XFixesQueryVersion (x11_display->xdisplay, &xfixes_major, &xfixes_minor);

      if (xfixes_major * 100 + xfixes_minor < 500)
        meta_fatal ("Mutter requires XFixes 5.0");
    }
  else
    {
      meta_fatal ("Mutter requires XFixes 5.0");
    }

  meta_verbose ("Attempted to init XFixes, found error base %d event base %d\n",
                x11_display->xfixes_error_base,
                x11_display->xfixes_event_base);
}

static void
query_xi_extension (MetaX11Display *x11_display)
{
  int major = 2, minor = 3;
  gboolean has_xi = FALSE;

  if (XQueryExtension (x11_display->xdisplay,
                       "XInputExtension",
                       &x11_display->xinput_opcode,
                       &x11_display->xinput_error_base,
                       &x11_display->xinput_event_base))
    {
        if (XIQueryVersion (x11_display->xdisplay, &major, &minor) == Success)
        {
          int version = (major * 10) + minor;
          if (version >= 22)
            has_xi = TRUE;

#ifdef HAVE_XI23
          if (version >= 23)
            x11_display->have_xinput_23 = TRUE;
#endif /* HAVE_XI23 */
        }
    }

  if (!has_xi)
    meta_fatal ("X server doesn't have the XInput extension, version 2.2 or newer\n");
}

/**
 * meta_x11_display_new:
 *
 * Opens a new X11 display, sets it up, initialises all the X extensions
 * we will need.
 *
 * Returns: #MetaX11Display if the display was opened successfully,
 * and %NULL otherwise-- that is, if the display doesn't exist or
 * it already has a window manager, and sets the error appropriately.
 */
MetaX11Display *
meta_x11_display_new (MetaDisplay *display, GError **error)
{
  MetaX11Display *x11_display;
  Display *xdisplay;
  Screen *xscreen;
  Window xroot;
  int i, number;

  /* A list of all atom names, so that we can intern them in one go. */
  const char *atom_names[] = {
#define item(x) #x,
#include "x11/atomnames.h"
#undef item
  };
  Atom atoms[G_N_ELEMENTS(atom_names)];

  meta_verbose ("Opening display '%s'\n", XDisplayName (NULL));

  xdisplay = meta_ui_get_display ();

  if (xdisplay == NULL)
    {
      meta_warning (_("Failed to open X Window System display “%s”\n"),
                    XDisplayName (NULL));

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to open X11 display");

      return NULL;
    }

#ifdef HAVE_WAYLAND
  if (meta_is_wayland_compositor ())
    meta_xwayland_complete_init ();
#endif

  if (meta_is_syncing ())
    XSynchronize (xdisplay, True);

  number = meta_ui_get_screen_number ();

  xroot = RootWindow (xdisplay, number);

  /* FVWM checks for None here, I don't know if this
   * ever actually happens
   */
  if (xroot == None)
    {

      meta_warning (_("Screen %d on display “%s” is invalid\n"),
                    number, XDisplayName (NULL));

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to open default X11 screen");

      XFlush (xdisplay);
      XCloseDisplay (xdisplay);

      return NULL;
    }

  xscreen = ScreenOfDisplay (xdisplay, number);

  x11_display = g_object_new (META_TYPE_X11_DISPLAY, NULL);
  x11_display->display = display;

  /* here we use XDisplayName which is what the user
   * probably put in, vs. DisplayString(display) which is
   * canonicalized by XOpenDisplay()
   */
  x11_display->xdisplay = xdisplay;
  x11_display->xroot = xroot;

  x11_display->name = g_strdup (XDisplayName (NULL));
  x11_display->screen_name = get_screen_name (xdisplay, number);
  x11_display->default_xvisual = DefaultVisualOfScreen (xscreen);
  x11_display->default_depth = DefaultDepthOfScreen (xscreen);

  meta_verbose ("Creating %d atoms\n", (int) G_N_ELEMENTS (atom_names));
  XInternAtoms (xdisplay, (char **)atom_names, G_N_ELEMENTS (atom_names),
                False, atoms);

  i = 0;
#define item(x) x11_display->atom_##x = atoms[i++];
#include "x11/atomnames.h"
#undef item

  query_xsync_extension (x11_display);
  query_xshape_extension (x11_display);
  query_xcomposite_extension (x11_display);
  query_xdamage_extension (x11_display);
  query_xfixes_extension (x11_display);
  query_xi_extension (x11_display);

  return x11_display;
}

int
meta_x11_display_get_screen_number (MetaX11Display *x11_display)
{
  return meta_ui_get_screen_number ();
}

/**
 * meta_x11_display_get_xdisplay: (skip)
 * @x11_display: a #MetaX11Display
 *
 */
Display *
meta_x11_display_get_xdisplay (MetaX11Display *x11_display)
{
  return x11_display->xdisplay;
}

/**
 * meta_x11_display_get_xroot: (skip)
 * @x11_display: A #MetaX11Display
 *
 */
Window
meta_x11_display_get_xroot (MetaX11Display *x11_display)
{
  return x11_display->xroot;
}

/**
 * meta_x11_display_get_xinput_opcode: (skip)
 * @x11_display: a #MetaX11Display
 *
 */
int
meta_x11_display_get_xinput_opcode (MetaX11Display *x11_display)
{
  return x11_display->xinput_opcode;
}

int
meta_x11_display_get_damage_event_base (MetaX11Display *x11_display)
{
  return x11_display->damage_event_base;
}

int
meta_x11_display_get_shape_event_base (MetaX11Display *x11_display)
{
  return x11_display->shape_event_base;
}

gboolean
meta_x11_display_has_shape (MetaX11Display *x11_display)
{
  return META_X11_DISPLAY_HAS_SHAPE (x11_display);
}

Window
meta_x11_display_create_offscreen_window (MetaX11Display *x11_display,
                                          Window          parent,
                                          long            valuemask)
{
  XSetWindowAttributes attrs;

  /* we want to be override redirect because sometimes we
   * create a window on a screen we aren't managing.
   * (but on a display we are managing at least one screen for)
   */
  attrs.override_redirect = True;
  attrs.event_mask = valuemask;

  return XCreateWindow (x11_display->xdisplay,
                        parent,
                        -100, -100, 1, 1,
                        0,
                        CopyFromParent,
                        CopyFromParent,
                        (Visual *)CopyFromParent,
                        CWOverrideRedirect | CWEventMask,
                        &attrs);
}

Cursor
meta_x11_display_create_x_cursor (MetaX11Display *x11_display,
                                  MetaCursor      cursor)
{
  return meta_cursor_create_x_cursor (x11_display->xdisplay, cursor);
}

static char *
get_screen_name (Display *xdisplay,
                 int      number)
{
  char *p;
  char *dname;
  char *scr;

  /* DisplayString gives us a sort of canonical display,
   * vs. the user-entered name from XDisplayName()
   */
  dname = g_strdup (DisplayString (xdisplay));

  /* Change display name to specify this screen.
   */
  p = strrchr (dname, ':');
  if (p)
    {
      p = strchr (p, '.');
      if (p)
        *p = '\0';
    }

  scr = g_strdup_printf ("%s.%d", dname, number);

  g_free (dname);

  return scr;
}
