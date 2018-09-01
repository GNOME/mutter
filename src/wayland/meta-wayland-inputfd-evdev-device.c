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

#include <linux/input.h>
#include <sys/ioctl.h>

#include "backends/native/meta-backend-native.h"
#include "compositor/meta-surface-actor-wayland.h"
#include "inputfd-v1-server-protocol.h"
#include "wayland/meta-wayland-inputfd-evdev-device.h"

static void
unbind_resource (struct wl_resource *resource)
{
  wl_list_remove (wl_resource_get_link (resource));
}

static void
move_resources (struct wl_list *destination,
                struct wl_list *source)
{
  wl_list_insert_list (destination, source);
  wl_list_init (source);
}

static void
move_resources_for_client (struct wl_list   *destination,
			   struct wl_list   *source,
			   struct wl_client *client)
{
  struct wl_resource *resource, *tmp;

  wl_resource_for_each_safe (resource, tmp, source)
    {
      if (wl_resource_get_client (resource) == client)
        {
          wl_list_remove (wl_resource_get_link (resource));
          wl_list_insert (destination, wl_resource_get_link (resource));
        }
    }
}

static void
evdev_device_handle_focus_surface_destroy (struct wl_listener *listener,
                                           void               *data)
{
  MetaWaylandInputFdEvdevDevice *device = wl_container_of (listener, device,
                                                           focus_surface_listener);

  meta_wayland_inputfd_evdev_device_set_focus (device, NULL);
}

static gboolean
check_device_qualifies (GUdevDevice *device)
{
  const gchar *path = g_udev_device_get_device_file (device);

  if (!path || !strstr (path, "/event"))
    return FALSE;
  if (!g_udev_device_get_property_as_boolean (device, "ID_INPUT_JOYSTICK"))
    return FALSE;

  return TRUE;
}

MetaWaylandInputFdEvdevDevice *
meta_wayland_inputfd_evdev_device_new (MetaWaylandInputFdSeat *seat,
                                       GUdevDevice            *device)
{
  MetaWaylandInputFdEvdevDevice *evdev_device;
  GUdevDevice *parent;

  if (!check_device_qualifies (device))
    return NULL;

  parent = g_udev_device_get_parent (device);

  evdev_device = g_slice_new0 (MetaWaylandInputFdEvdevDevice);
  evdev_device->udev_device = g_object_ref (device);
  wl_list_init (&evdev_device->resource_list);
  wl_list_init (&evdev_device->focus_resource_list);
  evdev_device->focus_surface_listener.notify =
    evdev_device_handle_focus_surface_destroy;
  evdev_device->fd = -1;

  evdev_device->name =
    g_udev_device_get_sysfs_attr (parent, "name");
  evdev_device->vid =
    strtol (g_udev_device_get_property (device, "ID_VENDOR_ID"),
            NULL, 16);
  evdev_device->pid =
    strtol (g_udev_device_get_property (device, "ID_MODEL_ID"),
            NULL, 16);

  g_object_unref (parent);

  return evdev_device;
}

void
meta_wayland_inputfd_evdev_device_free (MetaWaylandInputFdEvdevDevice *evdev_device)
{
  struct wl_resource *resource, *next;

  meta_wayland_inputfd_evdev_device_set_focus (evdev_device, NULL);

  wl_resource_for_each_safe (resource, next, &evdev_device->resource_list)
    {
      wp_inputfd_device_evdev_v1_send_removed (resource);
      wl_list_remove (wl_resource_get_link (resource));
      wl_list_init (wl_resource_get_link (resource));
    }

  g_object_unref (evdev_device->udev_device);
  g_slice_free (MetaWaylandInputFdEvdevDevice, evdev_device);
}

static void
inputfd_device_evdev_destroy (struct wl_client   *client,
                              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wp_inputfd_device_evdev_v1_interface inputfd_device_evdev_interface = {
  inputfd_device_evdev_destroy,
};

struct wl_resource *
meta_wayland_inputfd_evdev_device_create_new_resource (MetaWaylandInputFdEvdevDevice *device,
                                                       struct wl_client              *client,
                                                       struct wl_resource            *seat_resource,
                                                       uint32_t                       id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wp_inputfd_device_evdev_v1_interface,
                                 wl_resource_get_version (seat_resource),
                                 id);
  wl_resource_set_implementation (resource, &inputfd_device_evdev_interface,
                                  device, unbind_resource);
  wl_resource_set_user_data (resource, device);
  wl_list_insert (&device->resource_list, wl_resource_get_link (resource));

  return resource;
}

void
meta_wayland_inputfd_evdev_device_notify (MetaWaylandInputFdEvdevDevice *device,
                                          struct wl_resource            *resource)
{
  wp_inputfd_device_evdev_v1_send_name (resource, device->name);
  wp_inputfd_device_evdev_v1_send_usb_id (resource, device->vid, device->pid);
  wp_inputfd_device_evdev_v1_send_done (resource);
}

static void
device_open_fd (MetaWaylandInputFdEvdevDevice *device)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();
  MetaDevicePool *device_pool;
  MetaDeviceFile *device_file;
  const char *path;
  g_autoptr (GError) error = NULL;

  g_assert (device->fd == -1);
  path = g_udev_device_get_device_file (device->udev_device);

  if (!META_IS_BACKEND_NATIVE (backend))
    return;

  device_pool = meta_backend_native_get_device_pool (META_BACKEND_NATIVE (backend));

  device_file = meta_device_pool_open (device_pool, path,
                                       META_DEVICE_FILE_FLAG_TAKE_CONTROL,
                                       &error);
  if (error)
    {
      g_warning ("Could not open device file: %s", error->message);
      device->fd = -1;
    }
  else
    {
      /* Take ownership on the device file, this must be fully closed
       * and opened on focus changes, to ensure the device does not stay
       * revoked.
       */
      device->device_file = device_file;
      device->fd = meta_device_file_get_fd (device->device_file);
    }
#endif
}

static void
device_close_fd (MetaWaylandInputFdEvdevDevice *device)
{
#ifdef HAVE_NATIVE_BACKEND
  MetaBackend *backend = meta_get_backend ();

  if (device->fd < 0)
    return;

  ioctl (device->fd, EVIOCREVOKE, NULL);

  if (!META_IS_BACKEND_NATIVE (backend))
    return;

  g_clear_pointer (&device->device_file, meta_device_file_release);
  device->fd = -1;
#endif
}

static void
meta_wayland_inputfd_evdev_device_broadcast_focus_in (MetaWaylandInputFdEvdevDevice *device,
                                                      MetaWaylandSurface            *surface,
                                                      uint32_t                       serial)
{
  struct wl_resource *resource;

  wl_resource_for_each (resource, &device->focus_resource_list)
    {
      wp_inputfd_device_evdev_v1_send_focus_in (resource,
						serial,
						device->fd,
						surface->resource);
    }
}

static void
meta_wayland_inputfd_evdev_device_broadcast_focus_out (MetaWaylandInputFdEvdevDevice *device)
{
  struct wl_resource *resource;

  wl_resource_for_each (resource, &device->focus_resource_list)
    {
      wp_inputfd_device_evdev_v1_send_focus_out (resource);
    }
}

void
meta_wayland_inputfd_evdev_device_set_focus (MetaWaylandInputFdEvdevDevice *device,
                                             MetaWaylandSurface            *surface)
{
  if (device->focus_surface == surface)
    return;

  if (device->focus_surface != NULL)
    {
      struct wl_list *focus_resources = &device->focus_resource_list;

      if (!wl_list_empty (focus_resources))
        {
          meta_wayland_inputfd_evdev_device_broadcast_focus_out (device);
          move_resources (&device->resource_list, &device->focus_resource_list);
        }

      wl_list_remove (&device->focus_surface_listener.link);
      device->focus_surface = NULL;
      device_close_fd (device);
    }

  if (surface != NULL)
    device_open_fd (device);

  if (surface != NULL && device->fd >= 0)
    {
      struct wl_client *client;

      device->focus_surface = surface;
      wl_resource_add_destroy_listener (device->focus_surface->resource,
                                        &device->focus_surface_listener);

      client = wl_resource_get_client (device->focus_surface->resource);
      move_resources_for_client (&device->focus_resource_list,
                                 &device->resource_list, client);

      if (!wl_list_empty (&device->focus_resource_list))
        {
          struct wl_display *display = wl_client_get_display (client);
          uint32_t serial = wl_display_next_serial (display);

          meta_wayland_inputfd_evdev_device_broadcast_focus_in (device,
                                                                surface,
                                                                serial);
        }
    }
}
