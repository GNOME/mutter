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
#ifndef META_WAYLAND_INPUTFD_SEAT_H
#define META_WAYLAND_INPUTFD_SEAT_H

#include <glib.h>
#include <gudev/gudev.h>
#include <wayland-server.h>

#include "meta-wayland-types.h"

struct _MetaWaylandInputFdSeat
{
  GUdevClient *udev_client;
  MetaWaylandSeat *seat;
  struct wl_list evdev_seat_resources;
  GHashTable *evdev_devices;
};

MetaWaylandInputFdSeat *
     meta_wayland_inputfd_seat_new  (MetaWaylandInputFdManager *manager,
                                     MetaWaylandSeat           *seat);
void meta_wayland_inputfd_seat_free (MetaWaylandInputFdSeat *inputfd_seat);

struct wl_resource *
     meta_wayland_inputfd_seat_create_new_evdev_resource (MetaWaylandInputFdSeat *seat,
                                                          struct wl_client       *client,
                                                          struct wl_resource     *resource,
                                                          uint32_t                id);

void meta_wayland_inputfd_seat_set_focus (MetaWaylandInputFdSeat *seat,
                                          MetaWaylandSurface     *surface);

#endif /* META_WAYLAND_INPUTFD_SEAT_H */
