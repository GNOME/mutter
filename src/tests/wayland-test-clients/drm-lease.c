/*
 * Copyright (C) 2024 Red Hat, Inc.
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
 */

#include "config.h"

#include <glib.h>
#include <stdlib.h>
#include <wayland-client.h>

#include "wayland-test-client-utils.h"

#include "drm-lease-v1-client-protocol.h"

typedef struct _DrmLeaseClient
{
  WaylandDisplay *display;

  /* Key:   struct wp_drm_lease_device_v1 *drm_lease_device
   * Value: DrmLeaseDevice *device
   */
  GHashTable *devices;
} DrmLeaseClient;

typedef struct _DrmLeaseDevice
{
  DrmLeaseClient *client;

  int32_t fd;

  /* Key:   struct wp_drm_lease_connector_v1 *drm_lease_connector
   * Value: DrmLeaseConnector *connector
   */
  GHashTable *connectors;

  gboolean done;
} DrmLeaseDevice;

typedef struct _DrmLeaseConnector
{
  DrmLeaseDevice *device;

  char *name;
  char *description;
  uint32_t id;

  gboolean done;
} DrmLeaseConnector;

static DrmLeaseConnector *
drm_lease_connector_new (DrmLeaseDevice *device)
{
  DrmLeaseConnector *connector;

  connector = g_new0 (DrmLeaseConnector, 1);
  connector->device = device;

  return connector;
}

static void
drm_lease_connector_free (DrmLeaseConnector *connector)
{
  g_clear_pointer (&connector->name, g_free);
  g_clear_pointer (&connector->description, g_free);
  g_clear_pointer (&connector, g_free);
}

static DrmLeaseConnector *
drm_lease_connector_lookup (DrmLeaseDevice                   *device,
                            struct wp_drm_lease_connector_v1 *drm_lease_connector)
{
  DrmLeaseConnector *connector;

  connector = g_hash_table_lookup (device->connectors, drm_lease_connector);
  g_assert_nonnull (connector);

  return connector;
}

static void
handle_connector_name (void                             *user_data,
                       struct wp_drm_lease_connector_v1 *drm_lease_connector,
                       const char                       *connector_name)
{
  DrmLeaseDevice *device = user_data;
  DrmLeaseConnector *connector =
    drm_lease_connector_lookup (device, drm_lease_connector);

  connector->name = g_strdup (connector_name);
}

static void
handle_connector_description (void                             *user_data,
                              struct wp_drm_lease_connector_v1 *drm_lease_connector,
                              const char                       *connector_description)
{
  DrmLeaseDevice *device = user_data;
  DrmLeaseConnector *connector =
    drm_lease_connector_lookup (device, drm_lease_connector);

  connector->description = g_strdup (connector_description);
}

static void
handle_connector_connector_id (void                             *user_data,
                               struct wp_drm_lease_connector_v1 *drm_lease_connector,
                               uint32_t                          connector_id)
{
  DrmLeaseDevice *device = user_data;
  DrmLeaseConnector *connector =
    drm_lease_connector_lookup (device, drm_lease_connector);

  connector->id = connector_id;
}

static void
handle_connector_done (void                             *user_data,
                       struct wp_drm_lease_connector_v1 *drm_lease_connector)
{
  DrmLeaseDevice *device = user_data;
  DrmLeaseConnector *connector =
    drm_lease_connector_lookup (device, drm_lease_connector);

  connector->done = TRUE;
}

static void
handle_connector_withdrawn (void                             *user_data,
                            struct wp_drm_lease_connector_v1 *drm_lease_connector)
{
  DrmLeaseDevice *device = user_data;

  wp_drm_lease_connector_v1_destroy (drm_lease_connector);
  g_hash_table_remove (device->connectors, drm_lease_connector);
}

static const struct wp_drm_lease_connector_v1_listener connector_listener = {
  .name = handle_connector_name,
  .description = handle_connector_description,
  .connector_id = handle_connector_connector_id,
  .done = handle_connector_done,
  .withdrawn = handle_connector_withdrawn,
};

static DrmLeaseDevice *
drm_lease_device_new (DrmLeaseClient *client)
{
  DrmLeaseDevice *device;

  device = g_new0 (DrmLeaseDevice, 1);
  device->client = client;
  device->fd = -1;
  device->connectors =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) drm_lease_connector_free);

  return device;
}

static void
drm_lease_device_free (DrmLeaseDevice *device)
{
  if (device->fd >= 0)
    close (device->fd);

  g_clear_pointer (&device->connectors, g_hash_table_unref);
  g_clear_pointer (&device, g_free);
}

static DrmLeaseDevice *
drm_lease_device_lookup (DrmLeaseClient                *client,
                         struct wp_drm_lease_device_v1 *drm_lease_device)
{
  DrmLeaseDevice *device;

  device = g_hash_table_lookup (client->devices, drm_lease_device);
  g_assert_nonnull (device);

  return device;
}

static void
handle_device_drm_fd (void                          *user_data,
                      struct wp_drm_lease_device_v1 *drm_lease_device,
                      int32_t                        drm_lease_device_fd)
{
  DrmLeaseClient *client = user_data;
  DrmLeaseDevice *device = drm_lease_device_lookup (client, drm_lease_device);

  device->fd = drm_lease_device_fd;
}

static void
handle_device_connector (void                             *user_data,
                         struct wp_drm_lease_device_v1    *drm_lease_device,
                         struct wp_drm_lease_connector_v1 *drm_lease_connector)
{
  DrmLeaseClient *client = user_data;
  DrmLeaseDevice *device = drm_lease_device_lookup (client, drm_lease_device);
  DrmLeaseConnector *connector;

  connector = drm_lease_connector_new (device);
  g_hash_table_insert (device->connectors, drm_lease_connector, connector);

  wp_drm_lease_connector_v1_add_listener (drm_lease_connector,
                                          &connector_listener,
                                          device);
}

static void
handle_device_done (void                          *user_data,
                    struct wp_drm_lease_device_v1 *drm_lease_device)
{
  DrmLeaseClient *client = user_data;
  DrmLeaseDevice *device = drm_lease_device_lookup (client, drm_lease_device);

  device->done = TRUE;
}

static void
handle_device_released (void                          *user_data,
                        struct wp_drm_lease_device_v1 *drm_lease_device)
{
  DrmLeaseClient *client = user_data;

  g_hash_table_remove (client->devices, drm_lease_device);
}

static const struct wp_drm_lease_device_v1_listener device_listener = {
  .drm_fd = handle_device_drm_fd,
  .connector = handle_device_connector,
  .done = handle_device_done,
  .released = handle_device_released,
};

static void
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  DrmLeaseClient *client = user_data;
  struct wp_drm_lease_device_v1 *drm_lease_device;
  DrmLeaseDevice *device;

  if (strcmp (interface, wp_drm_lease_device_v1_interface.name) == 0)
    {
      drm_lease_device =
        wl_registry_bind (registry, id, &wp_drm_lease_device_v1_interface, 1);
      device = drm_lease_device_new (client);
      g_hash_table_insert (client->devices, drm_lease_device, device);

      wp_drm_lease_device_v1_add_listener (drm_lease_device,
                                           &device_listener,
                                           client);
    }
}

static void
handle_registry_global_remove (void               *user_data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
  handle_registry_global,
  handle_registry_global_remove
};

static DrmLeaseClient *
drm_lease_client_new (WaylandDisplay *display)
{
  DrmLeaseClient *client;
  struct wl_registry *registry;
  gboolean client_done = FALSE;

  client = g_new0 (DrmLeaseClient, 1);
  client->display = display;
  client->devices =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) drm_lease_device_free);

  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, client);
  wl_display_roundtrip (display->display);

  g_assert_cmpint (g_hash_table_size (client->devices), !=, 0);

  while (!client_done)
    {
      gboolean all_devices_done = TRUE;
      GHashTableIter iter;
      DrmLeaseDevice *device;

      wayland_display_dispatch (display);

      g_hash_table_iter_init (&iter, client->devices);
      while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &device))
        all_devices_done = all_devices_done && device->done;

      client_done = all_devices_done;
    }

  return client;
}

static void
drm_lease_client_free (DrmLeaseClient *client)
{
  g_clear_pointer (&client->devices, g_hash_table_unref);
  g_clear_pointer (&client, g_free);
}

static int
test_drm_lease_client_connection (WaylandDisplay *display)
{
  DrmLeaseClient *client;

  client = drm_lease_client_new (display);
  drm_lease_client_free (client);

  return EXIT_SUCCESS;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (WaylandDisplay) display = NULL;
  char *test_case;

  if (argc != 2)
    return EXIT_FAILURE;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER);
  test_case = argv[1];

  if (g_strcmp0 (test_case, "client-connection") == 0)
    return test_drm_lease_client_connection (display);

  return EXIT_FAILURE;
}
