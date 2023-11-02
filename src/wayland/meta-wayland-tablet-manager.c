/*
 * Wayland Support
 *
 * Copyright (C) 2015 Red Hat
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <glib.h>

#include <wayland-server.h>

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-tablet-manager.h"
#include "wayland/meta-wayland-tablet-seat.h"
#include "wayland/meta-wayland-tablet-tool.h"

#include "tablet-unstable-v2-server-protocol.h"

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
tablet_manager_get_tablet_seat (struct wl_client   *client,
                                struct wl_resource *resource,
                                guint32             id,
                                struct wl_resource *seat_resource)
{
  MetaWaylandTabletManager *tablet_manager = wl_resource_get_user_data (resource);
  MetaWaylandSeat *seat = wl_resource_get_user_data (seat_resource);
  MetaWaylandTabletSeat *tablet_seat;

  tablet_seat = meta_wayland_tablet_manager_ensure_seat (tablet_manager, seat);
  meta_wayland_tablet_seat_create_new_resource (tablet_seat, client,
                                                resource, id);
}

static void
tablet_manager_destroy (struct wl_client   *client,
                        struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct zwp_tablet_manager_v2_interface tablet_manager_interface = {
  tablet_manager_get_tablet_seat,
  tablet_manager_destroy
};

static void
bind_tablet_manager (struct wl_client *client,
                     void             *data,
                     uint32_t          version,
                     uint32_t          id)
{
  MetaWaylandCompositor *compositor = data;
  MetaWaylandTabletManager *tablet_manager = compositor->tablet_manager;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &zwp_tablet_manager_v2_interface,
                                 MIN (version, 1), id);
  wl_resource_set_implementation (resource, &tablet_manager_interface,
                                  tablet_manager, unbind_resource);
  wl_resource_set_user_data (resource, tablet_manager);
  wl_list_insert (&tablet_manager->resource_list,
                  wl_resource_get_link (resource));
}

static MetaWaylandTabletManager *
meta_wayland_tablet_manager_new (MetaWaylandCompositor *compositor)
{
  MetaWaylandTabletManager *tablet_manager;

  tablet_manager = g_new0 (MetaWaylandTabletManager, 1);
  tablet_manager->compositor = compositor;
  tablet_manager->wl_display = compositor->wayland_display;
  tablet_manager->seats = g_hash_table_new_full (NULL, NULL, NULL,
                                                 (GDestroyNotify) meta_wayland_tablet_seat_free);
  wl_list_init (&tablet_manager->resource_list);

  wl_global_create (tablet_manager->wl_display,
                    &zwp_tablet_manager_v2_interface, 1,
                    compositor, bind_tablet_manager);

  return tablet_manager;
}

void
meta_wayland_tablet_manager_init (MetaWaylandCompositor *compositor)
{
  compositor->tablet_manager = meta_wayland_tablet_manager_new (compositor);
}

void
meta_wayland_tablet_manager_finalize (MetaWaylandCompositor *compositor)
{
  g_hash_table_destroy (compositor->tablet_manager->seats);
  g_clear_pointer (&compositor->tablet_manager, g_free);
}

MetaWaylandTabletSeat *
meta_wayland_tablet_manager_ensure_seat (MetaWaylandTabletManager *manager,
                                         MetaWaylandSeat          *seat)
{
  MetaWaylandTabletSeat *tablet_seat;

  tablet_seat = g_hash_table_lookup (manager->seats, seat);

  if (!tablet_seat)
    {
      tablet_seat = meta_wayland_tablet_seat_new (manager, seat);
      g_hash_table_insert (manager->seats, seat, tablet_seat);
    }

  return tablet_seat;
}
