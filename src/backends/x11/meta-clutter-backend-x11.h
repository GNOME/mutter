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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#pragma once

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

MetaClutterBackendX11 * meta_clutter_backend_x11_new (MetaBackend    *backend,
                                                      ClutterContext *context);
