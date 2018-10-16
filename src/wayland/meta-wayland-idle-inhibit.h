/*
 * Copyright (C) 2018 SUSE Linux Products GmbH
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
 */

#ifndef META_WAYLAND_IDLE_H
#define META_WAYLAND_IDLE_H

#include <wayland-server.h>
#include <glib.h>
#include "idle-inhibit-unstable-v1-server-protocol.h"

#include "meta-wayland-surface.h"
#include "meta-wayland-types.h"

struct _MetaWaylandIdleInhibitor
{
  MetaWaylandSurface	   *surface;
  GDBusProxy 		   *session_proxy;
  guint			   cookie;
  gulong                   inhibit_idle_handler;
  gboolean                 idle_inhibited;
};

typedef struct _MetaWaylandIdleInhibitor MetaWaylandIdleInhibitor;

gboolean
meta_wayland_idle_inhibit_init (MetaWaylandCompositor *compositor);
#endif
