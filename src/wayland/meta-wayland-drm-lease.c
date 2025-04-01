/*
 * Copyright (C) 2016 Red Hat Inc.
 * Copyright (C) 2017 Intel Corporation
 * Copyright (C) 2018,2019 DisplayLink (UK) Ltd.
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

#include "config.h"

#include "wayland/meta-wayland-drm-lease.h"

#include <glib.h>
#include <glib/gstdio.h>

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-drm-lease.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms.h"
#include "backends/edid.h"
#include "backends/meta-launcher.h"
#include "wayland/meta-wayland-private.h"

#include "drm-lease-v1-server-protocol.h"

struct _MetaWaylandDrmLeaseManager
{
  MetaWaylandCompositor *compositor;

  /* Key:   MetaKmsDevice *kms_device
   * Value: MetaWaylandDrmLeaseDevice *lease_device
   */
  GHashTable *devices;

  GList *leases;

  gulong device_added_handler_id;
  gulong device_removed_handler_id;
  gulong connector_added_handler_id;
  gulong connector_removed_handler_id;
};

typedef struct _MetaWaylandDrmLeaseDevice
{
  MetaWaylandDrmLeaseManager *lease_manager;

  struct wl_global *global;
  MetaKmsDevice *kms_device;

  /* Key:   MetaKmsConnector *kms_connector
   * Value: MetaWaylandDrmLeaseConnector *lease_connector
   */
  GHashTable *connectors;

  GList *resources;

  /* List of pointers to struct wl_resource with the clients that are waiting
   * for a drm_fd event.
   */
  GList *pending_resources;
} MetaWaylandDrmLeaseDevice;

typedef struct _MetaWaylandDrmLeaseConnector
{
  MetaWaylandDrmLeaseDevice *lease_device;

  MetaKmsConnector *kms_connector;
  char *description;

  GList *resources;
} MetaWaylandDrmLeaseConnector;

typedef struct _MetaWaylandDrmLeaseRequest
{
  MetaWaylandDrmLeaseDevice *lease_device;
  GList *lease_connectors;
  struct wl_resource *resource;
} MetaWaylandDrmLeaseRequest;

typedef struct _MetaWaylandDrmLease
{
  MetaWaylandDrmLeaseManager *lease_manager;
  MetaWaylandDrmLeaseDevice *lease_device;
  uint32_t lessee_id;
  struct wl_resource *resource;
} MetaWaylandDrmLease;

static MetaDrmLeaseManager *
drm_lease_manager_from_lease_manager (MetaWaylandDrmLeaseManager *lease_manager)
{
  MetaWaylandCompositor *compositor = lease_manager->compositor;
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);

  return meta_backend_native_get_drm_lease_manager (backend_native);
}

static void
meta_wayland_drm_lease_device_free (MetaWaylandDrmLeaseDevice *lease_device)
{
  g_object_unref (lease_device->kms_device);
  g_clear_pointer (&lease_device->connectors, g_hash_table_unref);
}

static void
meta_wayland_drm_lease_device_release (MetaWaylandDrmLeaseDevice *lease_device)
{
  g_rc_box_release_full (lease_device,
                         (GDestroyNotify) meta_wayland_drm_lease_device_free);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaWaylandDrmLeaseDevice,
                               meta_wayland_drm_lease_device_release);

static void
meta_wayland_drm_lease_connector_free (MetaWaylandDrmLeaseConnector *lease_connector)
{
  g_object_unref (lease_connector->kms_connector);
  g_free (lease_connector->description);
}

static void
meta_wayland_drm_lease_connector_release (MetaWaylandDrmLeaseConnector *lease_connector)
{
  g_rc_box_release_full (lease_connector,
                         (GDestroyNotify) meta_wayland_drm_lease_connector_free);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaWaylandDrmLeaseConnector,
                               meta_wayland_drm_lease_connector_release);

static void
meta_wayland_drm_lease_free (MetaWaylandDrmLease *lease)
{
  meta_wayland_drm_lease_device_release (lease->lease_device);
}

static void
meta_wayland_drm_lease_release (MetaWaylandDrmLease *lease)
{
  g_rc_box_release_full (lease, (GDestroyNotify) meta_wayland_drm_lease_free);
}

static void
meta_wayland_drm_lease_revoke (MetaWaylandDrmLease *lease)
{
  MetaDrmLeaseManager *drm_lease_manager =
    drm_lease_manager_from_lease_manager (lease->lease_manager);
  MetaDrmLease *drm_lease =
    meta_drm_lease_manager_get_lease_from_id (drm_lease_manager,
                                              lease->lessee_id);

  if (drm_lease)
    meta_drm_lease_revoke (drm_lease);
}

static void
on_lease_revoked (MetaDrmLease        *drm_lease,
                  struct wl_resource  *resource)
{
  wp_drm_lease_v1_send_finished (resource);
}

static void
wp_drm_lease_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  MetaWaylandDrmLease *lease = wl_resource_get_user_data (resource);

  meta_wayland_drm_lease_revoke (lease);

  wl_resource_destroy (resource);
}

static const struct wp_drm_lease_v1_interface drm_lease_implementation = {
  wp_drm_lease_destroy,
};

static void
wp_drm_lease_destructor (struct wl_resource *resource)
{
  MetaWaylandDrmLease *lease = wl_resource_get_user_data (resource);
  MetaDrmLeaseManager *drm_lease_manager =
    drm_lease_manager_from_lease_manager (lease->lease_manager);
  MetaDrmLease *drm_lease;

  meta_wayland_drm_lease_revoke (lease);

  drm_lease =
    meta_drm_lease_manager_get_lease_from_id (drm_lease_manager,
                                              lease->lessee_id);
  if (drm_lease)
    {
      g_signal_handlers_disconnect_by_func (drm_lease,
                                            (gpointer) on_lease_revoked,
                                            lease->resource);
    }

  lease->lease_manager->leases = g_list_remove (lease->lease_manager->leases,
                                                lease);
  meta_wayland_drm_lease_release (lease);
}

static void
wp_drm_lease_request_request_connector (struct wl_client   *client,
                                        struct wl_resource *resource,
                                        struct wl_resource *connector)
{
  MetaWaylandDrmLeaseRequest *lease_request =
    wl_resource_get_user_data (resource);
  MetaWaylandDrmLeaseConnector *lease_connector =
    wl_resource_get_user_data (connector);

  if (lease_request->lease_device != lease_connector->lease_device)
    {
      wl_resource_post_error (resource,
                              WP_DRM_LEASE_REQUEST_V1_ERROR_WRONG_DEVICE,
                              "Wrong lease device");
      return;
    }

  if (g_list_find (lease_request->lease_connectors, lease_connector))
    {
      wl_resource_post_error (resource,
                              WP_DRM_LEASE_REQUEST_V1_ERROR_DUPLICATE_CONNECTOR,
                              "Connector requested twice");
      return;
    }

  lease_request->lease_connectors =
    g_list_append (lease_request->lease_connectors,
                   g_rc_box_acquire (lease_connector));
}

static void
wp_drm_lease_request_submit (struct wl_client   *client,
                             struct wl_resource *resource,
                             uint32_t            id)
{
  MetaWaylandDrmLeaseRequest *lease_request =
    wl_resource_get_user_data (resource);
  MetaWaylandDrmLeaseDevice *lease_device = lease_request->lease_device;
  MetaWaylandDrmLeaseManager *lease_manager = lease_device->lease_manager;
  MetaKmsDevice *kms_device = lease_device->kms_device;
  MetaDrmLeaseManager *drm_lease_manager =
    drm_lease_manager_from_lease_manager (lease_manager);
  MetaWaylandDrmLease *lease;
  g_autoptr (GList) connectors = NULL;
  g_autoptr (MetaDrmLease) drm_lease = NULL;
  g_autoptr (GError) error = NULL;
  g_autofd int fd = -1;
  GList *l;

  if (!lease_request->lease_connectors)
    {
      wl_resource_post_error (resource,
                              WP_DRM_LEASE_REQUEST_V1_ERROR_EMPTY_LEASE,
                              "Empty DRM lease request");
      wl_resource_destroy (resource);
      return;
    }

  lease = g_rc_box_new0 (MetaWaylandDrmLease);
  lease->lease_manager = lease_manager;
  lease->lease_device = g_rc_box_acquire (lease_device);
  lease->resource =
    wl_resource_create (client, &wp_drm_lease_v1_interface,
                        wl_resource_get_version (resource), id);

  wl_resource_set_implementation (lease->resource,
                                  &drm_lease_implementation,
                                  lease,
                                  wp_drm_lease_destructor);

  lease_manager->leases = g_list_append (lease_manager->leases, lease);

  for (l = lease_request->lease_connectors; l; l = l->next)
    {
      MetaWaylandDrmLeaseConnector *lease_connector = l->data;
      MetaKmsConnector *kms_connector = lease_connector->kms_connector;

      connectors = g_list_append (connectors, kms_connector);
    }

  drm_lease = meta_drm_lease_manager_lease_connectors (drm_lease_manager,
                                                       kms_device,
                                                       connectors,
                                                       &error);
  if (!drm_lease)
    {
      g_warning ("Failed to create lease from connector list: %s",
                 error->message);
      wp_drm_lease_v1_send_finished (lease->resource);
      wl_resource_destroy (resource);
      return;
    }

  g_signal_connect (drm_lease, "revoked",
                    G_CALLBACK (on_lease_revoked),
                    lease->resource);

  fd = meta_drm_lease_steal_fd (drm_lease);
  wp_drm_lease_v1_send_lease_fd (lease->resource, fd);

  lease->lessee_id = meta_drm_lease_get_id (drm_lease);

  wl_resource_destroy (resource);
}

static const struct wp_drm_lease_request_v1_interface drm_lease_request_implementation = {
  wp_drm_lease_request_request_connector,
  wp_drm_lease_request_submit,
};

static void
wp_drm_lease_request_destructor (struct wl_resource *resource)
{
  MetaWaylandDrmLeaseRequest *lease_request =
    wl_resource_get_user_data (resource);

  meta_wayland_drm_lease_device_release (lease_request->lease_device);
  g_list_foreach (lease_request->lease_connectors,
                  (GFunc) meta_wayland_drm_lease_connector_release,
                  NULL);
  g_free (lease_request);
}

static void
wp_drm_lease_device_create_lease_request (struct wl_client   *client,
                                          struct wl_resource *resource,
                                          uint32_t            id)
{
  MetaWaylandDrmLeaseDevice *lease_device =
    wl_resource_get_user_data (resource);
  MetaWaylandDrmLeaseRequest *lease_request;

  lease_request = g_new0 (MetaWaylandDrmLeaseRequest, 1);
  lease_request->lease_device = g_rc_box_acquire (lease_device);
  lease_request->resource =
    wl_resource_create (client, &wp_drm_lease_request_v1_interface,
                        wl_resource_get_version (resource), id);

  wl_resource_set_implementation (lease_request->resource,
                                  &drm_lease_request_implementation,
                                  lease_request,
                                  wp_drm_lease_request_destructor);
}

static void
wp_drm_lease_device_release (struct wl_client   *client,
                             struct wl_resource *resource)
{
  wp_drm_lease_device_v1_send_released (resource);
  wl_resource_destroy (resource);
}

static const struct wp_drm_lease_device_v1_interface drm_lease_device_implementation = {
  wp_drm_lease_device_create_lease_request,
  wp_drm_lease_device_release,
};

static char *
get_connector_description (MetaKmsConnector *kms_connector)
{
  const MetaKmsConnectorState *connector_state;
  gconstpointer edid_data;
  g_autoptr (MetaEdidInfo) edid_info = NULL;
  size_t edid_size;
  g_autofree char *vendor = NULL;
  g_autofree char *product = NULL;
  GString *description;

  connector_state = meta_kms_connector_get_current_state (kms_connector);
  if (!connector_state || !connector_state->edid_data)
    return g_strdup ("");

  edid_data = g_bytes_get_data (connector_state->edid_data, &edid_size);
  edid_info = meta_edid_info_new_parse (edid_data, edid_size);

  description = g_string_new (NULL);

  vendor = g_strndup (edid_info->manufacturer_code, 4);
  if (vendor && g_utf8_validate (vendor, -1, NULL))
    g_string_append_printf (description, "%s", vendor);

  product = g_strndup (edid_info->dsc_product_name, 14);
  if (product && g_utf8_validate (product, -1, NULL))
    {
      if (description->len > 0)
        g_string_append_c (description, ' ');
      g_string_append_printf (description, "%s", product);
    }

  if (description->len == 0)
    {
      g_string_append_printf (description, "%s",
                              meta_kms_connector_get_name (kms_connector));
    }

  return g_string_free_and_steal (description);
}

static MetaWaylandDrmLeaseConnector *
meta_wayland_drm_lease_connector_new (MetaWaylandDrmLeaseDevice *lease_device,
                                      MetaKmsConnector          *kms_connector)
{
  MetaWaylandDrmLeaseConnector *lease_connector;

  lease_connector = g_rc_box_new0 (MetaWaylandDrmLeaseConnector);
  lease_connector->lease_device = lease_device;
  lease_connector->kms_connector = g_object_ref (kms_connector);
  lease_connector->description = get_connector_description (kms_connector);

  return lease_connector;
}

static void
meta_wayland_drm_lease_connector_send_withdrawn (MetaWaylandDrmLeaseConnector *lease_connector)
{
  GList *l;

  for (l = lease_connector->resources; l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      if (wl_resource_get_user_data (resource) == lease_connector)
        wp_drm_lease_connector_v1_send_withdrawn (resource);
    }
}

static void
drm_lease_connector_destroy (struct wl_client   *client,
                             struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wp_drm_lease_connector_v1_interface drm_lease_connector_implementation = {
  drm_lease_connector_destroy,
};

static void
wp_drm_lease_connector_destructor (struct wl_resource *resource)
{
  MetaWaylandDrmLeaseConnector *lease_connector =
    wl_resource_get_user_data (resource);

  lease_connector->resources = g_list_remove (lease_connector->resources,
                                              resource);
  meta_wayland_drm_lease_connector_release (lease_connector);
}

static void
send_new_connector_resource (MetaWaylandDrmLeaseDevice    *lease_device,
                             struct wl_resource           *device_resource,
                             MetaWaylandDrmLeaseConnector *lease_connector)
{
  struct wl_resource *connector_resource;
  const char *connector_name;
  uint32_t connector_id;

  connector_resource =
    wl_resource_create (wl_resource_get_client (device_resource),
                        &wp_drm_lease_connector_v1_interface,
                        wl_resource_get_version (device_resource),
                        0);
  wl_resource_set_implementation (connector_resource,
                                  &drm_lease_connector_implementation,
                                  g_rc_box_acquire (lease_connector),
                                  wp_drm_lease_connector_destructor);

  lease_connector->resources = g_list_append (lease_connector->resources,
                                              connector_resource);

  connector_name = meta_kms_connector_get_name (lease_connector->kms_connector);
  connector_id = meta_kms_connector_get_id (lease_connector->kms_connector);

  wp_drm_lease_device_v1_send_connector (device_resource, connector_resource);
  wp_drm_lease_connector_v1_send_name (connector_resource, connector_name);
  wp_drm_lease_connector_v1_send_description (connector_resource,
                                              lease_connector->description);
  wp_drm_lease_connector_v1_send_connector_id (connector_resource,
                                               connector_id);
  wp_drm_lease_connector_v1_send_done (connector_resource);
}

static void
send_connectors (MetaWaylandDrmLeaseDevice *lease_device,
                 struct wl_resource        *device_resource)
{
  GHashTableIter iter;
  MetaWaylandDrmLeaseConnector *lease_connector;

  g_hash_table_iter_init (&iter, lease_device->connectors);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &lease_connector))
    send_new_connector_resource (lease_device, device_resource, lease_connector);
}

static gboolean
send_drm_fd (struct wl_client          *client,
             MetaWaylandDrmLeaseDevice *lease_device,
             struct wl_resource        *device_resource)
{
  g_autofd int fd = -1;
  MetaKmsImplDevice *impl_device;

  impl_device = meta_kms_device_get_impl_device (lease_device->kms_device);
  fd = meta_kms_impl_device_open_non_privileged_fd (impl_device);
  if (fd < 0)
    return FALSE;

  wp_drm_lease_device_v1_send_drm_fd (device_resource, fd);
  return TRUE;
}

static gboolean
send_on_device_bind_events (struct wl_client          *client,
                            MetaWaylandDrmLeaseDevice *lease_device,
                            struct wl_resource        *device_resource)
{
  if (!send_drm_fd (client, lease_device, device_resource))
    return FALSE;

  send_connectors (lease_device, device_resource);
  wp_drm_lease_device_v1_send_done (device_resource);
  return TRUE;
}

static void
wp_drm_lease_device_destructor (struct wl_resource *resource)
{
  MetaWaylandDrmLeaseDevice *lease_device =
    wl_resource_get_user_data (resource);

  lease_device->resources = g_list_remove (lease_device->resources, resource);
  lease_device->pending_resources =
    g_list_remove (lease_device->pending_resources, resource);
  meta_wayland_drm_lease_device_release (lease_device);
}

static void
lease_device_bind (struct wl_client *client,
                   void             *user_data,
                   uint32_t          version,
                   uint32_t          id)
{
  MetaWaylandDrmLeaseDevice *lease_device = user_data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &wp_drm_lease_device_v1_interface,
                                 version, id);
  wl_resource_set_implementation (resource,
                                  &drm_lease_device_implementation,
                                  g_rc_box_acquire (lease_device),
                                  wp_drm_lease_device_destructor);

  if (send_on_device_bind_events (client, lease_device, resource))
    {
      lease_device->resources = g_list_prepend (lease_device->resources,
                                                resource);
    }
  else
    {
      lease_device->pending_resources =
        g_list_prepend (lease_device->pending_resources, resource);
    }
}

static void
meta_wayland_drm_lease_device_add_connector (MetaKmsConnector          *kms_connector,
                                             MetaWaylandDrmLeaseDevice *lease_device)
{
  g_autoptr (MetaWaylandDrmLeaseConnector) lease_connector = NULL;

  lease_connector = meta_wayland_drm_lease_connector_new (lease_device,
                                                          kms_connector);
  g_hash_table_insert (lease_device->connectors,
                       kms_connector,
                       g_steal_pointer (&lease_connector));
}

static MetaWaylandDrmLeaseDevice *
meta_wayland_drm_lease_device_new (MetaWaylandDrmLeaseManager *lease_manager,
                                   MetaKmsDevice              *kms_device)
{
  struct wl_display *wayland_display =
    meta_wayland_compositor_get_wayland_display (lease_manager->compositor);
  MetaDrmLeaseManager *drm_lease_manager =
    drm_lease_manager_from_lease_manager (lease_manager);
  MetaWaylandDrmLeaseDevice *lease_device;
  g_autoptr (GList) kms_connectors = NULL;

  lease_device = g_rc_box_new0 (MetaWaylandDrmLeaseDevice);
  lease_device->lease_manager = lease_manager;
  lease_device->kms_device = g_object_ref (kms_device);

  lease_device->connectors =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) meta_wayland_drm_lease_connector_release);

  kms_connectors = meta_drm_lease_manager_get_connectors (drm_lease_manager,
                                                          kms_device);
  g_list_foreach (kms_connectors,
                  (GFunc) meta_wayland_drm_lease_device_add_connector,
                  lease_device);

  lease_device->global = wl_global_create (wayland_display,
                                           &wp_drm_lease_device_v1_interface,
                                           META_WP_DRM_LEASE_DEVICE_V1_VERSION,
                                           lease_device,
                                           lease_device_bind);

  return lease_device;
}

static void
meta_wayland_drm_lease_manager_add_device (MetaKmsDevice              *kms_device,
                                           MetaWaylandDrmLeaseManager *lease_manager)
{
  g_autoptr (MetaWaylandDrmLeaseDevice) lease_device = NULL;

  lease_device = meta_wayland_drm_lease_device_new (lease_manager, kms_device);
  g_hash_table_insert (lease_manager->devices,
                       kms_device,
                       g_steal_pointer (&lease_device));
}

static void
on_device_added (MetaDrmLeaseManager        *drm_lease_manager,
                 MetaKmsDevice              *kms_device,
                 MetaWaylandDrmLeaseManager *lease_manager)
{
  meta_wayland_drm_lease_manager_add_device (kms_device, lease_manager);
}

static void
on_device_removed (MetaDrmLeaseManager        *drm_lease_manager,
                   MetaKmsDevice              *kms_device,
                   MetaWaylandDrmLeaseManager *lease_manager)
{
  MetaWaylandDrmLeaseDevice *lease_device;

  lease_device = g_hash_table_lookup (lease_manager->devices, kms_device);
  g_return_if_fail (lease_device != NULL);

  wl_global_remove (lease_device->global);
  g_hash_table_remove (lease_manager->devices, kms_device);
}

static void
send_pending_on_device_bind_events (MetaWaylandDrmLeaseManager *lease_manager,
                                    MetaWaylandDrmLeaseDevice  *lease_device)
{
  GList *l;

  for (l = lease_device->pending_resources; l;)
    {
      struct wl_resource *resource = l->data;
      struct wl_client *client = resource->client;
      GList *l_next = l->next;

      if (send_on_device_bind_events (client, lease_device, resource))
        {
          lease_device->pending_resources =
            g_list_remove_link (lease_device->pending_resources, l);
          lease_device->resources =
            g_list_insert_before_link (lease_device->resources,
                                       lease_device->resources,
                                       l);
        }

      l = l_next;
    }
}

static void
on_active_session_changed (MetaLauncher *launcher,
                           GParamSpec   *pspec,
                           gpointer      user_data)
{
  MetaWaylandDrmLeaseManager *lease_manager = user_data;
  MetaWaylandDrmLeaseDevice *lease_device;
  GHashTableIter iter;

  if (!meta_launcher_is_session_active (launcher))
    return;

  g_hash_table_iter_init (&iter, lease_manager->devices);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &lease_device))
    send_pending_on_device_bind_events (lease_manager, lease_device);
}

static void
on_connector_added (MetaDrmLeaseManager        *drm_lease_manager,
                    MetaKmsConnector           *kms_connector,
                    gboolean                    is_last_connector_update,
                    MetaWaylandDrmLeaseManager *lease_manager)
{
  MetaWaylandDrmLeaseConnector *lease_connector;
  MetaWaylandDrmLeaseDevice *lease_device;
  MetaKmsDevice *kms_device;
  GList *l;

  kms_device = meta_kms_connector_get_device (kms_connector);
  lease_device = g_hash_table_lookup (lease_manager->devices, kms_device);
  g_return_if_fail (lease_device != NULL);

  meta_wayland_drm_lease_device_add_connector (kms_connector, lease_device);
  lease_connector = g_hash_table_lookup (lease_device->connectors,
                                         kms_connector);
  g_return_if_fail (lease_connector != NULL);

  for (l = lease_device->resources; l; l = l->next)
    {
      struct wl_resource *resource = l->data;

      if (wl_resource_get_user_data (resource) == lease_device)
        send_new_connector_resource (lease_device, resource, lease_connector);
    }

  if (is_last_connector_update)
    {
      g_list_foreach (lease_device->resources,
                      (GFunc) wp_drm_lease_device_v1_send_done,
                      NULL);
    }
}

static void
on_connector_removed (MetaDrmLeaseManager        *drm_lease_manager,
                      MetaKmsConnector           *kms_connector,
                      gboolean                    is_last_connector_update,
                      MetaWaylandDrmLeaseManager *lease_manager)
{
  MetaWaylandDrmLeaseConnector *lease_connector;
  MetaWaylandDrmLeaseDevice *lease_device;
  MetaKmsDevice *kms_device;

  kms_device = meta_kms_connector_get_device (kms_connector);
  lease_device = g_hash_table_lookup (lease_manager->devices, kms_device);
  g_return_if_fail (lease_device != NULL);

  lease_connector = g_hash_table_lookup (lease_device->connectors,
                                         kms_connector);
  g_return_if_fail (lease_connector != NULL);

  meta_wayland_drm_lease_connector_send_withdrawn (lease_connector);
  g_hash_table_remove (lease_device->connectors, kms_connector);

  if (is_last_connector_update)
    {
      g_list_foreach (lease_device->resources,
                      (GFunc) wp_drm_lease_device_v1_send_done,
                      NULL);
    }
}

static MetaWaylandDrmLeaseManager *
meta_wayland_drm_lease_manager_new (MetaWaylandCompositor *compositor)
{
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaLauncher *launcher = meta_backend_get_launcher (backend);
  MetaWaylandDrmLeaseManager *lease_manager;
  MetaDrmLeaseManager *drm_lease_manager;

  if (!META_IS_BACKEND_NATIVE (backend))
    return NULL;

  lease_manager = g_new0 (MetaWaylandDrmLeaseManager, 1);
  lease_manager->compositor = compositor;
  lease_manager->devices =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) meta_wayland_drm_lease_device_release);

  drm_lease_manager =
    drm_lease_manager_from_lease_manager (lease_manager);

  g_list_foreach (meta_drm_lease_manager_get_devices (drm_lease_manager),
                  (GFunc) meta_wayland_drm_lease_manager_add_device,
                  lease_manager);

  lease_manager->device_added_handler_id =
    g_signal_connect (drm_lease_manager, "device-added",
                      G_CALLBACK (on_device_added),
                      lease_manager);
  lease_manager->device_removed_handler_id =
    g_signal_connect (drm_lease_manager, "device-removed",
                      G_CALLBACK (on_device_removed),
                      lease_manager);
  lease_manager->connector_added_handler_id =
    g_signal_connect (drm_lease_manager, "connector-added",
                      G_CALLBACK (on_connector_added),
                      lease_manager);
  lease_manager->connector_removed_handler_id =
    g_signal_connect (drm_lease_manager, "connector-removed",
                      G_CALLBACK (on_connector_removed),
                      lease_manager);

  if (launcher)
    {
      g_signal_connect (launcher, "notify::session-active",
                        G_CALLBACK (on_active_session_changed),
                        lease_manager);
    }

  return lease_manager;
}

static void
meta_wayland_drm_lease_manager_free (gpointer data)
{
  MetaWaylandDrmLeaseManager *lease_manager = data;
  MetaDrmLeaseManager *drm_lease_manager =
    drm_lease_manager_from_lease_manager (lease_manager);

  g_clear_signal_handler (&lease_manager->device_added_handler_id,
                          drm_lease_manager);
  g_clear_signal_handler (&lease_manager->device_removed_handler_id,
                          drm_lease_manager);
  g_clear_signal_handler (&lease_manager->connector_added_handler_id,
                          drm_lease_manager);
  g_clear_signal_handler (&lease_manager->connector_removed_handler_id,
                          drm_lease_manager);
  g_clear_pointer (&lease_manager->devices, g_hash_table_unref);
  g_list_foreach (lease_manager->leases,
                  (GFunc) meta_wayland_drm_lease_release,
                  NULL);
  g_free (lease_manager);
}

void
meta_wayland_drm_lease_manager_init (MetaWaylandCompositor *compositor)
{
  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-drm-lease",
                          meta_wayland_drm_lease_manager_new (compositor),
                          meta_wayland_drm_lease_manager_free);
}
