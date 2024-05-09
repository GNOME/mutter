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

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-drm-lease.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms.h"
#include "backends/edid.h"
#include "wayland/meta-wayland-private.h"

#include "drm-lease-v1-server-protocol.h"

struct _MetaWaylandDrmLeaseManager
{
  MetaWaylandCompositor *compositor;
  MetaDrmLeaseManager *drm_lease_manager;

  /* Key:   MetaKmsDevice *kms_device
   * Value: MetaWaylandDrmLeaseDevice *lease_device
   */
  GHashTable *devices;
};

typedef struct _MetaWaylandDrmLeaseDevice
{
  MetaWaylandDrmLeaseManager *lease_manager;

  struct wl_global *global;
  MetaKmsDevice *kms_device;

  GList *resources;
} MetaWaylandDrmLeaseDevice;

static void
meta_wayland_drm_lease_device_free (MetaWaylandDrmLeaseDevice *lease_device)
{
  g_object_unref (lease_device->kms_device);
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
wp_drm_lease_device_create_lease_request (struct wl_client   *client,
                                          struct wl_resource *resource,
                                          uint32_t            id)
{
}

static void
wp_drm_lease_device_release (struct wl_client   *client,
                             struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wp_drm_lease_device_v1_interface drm_lease_device_implementation = {
  wp_drm_lease_device_create_lease_request,
  wp_drm_lease_device_release,
};

static void
wp_drm_lease_device_destructor (struct wl_resource *resource)
{
  MetaWaylandDrmLeaseDevice *lease_device =
    wl_resource_get_user_data (resource);

  lease_device->resources = g_list_remove (lease_device->resources, resource);
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

  lease_device->resources = g_list_prepend (lease_device->resources, resource);
}

static MetaWaylandDrmLeaseDevice *
meta_wayland_drm_lease_device_new (MetaWaylandDrmLeaseManager *lease_manager,
                                   MetaKmsDevice              *kms_device)
{
  struct wl_display *wayland_display =
    meta_wayland_compositor_get_wayland_display (lease_manager->compositor);
  MetaWaylandDrmLeaseDevice *lease_device;

  lease_device = g_rc_box_new0 (MetaWaylandDrmLeaseDevice);
  lease_device->lease_manager = lease_manager;
  lease_device->kms_device = g_object_ref (kms_device);
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

static MetaWaylandDrmLeaseManager *
meta_wayland_drm_lease_manager_new (MetaWaylandCompositor *compositor)
{
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaBackendNative *backend_native;
  MetaKms *kms;
  MetaWaylandDrmLeaseManager *lease_manager;
  MetaDrmLeaseManager *drm_lease_manager;

  if (!META_IS_BACKEND_NATIVE (backend))
    return NULL;

  backend_native = META_BACKEND_NATIVE (backend);
  kms = meta_backend_native_get_kms (backend_native);
  drm_lease_manager = g_object_new (META_TYPE_DRM_LEASE_MANAGER,
                                    "meta-kms", kms,
                                    NULL);

  lease_manager = g_new0 (MetaWaylandDrmLeaseManager, 1);
  lease_manager->compositor = compositor;
  lease_manager->drm_lease_manager = drm_lease_manager;
  lease_manager->devices =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) meta_wayland_drm_lease_device_release);

  g_list_foreach (meta_drm_lease_manager_get_devices (drm_lease_manager),
                  (GFunc) meta_wayland_drm_lease_manager_add_device,
                  lease_manager);

  return lease_manager;
}

static void
meta_wayland_drm_lease_manager_free (gpointer data)
{
  MetaWaylandDrmLeaseManager *lease_manager = data;

  g_clear_pointer (&lease_manager->devices, g_hash_table_unref);
  g_clear_pointer (&lease_manager->drm_lease_manager, g_object_unref);
  g_free (lease_manager);
}

void
meta_wayland_drm_lease_manager_init (MetaWaylandCompositor *compositor)
{
  g_object_set_data_full (G_OBJECT (compositor), "-meta-wayland-drm-lease",
                          meta_wayland_drm_lease_manager_new (compositor),
                          meta_wayland_drm_lease_manager_free);
}
