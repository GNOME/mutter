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

typedef enum
{
  DEVICE_DRM_FD,
  DEVICE_CONNECTOR,
  DEVICE_DONE,
  DEVICE_RELEASED,
  CONNECTOR_NAME,
  CONNECTOR_DESCRIPTION,
  CONNECTOR_ID,
  CONNECTOR_DONE,
  CONNECTOR_WITHDRAWN,
  LEASE_FD,
  LEASE_FINISHED,
} DrmLeaseEventType;

typedef struct _DrmLeaseClient
{
  WaylandDisplay *display;

  /* Key:   struct wp_drm_lease_device_v1 *drm_lease_device
   * Value: DrmLeaseDevice *device
   */
  GHashTable *devices;

  /* Queue of DrmLeaseEventType */
  GQueue *event_queue;
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

typedef struct _DrmLeaseLease
{
  DrmLeaseClient *client;

  struct wp_drm_lease_v1 *lease;
  struct wp_drm_lease_request_v1 *request;
  int32_t fd;

  gboolean done;
} DrmLeaseLease;

static void
event_queue_add (GQueue           *event_queue,
                 DrmLeaseEventType event_type)
{
  g_queue_push_tail (event_queue, GUINT_TO_POINTER (event_type));
}

static void
event_queue_assert_event (GQueue           *event_queue,
                          DrmLeaseEventType expected)
{
  DrmLeaseEventType actual = GPOINTER_TO_UINT (g_queue_pop_head (event_queue));
  g_assert_cmpint (expected, ==, actual);
}

static void
event_queue_assert_empty (GQueue *event_queue)
{
  g_assert_cmpint (g_queue_get_length (event_queue), ==, 0);
}

static void
handle_lease_fd (void                   *user_data,
                 struct wp_drm_lease_v1 *drm_lease,
                 int32_t                 lease_fd)
{
  DrmLeaseLease *lease = user_data;

  event_queue_add (lease->client->event_queue, LEASE_FD);

  lease->fd = lease_fd;
  lease->done = TRUE;
}

static void
handle_finished (void                   *user_data,
                 struct wp_drm_lease_v1 *drm_lease)
{
  DrmLeaseLease *lease = user_data;

  event_queue_add (lease->client->event_queue, LEASE_FINISHED);

  lease->done = TRUE;
}

static const struct wp_drm_lease_v1_listener lease_listener = {
  .lease_fd = handle_lease_fd,
  .finished = handle_finished,
};

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
drm_lease_connector_get_at_index (guint                              index,
                                  DrmLeaseDevice                    *device,
                                  struct wp_drm_lease_connector_v1 **out_drm_lease_connector,
                                  DrmLeaseConnector                **out_connector)
{
  gpointer key, value;
  GHashTableIter iter;
  guint n = 0;

  g_hash_table_iter_init (&iter, device->connectors);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (n != index)
        {
          n++;
          continue;
        }

      if (out_drm_lease_connector)
        *out_drm_lease_connector = key;

      if (out_connector)
        *out_connector = value;

      return;
    }

  g_assert_not_reached ();
}

static void
handle_connector_name (void                             *user_data,
                       struct wp_drm_lease_connector_v1 *drm_lease_connector,
                       const char                       *connector_name)
{
  DrmLeaseDevice *device = user_data;
  DrmLeaseConnector *connector =
    drm_lease_connector_lookup (device, drm_lease_connector);

  event_queue_add (device->client->event_queue, CONNECTOR_NAME);

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

  event_queue_add (device->client->event_queue, CONNECTOR_DESCRIPTION);

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

  event_queue_add (device->client->event_queue, CONNECTOR_ID);

  connector->id = connector_id;
}

static void
handle_connector_done (void                             *user_data,
                       struct wp_drm_lease_connector_v1 *drm_lease_connector)
{
  DrmLeaseDevice *device = user_data;
  DrmLeaseConnector *connector =
    drm_lease_connector_lookup (device, drm_lease_connector);

  event_queue_add (device->client->event_queue, CONNECTOR_DONE);

  connector->done = TRUE;
}

static void
handle_connector_withdrawn (void                             *user_data,
                            struct wp_drm_lease_connector_v1 *drm_lease_connector)
{
  DrmLeaseDevice *device = user_data;

  event_queue_add (device->client->event_queue, CONNECTOR_WITHDRAWN);

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
drm_lease_device_get_at_index (guint                           index,
                               DrmLeaseClient                 *client,
                               struct wp_drm_lease_device_v1 **out_drm_lease_device,
                               DrmLeaseDevice                **out_device)
{
  gpointer key, value;
  GHashTableIter iter;
  guint n = 0;

  g_hash_table_iter_init (&iter, client->devices);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (n != index)
        {
          n++;
          continue;
        }

      if (out_drm_lease_device)
        *out_drm_lease_device = key;

      if (out_device)
        *out_device = value;

      return;
    }

  g_assert_not_reached ();
}

static void
handle_device_drm_fd (void                          *user_data,
                      struct wp_drm_lease_device_v1 *drm_lease_device,
                      int32_t                        drm_lease_device_fd)
{
  DrmLeaseClient *client = user_data;
  DrmLeaseDevice *device = drm_lease_device_lookup (client, drm_lease_device);

  event_queue_add (client->event_queue, DEVICE_DRM_FD);

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

  event_queue_add (client->event_queue, DEVICE_CONNECTOR);

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

  event_queue_add (client->event_queue, DEVICE_DONE);

  device->done = TRUE;
}

static void
handle_device_released (void                          *user_data,
                        struct wp_drm_lease_device_v1 *drm_lease_device)
{
  DrmLeaseClient *client = user_data;

  event_queue_add (client->event_queue, DEVICE_RELEASED);

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

static DrmLeaseLease *
drm_lease_lease_new (DrmLeaseClient *client,
                     guint           device_index,
                     guint          *connector_indices,
                     int             num_connectors)
{
  DrmLeaseLease *lease;
  DrmLeaseDevice *device;
  struct wp_drm_lease_device_v1 *drm_lease_device;
  int n;

  lease = g_new0 (DrmLeaseLease, 1);
  lease->client = client;
  lease->fd = -1;

  drm_lease_device_get_at_index (device_index,
                                 client,
                                 &drm_lease_device,
                                 &device);

  lease->request =
    wp_drm_lease_device_v1_create_lease_request (drm_lease_device);

  for (n = 0; n < num_connectors; n++)
    {
      struct wp_drm_lease_connector_v1 *drm_lease_connector;
      guint connector_index = connector_indices[n];

      drm_lease_connector_get_at_index (connector_index,
                                        device,
                                        &drm_lease_connector,
                                        NULL);
      wp_drm_lease_request_v1_request_connector (lease->request,
                                                 drm_lease_connector);
    }

  return lease;
}

static void
drm_lease_lease_submit (DrmLeaseLease *lease)
{
  lease->lease = wp_drm_lease_request_v1_submit (lease->request);

  wp_drm_lease_v1_add_listener (lease->lease, &lease_listener, lease);

  while (!lease->done)
    {
      if (wl_display_dispatch (lease->client->display->display) == -1)
        return;
    }
}

static void
drm_lease_lease_destroy (DrmLeaseLease *lease)
{
  wp_drm_lease_v1_destroy (lease->lease);
}

static void
drm_lease_lease_free (DrmLeaseLease *lease)
{
  g_clear_pointer (&lease, g_free);
}

static DrmLeaseClient *
drm_lease_client_new (WaylandDisplay *display)
{
  DrmLeaseClient *client;
  struct wl_registry *registry;
  gboolean client_done = FALSE;
  GHashTableIter iter;
  DrmLeaseDevice *device;
  int n;

  client = g_new0 (DrmLeaseClient, 1);
  client->display = display;
  client->devices =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) drm_lease_device_free);
  client->event_queue = g_queue_new ();

  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, client);
  wl_display_roundtrip (display->display);

  g_assert_cmpint (g_hash_table_size (client->devices), !=, 0);

  while (!client_done)
    {
      gboolean all_devices_done = TRUE;

      wayland_display_dispatch (display);

      g_hash_table_iter_init (&iter, client->devices);
      while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &device))
        all_devices_done = all_devices_done && device->done;

      client_done = all_devices_done;
    }

  g_hash_table_iter_init (&iter, client->devices);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &device))
    {
      event_queue_assert_event (client->event_queue, DEVICE_DRM_FD);
      for (n = 0; n < g_hash_table_size (device->connectors); n++)
        {
          event_queue_assert_event (client->event_queue, DEVICE_CONNECTOR);
          event_queue_assert_event (client->event_queue, CONNECTOR_NAME);
          event_queue_assert_event (client->event_queue, CONNECTOR_DESCRIPTION);
          event_queue_assert_event (client->event_queue, CONNECTOR_ID);
          event_queue_assert_event (client->event_queue, CONNECTOR_DONE);
        }
      event_queue_assert_event (client->event_queue, DEVICE_DONE);
    }
  event_queue_assert_empty (client->event_queue);

  return client;
}

static void
drm_lease_client_free (DrmLeaseClient *client)
{
  g_queue_free (client->event_queue);
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

static int
test_drm_lease_release_device (WaylandDisplay *display)
{
  DrmLeaseClient *client1;
  DrmLeaseClient *client2;
  struct wp_drm_lease_device_v1 *drm_lease_device;

  client1 = drm_lease_client_new (display);
  client2 = drm_lease_client_new (display);

  /* Release the first client's device */
  drm_lease_device_get_at_index (0, client1, &drm_lease_device, NULL);
  wp_drm_lease_device_v1_release (drm_lease_device);
  g_assert_cmpint (wl_display_roundtrip (display->display), !=, -1);

  event_queue_assert_event (client1->event_queue, DEVICE_RELEASED);
  event_queue_assert_empty (client1->event_queue);
  event_queue_assert_empty (client2->event_queue);

  /* Release the second client's device */
  drm_lease_device_get_at_index (0, client2, &drm_lease_device, NULL);
  wp_drm_lease_device_v1_release (drm_lease_device);
  g_assert_cmpint (wl_display_roundtrip (display->display), !=, -1);

  event_queue_assert_event (client2->event_queue, DEVICE_RELEASED);
  event_queue_assert_empty (client2->event_queue);
  event_queue_assert_empty (client1->event_queue);

  /* Check that a client error is raised if a released device is used */
  g_assert_cmpint (wl_display_get_error (display->display), ==, 0);
  wp_drm_lease_device_v1_release (drm_lease_device);
  g_assert_cmpint (wl_display_roundtrip (display->display), ==, -1);
  g_assert_cmpint (wl_display_get_error (display->display), !=, 0);

  drm_lease_client_free (client1);
  drm_lease_client_free (client2);

  return EXIT_SUCCESS;
}

static int
test_drm_lease_lease_request (WaylandDisplay *display)
{
  DrmLeaseClient *client1;
  DrmLeaseClient *client2;
  DrmLeaseLease *lease;
  guint connectors[] = {0};
  int num_connectors = G_N_ELEMENTS (connectors);

  client1 = drm_lease_client_new (display);
  client2 = drm_lease_client_new (display);

  /* Create and submit a lease request */
  lease = drm_lease_lease_new (client1, 0, connectors, num_connectors);
  drm_lease_lease_submit (lease);

  /* Check that the lease succeeded*/
  event_queue_assert_event (client1->event_queue, CONNECTOR_WITHDRAWN);
  event_queue_assert_event (client1->event_queue, DEVICE_DONE);
  event_queue_assert_event (client1->event_queue, LEASE_FD);
  event_queue_assert_empty (client1->event_queue);

  /* Check that the other client receive the withdrawn event */
  event_queue_assert_event (client2->event_queue, CONNECTOR_WITHDRAWN);
  event_queue_assert_event (client2->event_queue, DEVICE_DONE);
  event_queue_assert_empty (client2->event_queue);

  /* Finish the lease and check that both clients have access to the leased
     connector again */
  drm_lease_lease_destroy (lease);
  g_assert_cmpint (wl_display_roundtrip (display->display), !=, -1);

  event_queue_assert_event (client1->event_queue, DEVICE_CONNECTOR);
  event_queue_assert_event (client1->event_queue, CONNECTOR_NAME);
  event_queue_assert_event (client1->event_queue, CONNECTOR_DESCRIPTION);
  event_queue_assert_event (client1->event_queue, CONNECTOR_ID);
  event_queue_assert_event (client1->event_queue, CONNECTOR_DONE);
  event_queue_assert_event (client1->event_queue, DEVICE_DONE);
  event_queue_assert_empty (client1->event_queue);

  event_queue_assert_event (client2->event_queue, DEVICE_CONNECTOR);
  event_queue_assert_event (client2->event_queue, CONNECTOR_NAME);
  event_queue_assert_event (client2->event_queue, CONNECTOR_DESCRIPTION);
  event_queue_assert_event (client2->event_queue, CONNECTOR_ID);
  event_queue_assert_event (client2->event_queue, CONNECTOR_DONE);
  event_queue_assert_event (client2->event_queue, DEVICE_DONE);
  event_queue_assert_empty (client2->event_queue);

  drm_lease_lease_free (lease);
  drm_lease_client_free (client1);
  drm_lease_client_free (client2);

  return EXIT_SUCCESS;
}

static int
test_drm_lease_lease_leased_connector (WaylandDisplay *display)
{
  DrmLeaseClient *client1;
  DrmLeaseClient *client2;
  DrmLeaseLease *lease1;
  DrmLeaseLease *lease2;
  guint connectors[] = {0};
  int num_connectors = G_N_ELEMENTS (connectors);

  /* Create and submit 2 leases with the same connector */
  client1 = drm_lease_client_new (display);
  client2 = drm_lease_client_new (display);

  lease1 = drm_lease_lease_new (client1, 0, connectors, num_connectors);
  lease2 = drm_lease_lease_new (client2, 0, connectors, num_connectors);

  drm_lease_lease_submit (lease1);
  drm_lease_lease_submit (lease2);

  /* Check that the first one succeeded */
  event_queue_assert_event (client1->event_queue, CONNECTOR_WITHDRAWN);
  event_queue_assert_event (client1->event_queue, DEVICE_DONE);
  event_queue_assert_event (client1->event_queue, LEASE_FD);
  event_queue_assert_empty (client1->event_queue);

  /* Check that the second one failed */
  event_queue_assert_event (client2->event_queue, CONNECTOR_WITHDRAWN);
  event_queue_assert_event (client2->event_queue, DEVICE_DONE);
  event_queue_assert_event (client2->event_queue, LEASE_FINISHED);
  event_queue_assert_empty (client2->event_queue);

  drm_lease_lease_free (lease1);
  drm_lease_lease_free (lease2);
  drm_lease_client_free (client1);
  drm_lease_client_free (client2);

  return EXIT_SUCCESS;
}

static int
test_drm_lease_lease_duplicated_connector (WaylandDisplay *display)
{
  DrmLeaseClient *client;
  DrmLeaseLease *lease;
  guint connectors[] = {0, 0};
  int num_connectors = G_N_ELEMENTS (connectors);

  /* Create a lease with a duplicated connector */
  client = drm_lease_client_new (display);
  lease = drm_lease_lease_new (client, 0, connectors, num_connectors);

  /* Check that the correct error is returned */
  g_assert_cmpint (wl_display_roundtrip (display->display), ==, -1);
  g_assert_cmpint (wl_display_get_error (display->display), ==, EPROTO);
  g_assert_cmpint (wl_display_get_protocol_error (display->display, NULL, NULL),
                   ==,
                   WP_DRM_LEASE_REQUEST_V1_ERROR_DUPLICATE_CONNECTOR);

  drm_lease_lease_free (lease);
  drm_lease_client_free (client);

  return EXIT_SUCCESS;
}

static int
test_drm_lease_lease_no_connectors (WaylandDisplay *display)
{
  DrmLeaseClient *client;
  DrmLeaseLease *lease;

  /* Create and submit lease without connectors */
  client = drm_lease_client_new (display);
  lease = drm_lease_lease_new (client, 0, NULL, 0);

  drm_lease_lease_submit (lease);

  /* Check that the correct error is returned */
  g_assert_cmpint (wl_display_roundtrip (display->display), ==, -1);
  g_assert_cmpint (wl_display_get_error (display->display), ==, EPROTO);
  g_assert_cmpint (wl_display_get_protocol_error (display->display, NULL, NULL),
                   ==,
                   WP_DRM_LEASE_REQUEST_V1_ERROR_EMPTY_LEASE);

  drm_lease_lease_free (lease);
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
  else if (g_strcmp0 (test_case, "release-device") == 0)
    return test_drm_lease_release_device (display);
  else if (g_strcmp0 (test_case, "lease-request") == 0)
    return test_drm_lease_lease_request (display);
  else if (g_strcmp0 (test_case, "lease-leased-connector") == 0)
    return test_drm_lease_lease_leased_connector (display);
  else if (g_strcmp0 (test_case, "lease-duplicated-connector") == 0)
    return test_drm_lease_lease_duplicated_connector (display);
  else if (g_strcmp0 (test_case, "lease-no-connectors") == 0)
    return test_drm_lease_lease_no_connectors (display);

  return EXIT_FAILURE;
}
