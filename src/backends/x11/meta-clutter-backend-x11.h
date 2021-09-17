/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#ifndef META_CLUTTER_BACKEND_X11_H
#define META_CLUTTER_BACKEND_X11_H

#include <glib-object.h>

#include "backends/meta-backend-types.h"
#include "clutter/clutter-mutter.h"

struct _MetaClutterBackendX11
{
  ClutterBackend parent_instance;

  Display *xdisplay;

  /* event source */
  GSList  *event_filters;

  /* props */
  Atom atom_NET_WM_PID;
  Atom atom_NET_WM_PING;
  Atom atom_NET_WM_STATE;
  Atom atom_NET_WM_USER_TIME;
  Atom atom_WM_PROTOCOLS;
  Atom atom_WM_DELETE_WINDOW;
  Atom atom_XEMBED;
  Atom atom_XEMBED_INFO;
  Atom atom_NET_WM_NAME;
  Atom atom_UTF8_STRING;

  Time last_event_time;
};

#define META_TYPE_CLUTTER_BACKEND_X11 (meta_clutter_backend_x11_get_type ())
G_DECLARE_FINAL_TYPE (MetaClutterBackendX11, meta_clutter_backend_x11,
                      META, CLUTTER_BACKEND_X11,
                      ClutterBackend)

typedef enum
{
  META_X11_FILTER_CONTINUE,
  META_X11_FILTER_TRANSLATE,
  META_X11_FILTER_REMOVE
} MetaX11FilterReturn;

typedef MetaX11FilterReturn (*MetaX11FilterFunc) (XEvent        *xev,
                                                  ClutterEvent  *cev,
                                                  gpointer       data);

MetaClutterBackendX11 * meta_clutter_backend_x11_new (MetaBackend *backend);

void meta_clutter_x11_trap_x_errors (void);
gint meta_clutter_x11_untrap_x_errors (void);

Window meta_clutter_x11_get_root_window (void);

void meta_clutter_backend_x11_add_filter (MetaClutterBackendX11 *clutter_backend_x11,
                                          MetaX11FilterFunc      func,
                                          gpointer               data);

void meta_clutter_backend_x11_remove_filter (MetaClutterBackendX11 *clutter_backend_x11,
                                             MetaX11FilterFunc      func,
                                             gpointer               data);

void meta_clutter_x11_set_use_stereo_stage (gboolean use_stereo);
gboolean meta_clutter_x11_get_use_stereo_stage (void);

#endif /* META_CLUTTER_BACKEND_X11_H */
