/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2013 Red Hat
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <X11/extensions/Xcomposite.h>

typedef struct {
  Display *xdisplay;
} Decorator;

typedef struct {
  Decorator decorator;
  Window child_window;

  GtkWidget *window;
  GtkWidget *socket;
} WindowFrame;

static void
socket_size_allocate (GtkWidget     *widget,
                      GtkAllocation *allocation,
                      gpointer       user_data)
{
  WindowFrame *frame = user_data;

  XMoveResizeWindow (GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (widget)),
                     frame->child_window,
                     allocation->x, allocation->y,
                     allocation->width, allocation->height);
}

static WindowFrame *
frame_window (Decorator *decorator,
              Window     child_window)
{
  WindowFrame *frame;
  XWindowAttributes attrs;
  GtkWidget *window, *socket;

  XGetWindowAttributes (decorator->xdisplay, child_window, &attrs);

  frame = g_slice_new0 (WindowFrame);
  frame->child_window = child_window;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  frame->window = window;
  gtk_window_move (GTK_WINDOW (window), attrs.x, attrs.y);

  socket = gtk_frame_new (NULL);
  frame->socket = socket;
  gtk_widget_set_size_request (socket, attrs.width, attrs.height);
  g_signal_connect (socket, "size-allocate",
                    G_CALLBACK (socket_size_allocate), frame);
  gtk_container_add (GTK_CONTAINER (window), socket);

  gtk_widget_show (socket);
  gtk_widget_show (window);

  XReparentWindow (decorator->xdisplay,
                   child_window,
                   GDK_WINDOW_XID (gtk_widget_get_window (window)),
                   /* these will be positioned correctly at the
                    * next size-allocate pass... */
                   0, 0);

  return frame;
}

static Window
find_test_window (Display *dpy)
{
  Window root, parent;
  Window *children;
  Window ret = None;
  unsigned int i, n_children;

  XQueryTree (dpy, DefaultRootWindow (dpy),
              &root, &parent,
              &children, &n_children);

  for (i = 0; i < n_children; i++)
    {
      Window child = children[i];
      char *name;

      XFetchName (dpy, child, &name);
      if (g_strcmp0 (name, "this is a test window") == 0)
        ret = child;

      g_free (name);

      if (ret)
        break;
    }

  return ret;
}

static void
decorator_init (Decorator *decorator)
{
  decorator->xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
}

int
main (int    argc,
      char **argv)
{
  Decorator decorator;
  Window window;

  gtk_init (&argc, &argv);

  decorator_init (&decorator);

  window = find_test_window (decorator.xdisplay);
  frame_window (&decorator, window);

  gtk_main ();
  return 0;
}
