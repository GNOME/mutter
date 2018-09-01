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
#ifndef META_WAYLAND_INPUTFD_EVDEV_DEVICE_H
#define META_WAYLAND_INPUTFD_EVDEV_DEVICE_H

#include <glib.h>
#include <gudev/gudev.h>
#include <wayland-server.h>

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-device-pool.h"
#include "wayland/meta-wayland-types.h"

struct _MetaWaylandInputFdEvdevDevice
{
  GUdevDevice *udev_device;

  struct wl_list resource_list;
  struct wl_list focus_resource_list;

  MetaWaylandSurface *focus_surface;
  struct wl_listener focus_surface_listener;

  const gchar *name;
  uint32_t vid;
  uint32_t pid;

  MetaDeviceFile *device_file;
  int fd;
};

MetaWaylandInputFdEvdevDevice *
     meta_wayland_inputfd_evdev_device_new  (MetaWaylandInputFdSeat *seat,
                                             GUdevDevice            *device);
void meta_wayland_inputfd_evdev_device_free (MetaWaylandInputFdEvdevDevice *evdev_device);


struct wl_resource *
     meta_wayland_inputfd_evdev_device_create_new_resource (MetaWaylandInputFdEvdevDevice *device,
                                                            struct wl_client              *client,
                                                            struct wl_resource            *seat_resource,
                                                            uint32_t                       id);
void meta_wayland_inputfd_evdev_device_set_focus (MetaWaylandInputFdEvdevDevice *device,
                                                  MetaWaylandSurface            *surface);

void meta_wayland_inputfd_evdev_device_notify (MetaWaylandInputFdEvdevDevice *device,
                                               struct wl_resource            *resource);

#endif /* META_WAYLAND_INPUTFD_EVDEV_DEVICE_H */
