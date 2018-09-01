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
#include "wayland/meta-wayland-inputfd-seat.h"
#include "wayland/meta-wayland-inputfd-evdev-device.h"
#include "wayland/meta-wayland-seat.h"

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
notify_evdev_device_added (MetaWaylandInputFdSeat        *inputfd_seat,
                           MetaWaylandInputFdEvdevDevice *evdev_device,
                           struct wl_resource            *seat_resource)
{
  MetaWaylandSurface *focus;
  struct wl_resource *resource;
  struct wl_client *client;

  if (inputfd_seat->seat->keyboard)
    focus = inputfd_seat->seat->keyboard->focus_surface;
  else
    focus = NULL;

  client = wl_resource_get_client (seat_resource);
  resource = meta_wayland_inputfd_evdev_device_create_new_resource (evdev_device,
                                                                    client,
                                                                    seat_resource,
                                                                    0);
  wp_inputfd_seat_evdev_v1_send_device_added (seat_resource, resource);

  meta_wayland_inputfd_evdev_device_notify (evdev_device, resource);

  meta_wayland_inputfd_evdev_device_set_focus (evdev_device, focus);
}

static void
broadcast_evdev_device_added (MetaWaylandInputFdSeat        *seat,
                              MetaWaylandInputFdEvdevDevice *evdev_device)
{
  struct wl_resource *seat_resource;

  wl_resource_for_each (seat_resource, &seat->evdev_seat_resources)
    {
      notify_evdev_device_added (seat, evdev_device, seat_resource);
    }
}

static void
check_add_device (MetaWaylandInputFdSeat *inputfd_seat,
                  GUdevDevice            *device)
{
  MetaWaylandInputFdEvdevDevice *evdev_device;

  evdev_device = meta_wayland_inputfd_evdev_device_new (inputfd_seat,
                                                        device);
  if (evdev_device)
    {
      g_hash_table_insert (inputfd_seat->evdev_devices,
                           (gpointer) g_udev_device_get_sysfs_path (device),
                           evdev_device);
      broadcast_evdev_device_added (inputfd_seat, evdev_device);
      return;
    }
}

static void
remove_device (MetaWaylandInputFdSeat *inputfd_seat,
               GUdevDevice            *device)
{
  g_hash_table_remove (inputfd_seat->evdev_devices,
                       g_udev_device_get_sysfs_path (device));
}

static void
udev_event_cb (GUdevClient            *client,
               char                   *action,
               GUdevDevice            *device,
               MetaWaylandInputFdSeat *seat)
{
  if (g_strcmp0 (action, "add") == 0)
    check_add_device (seat, device);
  else if (g_strcmp0 (action, "remove") == 0)
    remove_device (seat, device);
}

MetaWaylandInputFdSeat *
meta_wayland_inputfd_seat_new (MetaWaylandInputFdManager *manager,
                               MetaWaylandSeat           *seat)
{
  const char * const subsystems[] = { "input", NULL };
  MetaWaylandInputFdSeat *inputfd_seat;
  GList *devices;

  inputfd_seat = g_slice_new0 (MetaWaylandInputFdSeat);
  wl_list_init (&inputfd_seat->evdev_seat_resources);
  inputfd_seat->seat = seat;
  inputfd_seat->evdev_devices =
    g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                           (GDestroyNotify) meta_wayland_inputfd_evdev_device_free);

  inputfd_seat->udev_client = g_udev_client_new (subsystems);
  g_signal_connect (inputfd_seat->udev_client, "uevent",
                    G_CALLBACK (udev_event_cb), inputfd_seat);

  devices = g_udev_client_query_by_subsystem (inputfd_seat->udev_client,
                                              subsystems[0]);
  while (devices)
    {
      GUdevDevice *device = devices->data;

      check_add_device (inputfd_seat, device);
      g_object_unref (device);
      devices = g_list_delete_link (devices, devices);
    }

  return inputfd_seat;
}

void
meta_wayland_inputfd_seat_free (MetaWaylandInputFdSeat *inputfd_seat)
{
  g_hash_table_destroy (inputfd_seat->evdev_devices);

  g_signal_handlers_disconnect_by_func (inputfd_seat->udev_client,
                                        udev_event_cb, inputfd_seat);
  g_object_unref (inputfd_seat->udev_client);

  g_slice_free (MetaWaylandInputFdSeat, inputfd_seat);
}

static void
notify_evdev_devices (MetaWaylandInputFdSeat *seat,
                      struct wl_resource     *seat_resource)
{
  MetaWaylandInputFdEvdevDevice *device;
  GHashTableIter iter;

  g_hash_table_iter_init (&iter, seat->evdev_devices);

  while (g_hash_table_iter_next (&iter, NULL, (void **) &device))
    {
      notify_evdev_device_added (seat, device, seat_resource);
    }
}

static void
inputfd_seat_evdev_destroy (struct wl_client   *client,
                            struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wp_inputfd_seat_evdev_v1_interface inputfd_seat_evdev_interface = {
  inputfd_seat_evdev_destroy
};

struct wl_resource *
meta_wayland_inputfd_seat_create_new_evdev_resource (MetaWaylandInputFdSeat *seat,
                                                     struct wl_client       *client,
                                                     struct wl_resource     *manager_resource,
                                                     uint32_t                id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wp_inputfd_seat_evdev_v1_interface,
                                 wl_resource_get_version (manager_resource),
                                 id);
  wl_resource_set_implementation (resource, &inputfd_seat_evdev_interface,
                                  seat, unbind_resource);
  wl_resource_set_user_data (resource, seat);
  wl_list_insert (&seat->evdev_seat_resources, wl_resource_get_link (resource));

  notify_evdev_devices (seat, resource);

  return resource;
}

void
meta_wayland_inputfd_seat_set_focus (MetaWaylandInputFdSeat *seat,
                                     MetaWaylandSurface     *surface)
{
  MetaWaylandInputFdEvdevDevice *device;
  GHashTableIter iter;

  g_hash_table_iter_init (&iter, seat->evdev_devices);

  while (g_hash_table_iter_next (&iter, NULL, (void **) &device))
    meta_wayland_inputfd_evdev_device_set_focus (device, surface);
}
