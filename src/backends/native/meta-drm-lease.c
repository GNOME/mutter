/*
 * Copyright (C) 2023 Red Hat
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

#include "backends/native/meta-drm-lease.h"

#include <glib.h>

#include "backends/meta-logical-monitor-private.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc-private.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-output-kms.h"

enum
{
  PROP_0,

  PROP_MANAGER_BACKEND,

  N_PROPS_MANAGER,
};

static GParamSpec *props_manager[N_PROPS_MANAGER] = { NULL };

enum
{
  MANAGER_DEVICE_ADDED,
  MANAGER_DEVICE_REMOVED,
  MANAGER_CONNECTOR_ADDED,
  MANAGER_CONNECTOR_REMOVED,

  N_SIGNALS_MANAGER,
};

static guint signals_manager[N_SIGNALS_MANAGER] = { 0 };

enum
{
  LEASE_REVOKED,

  N_SIGNALS_LEASE,
};

static guint signals_lease[N_SIGNALS_LEASE] = { 0 };

struct _MetaDrmLeaseManager
{
  GObject parent;

  MetaBackend *backend;

  gulong resources_changed_handler_id;
  gulong lease_changed_handler_id;
  gulong monitors_changed_handler_id;

  /* MetaKmsDevice *kms_device */
  GList *devices;
  /* MetaKmsConnector *kms_connector */
  GList *connectors;
  /* Key:   uint32_t lessee_id
   * Value: MetaDrmLease *lease
   */
  GHashTable *leases;
  /* Key:   MetaKmsConnector *kms_connector
   * Value: MetaDrmLease *lease
   */
  GHashTable *leased_connectors;

  gboolean is_paused;
};

G_DEFINE_TYPE (MetaDrmLeaseManager, meta_drm_lease_manager, G_TYPE_OBJECT)

typedef struct _LeasingKmsAssignment
{
  MetaKmsConnector *connector;
  MetaKmsCrtc *crtc;
  MetaKmsPlane *primary_plane;
  MetaKmsPlane *cursor_plane;
} LeasingKmsAssignment;

struct _MetaDrmLease
{
  GObject parent;

  uint32_t lessee_id;
  int fd;
  MetaKmsDevice *kms_device;
  GList *assignments;
};

G_DEFINE_TYPE (MetaDrmLease, meta_drm_lease, G_TYPE_OBJECT)

static MetaKmsCrtc *
find_crtc_to_lease (MetaKmsConnector *kms_connector)
{
  MetaKmsDevice *device = meta_kms_connector_get_device (kms_connector);
  const MetaKmsConnectorState *connector_state =
    meta_kms_connector_get_current_state (kms_connector);
  GList *l;

  for (l = meta_kms_device_get_crtcs (device); l; l = l->next)
    {
      MetaKmsCrtc *kms_crtc = l->data;
      MetaCrtcKms *crtc_kms = meta_crtc_kms_from_kms_crtc (kms_crtc);
      uint32_t crtc_idx;

      if (meta_crtc_is_leased (META_CRTC (crtc_kms)))
        continue;

      if (meta_crtc_get_outputs (META_CRTC (crtc_kms)) != NULL)
        continue;

      crtc_idx = meta_kms_crtc_get_idx (kms_crtc);
      if (!(connector_state->common_possible_crtcs & (1 << crtc_idx)))
        continue;

      return kms_crtc;
    }

  return NULL;
}

static gboolean
is_plane_assigned (MetaKmsDevice *kms_device,
                   MetaKmsPlane  *kms_plane)
{
  GList *l;

  for (l = meta_kms_device_get_crtcs (kms_device); l; l = l->next)
    {
      MetaKmsCrtc *kms_crtc = l->data;
      MetaCrtcKms *crtc_kms = meta_crtc_kms_from_kms_crtc (kms_crtc);

      if (meta_crtc_kms_get_assigned_primary_plane (crtc_kms) == kms_plane)
        return TRUE;
    }

  return FALSE;
}

static MetaKmsPlane *
find_plane_to_lease (MetaKmsCrtc      *kms_crtc,
                     MetaKmsPlaneType  plane_type)
{
  MetaKmsDevice *kms_device = meta_kms_crtc_get_device (kms_crtc);
  GList *l;

  for (l = meta_kms_device_get_planes (kms_device); l; l = l->next)
    {
      MetaKmsPlane *kms_plane = l->data;

      if (meta_kms_plane_get_plane_type (kms_plane) != plane_type)
        continue;

      if (!meta_kms_plane_is_usable_with (kms_plane, kms_crtc))
        continue;

      if (is_plane_assigned (kms_device, kms_plane))
        continue;

      return kms_plane;
    }

  return NULL;
}

static gboolean
is_connector_configured_for_lease (MetaKmsConnector *connector)
{
  const MetaKmsConnectorState *connector_state;
  MetaOutputKms *output_kms;
  MetaMonitor *monitor;

  connector_state = meta_kms_connector_get_current_state (connector);
  if (!connector_state)
    return FALSE;

  output_kms = meta_output_kms_from_kms_connector (connector);
  if (!output_kms)
    return FALSE;

  monitor = meta_output_get_monitor (META_OUTPUT (output_kms));
  return meta_monitor_is_for_lease (monitor);
}

static gboolean
is_connector_for_lease (MetaKmsConnector *connector)
{
  return meta_kms_connector_is_non_desktop (connector) ||
         is_connector_configured_for_lease (connector);
}

static gboolean
find_resources_to_lease (MetaDrmLeaseManager  *lease_manager,
                         MetaKmsDevice        *kms_device,
                         GList                *connectors,
                         GList               **out_assignments,
                         GList               **out_crtcs,
                         GList               **out_planes,
                         GError              **error)
{
  MetaKms *kms =
    meta_backend_native_get_kms (META_BACKEND_NATIVE (lease_manager->backend));
  g_autoptr (GList) assignments = NULL;
  g_autoptr (GList) crtcs = NULL;
  g_autoptr (GList) planes = NULL;
  GList *available_devices;
  GList *available_connectors;
  GList *l;

  if (!kms_device)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Cannot create lease without device");
      return FALSE;
    }

  if (!connectors)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Cannot create lease without connectors");
      return FALSE;
    }

  available_devices = meta_kms_get_devices (kms);
  if (!g_list_find (available_devices, kms_device))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Failed to find KMS device %s",
                   meta_kms_device_get_path (kms_device));
      return FALSE;
    }

  available_connectors = meta_kms_device_get_connectors (kms_device);

  for (l = connectors; l; l = l->next)
    {
      MetaKmsConnector *connector = l->data;
      MetaKmsDevice *connector_device;

      if (!g_list_find (available_connectors, connector) ||
          !is_connector_for_lease (connector))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                       "Failed to find connector %u (%s)",
                       meta_kms_connector_get_id (connector),
                       meta_kms_device_get_path (kms_device));
          return FALSE;
        }

      connector_device = meta_kms_connector_get_device (connector);
      if (connector_device != kms_device)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                       "Cannot create lease on multiple devices");
          return FALSE;
        }
    }

  for (l = connectors; l; l = l->next)
    {
      MetaKmsConnector *connector = l->data;
      LeasingKmsAssignment *assignment;
      MetaKmsCrtc *crtc;
      MetaKmsPlane *primary_plane;
      MetaKmsPlane *cursor_plane;

      crtc = find_crtc_to_lease (connector);
      if (!crtc)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                       "Failed to find CRTC to lease with connector %u (%s)",
                       meta_kms_connector_get_id (connector),
                       meta_kms_device_get_path (kms_device));
          return FALSE;
        }

      crtcs = g_list_append (crtcs, crtc);

      primary_plane = find_plane_to_lease (crtc, META_KMS_PLANE_TYPE_PRIMARY);
      if (!primary_plane)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                       "Failed to find primary plane "
                       "to lease with connector %u (%s)",
                       meta_kms_connector_get_id (connector),
                       meta_kms_device_get_path (kms_device));
          return FALSE;
        }

      planes = g_list_append (planes, primary_plane);

      cursor_plane = find_plane_to_lease (crtc, META_KMS_PLANE_TYPE_CURSOR);
      if (!cursor_plane)
        {
          g_warning ("Failed to find cursor plane "
                     "to lease with connector %u (%s)",
                     meta_kms_connector_get_id (connector),
                     meta_kms_device_get_path (kms_device));
        }
      else
        {
          planes = g_list_append (planes, cursor_plane);
        }

      assignment = g_new0 (LeasingKmsAssignment, 1);
      assignment->connector = connector;
      assignment->crtc = crtc;
      assignment->primary_plane = primary_plane;
      assignment->cursor_plane = cursor_plane;

      assignments = g_list_append (assignments, assignment);
    }

  *out_assignments = g_steal_pointer (&assignments);
  *out_crtcs = g_steal_pointer (&crtcs);
  *out_planes = g_steal_pointer (&planes);
  return TRUE;
}

uint32_t
meta_drm_lease_get_id (MetaDrmLease *lease)
{
  return lease->lessee_id;
}

int
meta_drm_lease_steal_fd (MetaDrmLease *lease)
{
  int fd = lease->fd;
  lease->fd = -1;
  return fd;
}

gboolean
meta_drm_lease_is_active (MetaDrmLease *lease)
{
  return lease->lessee_id != 0;
}

static void
meta_drm_lease_assign (MetaDrmLease *lease)
{
  GList *l;

  for (l = lease->assignments; l; l = l->next)
    {
      LeasingKmsAssignment *assignment = l->data;
      MetaCrtcKms *crtc_kms = meta_crtc_kms_from_kms_crtc (assignment->crtc);

      meta_kms_crtc_set_is_leased (assignment->crtc, TRUE);
      meta_crtc_kms_assign_planes (crtc_kms,
                                   assignment->primary_plane,
                                   assignment->cursor_plane);
    }
}

static void
meta_drm_lease_unassign (MetaDrmLease *lease)
{
  GList *l;

  for (l = lease->assignments; l; l = l->next)
    {
      LeasingKmsAssignment *assignment = l->data;
      MetaCrtcKms *crtc_kms = meta_crtc_kms_from_kms_crtc (assignment->crtc);

      meta_kms_crtc_set_is_leased (assignment->crtc, FALSE);
      meta_crtc_kms_assign_planes (crtc_kms, NULL, NULL);
    }
}

static void
mark_revoked (MetaDrmLease *lease)
{
  meta_drm_lease_unassign (lease);

  g_signal_emit (lease, signals_lease[LEASE_REVOKED], 0);
  lease->lessee_id = 0;
}

void
meta_drm_lease_revoke (MetaDrmLease *lease)
{
  g_autoptr (GError) error = NULL;

  if (!lease->lessee_id)
    return;

  if (!meta_kms_device_revoke_lease (lease->kms_device, lease->lessee_id, &error))
    {
      g_warning ("Failed to revoke DRM lease on %s: %s",
                 meta_kms_device_get_path (lease->kms_device),
                 error->message);
      return;
    }

  mark_revoked (lease);
}

static void
meta_drm_lease_dispose (GObject *object)
{
  MetaDrmLease *lease = META_DRM_LEASE (object);

  g_clear_object (&lease->kms_device);

  if (lease->assignments)
    {
      g_list_free_full (lease->assignments, g_free);
      lease->assignments = NULL;
    }

  G_OBJECT_CLASS (meta_drm_lease_parent_class)->dispose (object);
}

static void
meta_drm_lease_finalize (GObject *object)
{
  MetaDrmLease *lease = META_DRM_LEASE (object);

  close (lease->fd);

  G_OBJECT_CLASS (meta_drm_lease_parent_class)->finalize (object);
}

static void
meta_drm_lease_class_init (MetaDrmLeaseClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_drm_lease_dispose;
  object_class->finalize = meta_drm_lease_finalize;

  signals_lease[LEASE_REVOKED] =
    g_signal_new ("revoked",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_drm_lease_init (MetaDrmLease *lease)
{
}

static void
set_connectors_as_leased (MetaDrmLeaseManager *lease_manager,
                          MetaDrmLease        *lease)
{
  g_autoptr (GList) connectors = NULL;
  GList *l;

  for (l = lease->assignments; l; l = l->next)
    {
      LeasingKmsAssignment *assignment = l->data;
      MetaKmsConnector *kms_connector = assignment->connector;

      if (g_list_find (lease_manager->connectors, kms_connector))
        {
          lease_manager->connectors = g_list_remove (lease_manager->connectors,
                                                     kms_connector);
          g_hash_table_insert (lease_manager->leased_connectors, kms_connector,
                               lease);
          connectors = g_list_append (connectors, kms_connector);
        }
    }

  for (l = connectors; l; l = l->next)
    {
      MetaKmsConnector *connector = l->data;
      gboolean is_last_connector_update = (l->next == NULL);

      g_signal_emit (lease_manager, signals_manager[MANAGER_CONNECTOR_REMOVED],
                     0, connector, is_last_connector_update);
    }
}

static void
set_connectors_as_available (MetaDrmLeaseManager *lease_manager,
                             MetaDrmLease        *lease)
{
  g_autoptr (GList) connectors = NULL;
  GList *l;

  for (l = lease->assignments; l; l = l->next)
    {
      LeasingKmsAssignment *assignment = l->data;
      MetaKmsConnector *kms_connector = assignment->connector;

      if (g_hash_table_steal (lease_manager->leased_connectors, kms_connector))
        {
          lease_manager->connectors = g_list_append (lease_manager->connectors,
                                                     kms_connector);
          connectors = g_list_append (connectors, kms_connector);
        }
    }

  for (l = connectors; l; l = l->next)
    {
      MetaKmsConnector *kms_connector = l->data;
      gboolean is_last_connector_update = (l->next == NULL);

      g_signal_emit (lease_manager, signals_manager[MANAGER_CONNECTOR_ADDED],
                     0, kms_connector, is_last_connector_update);
    }
}

static void
on_lease_revoked (MetaDrmLease        *lease,
                  MetaDrmLeaseManager *lease_manager)
{
  g_signal_handlers_disconnect_by_func (lease,
                                        on_lease_revoked,
                                        lease_manager);

  set_connectors_as_available (lease_manager, lease);

  g_hash_table_remove (lease_manager->leases,
                       GUINT_TO_POINTER (lease->lessee_id));
}

MetaDrmLease *
meta_drm_lease_manager_lease_connectors (MetaDrmLeaseManager  *lease_manager,
                                         MetaKmsDevice        *kms_device,
                                         GList                *connectors,
                                         GError              **error)
{
  MetaDrmLease *lease;
  g_autoptr (GList) assignments = NULL;
  g_autoptr (GList) crtcs = NULL;
  g_autoptr (GList) planes = NULL;
  int fd;
  uint32_t lessee_id;

  if (!find_resources_to_lease (lease_manager,
                                kms_device, connectors,
                                &assignments, &crtcs, &planes,
                                error))
    return NULL;

  if (!meta_kms_device_lease_objects (kms_device,
                                      connectors, crtcs, planes,
                                      &fd, &lessee_id,
                                      error))
    return NULL;

  lease = g_object_new (META_TYPE_DRM_LEASE, NULL);
  lease->lessee_id = lessee_id;
  lease->fd = fd;
  lease->kms_device = g_object_ref (kms_device);
  lease->assignments = g_steal_pointer (&assignments);

  meta_drm_lease_assign (lease);

  g_signal_connect_after (lease, "revoked", G_CALLBACK (on_lease_revoked),
                          lease_manager);

  set_connectors_as_leased (lease_manager, lease);

  g_hash_table_insert (lease_manager->leases,
                       GUINT_TO_POINTER (lessee_id), g_object_ref (lease));

  return lease;
}

GList *
meta_drm_lease_manager_get_devices (MetaDrmLeaseManager *lease_manager)
{
  return lease_manager->devices;
}

GList *
meta_drm_lease_manager_get_connectors (MetaDrmLeaseManager *lease_manager,
                                       MetaKmsDevice       *kms_device)
{
  return g_list_copy (lease_manager->connectors);
}

MetaDrmLease *
meta_drm_lease_manager_get_lease_from_connector (MetaDrmLeaseManager *lease_manager,
                                                 MetaKmsConnector    *kms_connector)
{
  return g_hash_table_lookup (lease_manager->leased_connectors, kms_connector);
}



MetaDrmLease *
meta_drm_lease_manager_get_lease_from_id (MetaDrmLeaseManager *lease_manager,
                                          uint32_t             lessee_id)
{
  return g_hash_table_lookup (lease_manager->leases,
                              GUINT_TO_POINTER (lessee_id));
}

static void
update_devices (MetaDrmLeaseManager  *lease_manager,
                GList               **added_devices_out,
                GList               **removed_devices_out)
{
  MetaKms *kms =
    meta_backend_native_get_kms (META_BACKEND_NATIVE (lease_manager->backend));
  g_autoptr (GList) added_devices = NULL;
  GList *new_devices;
  GList *l;

  new_devices = g_list_copy (meta_kms_get_devices (kms));

  for (l = new_devices; l; l = l->next)
    {
      MetaKmsDevice *kms_device = l->data;

      if (g_list_find (lease_manager->devices, kms_device))
        {
          lease_manager->devices = g_list_remove (lease_manager->devices,
                                                  kms_device);
        }
      else
        {
          added_devices = g_list_append (added_devices, kms_device);
        }
    }

  *removed_devices_out = g_steal_pointer (&lease_manager->devices);
  *added_devices_out = g_steal_pointer (&added_devices);
  lease_manager->devices = new_devices;
}

static void
update_connectors (MetaDrmLeaseManager  *lease_manager,
                   GList               **added_connectors_out,
                   GList               **removed_connectors_out,
                   GList               **leases_to_revoke_out)
{
  MetaKms *kms =
    meta_backend_native_get_kms (META_BACKEND_NATIVE (lease_manager->backend));
  GList *new_connectors = NULL;
  GHashTable *new_leased_connectors;
  MetaDrmLease *lease = NULL;
  GList *l;
  GList *o;
  g_autoptr (GList) added_connectors = NULL;
  g_autoptr (GList) removed_connectors = NULL;
  g_autoptr (GList) leases_to_revoke = NULL;
  MetaKmsConnector *kms_connector;
  GHashTableIter iter;

  new_leased_connectors =
    g_hash_table_new_similar (lease_manager->leased_connectors);

  if (lease_manager->is_paused)
    goto scanned_resources;

  for (l = meta_kms_get_devices (kms); l; l = l->next)
    {
      MetaKmsDevice *kms_device = l->data;

      for (o = meta_kms_device_get_connectors (kms_device); o; o = o->next)
        {
          kms_connector = o->data;
          lease = NULL;

          if (!is_connector_for_lease (kms_connector))
            continue;

          if (g_list_find (lease_manager->connectors, kms_connector))
            {
              lease_manager->connectors =
                g_list_remove (lease_manager->connectors, kms_connector);
              new_connectors = g_list_append (new_connectors, kms_connector);
            }
          else if (g_hash_table_steal_extended (lease_manager->leased_connectors,
                                                kms_connector,
                                                NULL, (gpointer *) &lease))
            {
              g_hash_table_insert (new_leased_connectors, kms_connector, lease);
            }
          else
            {
              added_connectors = g_list_append (added_connectors, kms_connector);
              new_connectors = g_list_append (new_connectors, kms_connector);
            }
        }
    }

scanned_resources:

  g_hash_table_iter_init (&iter, lease_manager->leased_connectors);
  while (g_hash_table_iter_next (&iter, (gpointer *)&kms_connector, NULL))
    {
      lease = meta_drm_lease_manager_get_lease_from_connector (lease_manager,
                                                               kms_connector);
      if (lease && meta_drm_lease_is_active (lease))
        leases_to_revoke = g_list_append (leases_to_revoke, lease);
    }

  removed_connectors = g_steal_pointer (&lease_manager->connectors);
  lease_manager->connectors = new_connectors;

  g_clear_pointer (&lease_manager->leased_connectors, g_hash_table_unref);
  lease_manager->leased_connectors = new_leased_connectors;

  *added_connectors_out = g_steal_pointer (&added_connectors);
  *removed_connectors_out = g_steal_pointer (&removed_connectors);
  *leases_to_revoke_out = g_steal_pointer (&leases_to_revoke);
}

static void
update_resources (MetaDrmLeaseManager *lease_manager)
{
  g_autoptr (GList) added_devices = NULL;
  g_autoptr (GList) removed_devices = NULL;
  g_autoptr (GList) added_connectors = NULL;
  g_autoptr (GList) removed_connectors = NULL;
  g_autoptr (GList) leases_to_revoke = NULL;
  GList *l;

  update_devices (lease_manager, &added_devices, &removed_devices);
  update_connectors (lease_manager, &added_connectors, &removed_connectors,
                     &leases_to_revoke);

  for (l = added_devices; l; l = l->next)
    {
      MetaKmsDevice *kms_device = l->data;

      g_object_ref (kms_device);
      g_signal_emit (lease_manager, signals_manager[MANAGER_DEVICE_ADDED],
                     0, kms_device);
    }

  for (l = added_connectors; l; l = l->next)
    {
      MetaKmsConnector *kms_connector = l->data;
      gboolean is_last_connector_update = FALSE;

      if (g_list_length (removed_connectors) == 0 &&
          kms_connector == g_list_last (added_connectors)->data)
        is_last_connector_update = TRUE;

      g_object_ref (kms_connector);
      g_signal_emit (lease_manager, signals_manager[MANAGER_CONNECTOR_ADDED],
                     0, kms_connector, is_last_connector_update);
    }

  for (l = removed_connectors; l; l = l->next)
    {
      MetaKmsConnector *kms_connector = l->data;
      gboolean is_last_connector_update = FALSE;

      if (kms_connector == g_list_last (removed_connectors)->data)
        is_last_connector_update = TRUE;

      g_signal_emit (lease_manager, signals_manager[MANAGER_CONNECTOR_REMOVED],
                     0, kms_connector, is_last_connector_update);
      g_object_unref (kms_connector);
    }

  for (l = leases_to_revoke; l; l = l->next)
    {
      MetaDrmLease *lease = l->data;

      meta_drm_lease_revoke (lease);
    }

  for (l = removed_devices; l; l = l->next)
    {
      MetaKmsDevice *kms_device = l->data;

      g_signal_emit (lease_manager, signals_manager[MANAGER_DEVICE_REMOVED],
                     0, kms_device);
      g_object_unref (kms_device);
    }
}

static gboolean
did_lease_disappear (MetaDrmLease  *lease,
                     uint32_t      *lessees,
                     int            num_lessees,
                     MetaKmsDevice *kms_device)
{
  int i;

  if (lease->kms_device != kms_device)
    return FALSE;

  for (i = 0; i < num_lessees; i++)
    {
      if (lease->lessee_id == lessees[i])
        return FALSE;
    }

  return TRUE;
}

static void
update_leases (MetaDrmLeaseManager *lease_manager)
{
  MetaKms *kms =
    meta_backend_native_get_kms (META_BACKEND_NATIVE (lease_manager->backend));
  MetaDrmLease *lease;
  GList *l;
  g_autoptr (GList) disappeared_leases = NULL;

  for (l = meta_kms_get_devices (kms); l; l = l->next)
    {
      MetaKmsDevice *kms_device = l->data;
      g_autofree uint32_t *lessees = NULL;
      int num_lessees;
      g_autoptr (GError) error = NULL;
      GHashTableIter iter;

      if (!meta_kms_device_list_lessees (kms_device,
                                         &lessees, &num_lessees,
                                         &error))
        {
          g_warning ("Failed to list leases: %s", error->message);
          continue;
        }

      g_hash_table_iter_init (&iter, lease_manager->leases);
      while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&lease))
        {
          if (did_lease_disappear (lease, lessees, num_lessees, kms_device))
            disappeared_leases = g_list_append (disappeared_leases, lease);
        }
    }

  for (l = disappeared_leases; l; l = l->next)
    {
      lease = l->data;

      mark_revoked (lease);
    }
}

static void
on_resources_changed (MetaKms                *kms,
                      MetaKmsResourceChanges  changes,
                      MetaDrmLeaseManager    *lease_manager)
{
  if (changes != META_KMS_RESOURCE_CHANGE_FULL)
    return;

  update_resources (lease_manager);
}

static void
on_lease_changed (MetaKms             *kms,
                  MetaDrmLeaseManager *lease_manager)
{
  update_leases (lease_manager);
}

void
meta_drm_lease_manager_pause (MetaDrmLeaseManager *lease_manager)
{
  lease_manager->is_paused = TRUE;
  update_resources (lease_manager);
}

void
meta_drm_lease_manager_resume (MetaDrmLeaseManager *lease_manager)
{
  lease_manager->is_paused = FALSE;
}

static void
on_prepare_shutdown (MetaBackend         *backend,
                     MetaDrmLeaseManager *lease_manager)
{
  MetaKms *kms =
    meta_backend_native_get_kms (META_BACKEND_NATIVE (lease_manager->backend));
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  g_clear_signal_handler (&lease_manager->resources_changed_handler_id, kms);
  g_clear_signal_handler (&lease_manager->lease_changed_handler_id, kms);
  g_clear_signal_handler (&lease_manager->monitors_changed_handler_id,
                          monitor_manager);

  g_list_free_full (g_steal_pointer (&lease_manager->devices), g_object_unref);
  g_list_free_full (g_steal_pointer (&lease_manager->connectors),
                    g_object_unref);
  g_clear_pointer (&lease_manager->leases, g_hash_table_unref);
  g_clear_pointer (&lease_manager->leased_connectors, g_hash_table_unref);
}

static void
meta_drm_lease_manager_constructed (GObject *object)
{
  MetaDrmLeaseManager *lease_manager = META_DRM_LEASE_MANAGER (object);
  MetaKms *kms =
    meta_backend_native_get_kms (META_BACKEND_NATIVE (lease_manager->backend));
  MetaBackend *backend = meta_kms_get_backend (kms);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  g_signal_connect (backend, "prepare-shutdown",
                    G_CALLBACK (on_prepare_shutdown),
                    lease_manager);

  /* Connect to MetaKms::resources-changed using G_CONNECT_AFTER to make sure
   * MetaMonitorManager state is up to date. */
  lease_manager->resources_changed_handler_id =
    g_signal_connect_after (kms, "resources-changed",
                            G_CALLBACK (on_resources_changed),
                            lease_manager);
  lease_manager->lease_changed_handler_id =
    g_signal_connect (kms, "lease-changed",
                      G_CALLBACK (on_lease_changed),
                      lease_manager);
  lease_manager->monitors_changed_handler_id =
    g_signal_connect_swapped (monitor_manager, "monitors-changed-internal",
                              G_CALLBACK (update_resources),
                              lease_manager);

  lease_manager->leases =
    g_hash_table_new_full (NULL, NULL,
                           NULL,
                           (GDestroyNotify) g_object_unref);
  lease_manager->leased_connectors = g_hash_table_new (NULL, NULL);

  update_resources (lease_manager);

  G_OBJECT_CLASS (meta_drm_lease_manager_parent_class)->constructed (object);
}

static void
meta_drm_lease_manager_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  MetaDrmLeaseManager *lease_manager = META_DRM_LEASE_MANAGER (object);
  switch (prop_id)
    {
    case PROP_MANAGER_BACKEND:
      lease_manager->backend = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_drm_lease_manager_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  MetaDrmLeaseManager *lease_manager = META_DRM_LEASE_MANAGER (object);
  switch (prop_id)
    {
    case PROP_MANAGER_BACKEND:
      g_value_set_object (value, lease_manager->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
meta_drm_lease_manager_class_init (MetaDrmLeaseManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_drm_lease_manager_constructed;
  object_class->set_property = meta_drm_lease_manager_set_property;
  object_class->get_property = meta_drm_lease_manager_get_property;

  props_manager[PROP_MANAGER_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND_NATIVE,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class,
                                     N_PROPS_MANAGER, props_manager);

  signals_manager[MANAGER_DEVICE_ADDED] =
    g_signal_new ("device-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_KMS_DEVICE);

  signals_manager[MANAGER_DEVICE_REMOVED] =
    g_signal_new ("device-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_KMS_DEVICE);

  signals_manager[MANAGER_CONNECTOR_ADDED] =
    g_signal_new ("connector-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  META_TYPE_KMS_CONNECTOR,
                  G_TYPE_BOOLEAN);

  signals_manager[MANAGER_CONNECTOR_REMOVED] =
    g_signal_new ("connector-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  META_TYPE_KMS_CONNECTOR,
                  G_TYPE_BOOLEAN);
}

static void
meta_drm_lease_manager_init (MetaDrmLeaseManager *lease_manager)
{
}
