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
#include "config.h"

#include "inputfd-v1-server-protocol.h"
#include "wayland/meta-wayland-inputfd-manager.h"
#include "wayland/meta-wayland-inputfd-seat.h"
#include "wayland/meta-wayland-private.h"

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
inputfd_manager_get_seat_evdev (struct wl_client   *client,
                                struct wl_resource *resource,
                                uint32_t            id,
                                struct wl_resource *seat_resource)
{
  MetaWaylandInputFdManager *manager = wl_resource_get_user_data (resource);
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  MetaWaylandInputFdSeat *inputfd_seat;

  inputfd_seat = meta_wayland_inputfd_manager_ensure_seat (manager, seat);
  meta_wayland_inputfd_seat_create_new_evdev_resource (inputfd_seat, client,
                                                       resource, id);
}

static void
inputfd_manager_destroy (struct wl_client   *client,
                         struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wp_inputfd_manager_v1_interface inputfd_manager_interface = {
  inputfd_manager_get_seat_evdev,
  inputfd_manager_destroy
};

static void
bind_inputfd_manager (struct wl_client *client,
		      void             *data,
		      uint32_t          version,
		      uint32_t          id)
{
  MetaWaylandCompositor *compositor = data;
  MetaWaylandInputFdManager *manager = compositor->inputfd_manager;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wp_inputfd_manager_v1_interface,
                                 MIN (version, 1), id);
  wl_resource_set_implementation (resource, &inputfd_manager_interface,
                                  manager, unbind_resource);
  wl_resource_set_user_data (resource, manager);
  wl_list_insert (&manager->resource_list,
                  wl_resource_get_link (resource));
}

static MetaWaylandInputFdManager *
meta_wayland_inputfd_manager_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandInputFdManager *manager;

  manager = g_slice_new0 (MetaWaylandInputFdManager);
  manager->compositor = compositor;
  manager->wl_display = compositor->wayland_display;
  manager->seats = g_hash_table_new_full (NULL, NULL, NULL,
					  (GDestroyNotify) meta_wayland_inputfd_seat_free);
  wl_list_init (&manager->resource_list);

  wl_global_create (manager->wl_display,
                    &wp_inputfd_manager_v1_interface,
                    META_WP_INPUTFD_V1_VERSION,
                    compositor, bind_inputfd_manager);

  return manager;
}

void
meta_wayland_inputfd_manager_init (MetaWaylandCompositor *compositor)
{
  compositor->inputfd_manager = meta_wayland_inputfd_manager_new (compositor);
}

void
meta_wayland_inputfd_manager_free (MetaWaylandInputFdManager *manager)
{
  g_hash_table_destroy (manager->seats);
  g_slice_free (MetaWaylandInputFdManager, manager);
}

MetaWaylandInputFdSeat *
meta_wayland_inputfd_manager_ensure_seat (MetaWaylandInputFdManager *manager,
                                          MetaWaylandSeat           *seat)
{
  MetaWaylandInputFdSeat *inputfd_seat;

  inputfd_seat = g_hash_table_lookup (manager->seats, seat);

  if (!inputfd_seat)
    {
      inputfd_seat = meta_wayland_inputfd_seat_new (manager, seat);
      g_hash_table_insert (manager->seats, seat, inputfd_seat);
    }

  return inputfd_seat;
}
