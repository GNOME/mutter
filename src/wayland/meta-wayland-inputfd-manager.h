/*
 * Wayland Support
 *
 * Copyright (C) 2018 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef META_WAYLAND_INPUTFD_MANAGER_H
#define META_WAYLAND_INPUTFD_MANAGER_H

#include <wayland-server.h>

#include <glib.h>

#include "meta-wayland-types.h"

struct _MetaWaylandInputFdManager
{
  MetaWaylandCompositor *compositor;
  struct wl_display *wl_display;
  struct wl_list resource_list;

  GHashTable *seats;
};

void     meta_wayland_inputfd_manager_init (MetaWaylandCompositor    *compositor);
void     meta_wayland_inputfd_manager_free (MetaWaylandInputFdManager *manager);

MetaWaylandInputFdSeat *
         meta_wayland_inputfd_manager_ensure_seat (MetaWaylandInputFdManager *manager,
						   MetaWaylandSeat           *seat);

#endif /* META_WAYLAND_INPUTFD_MANAGER_H */
