/*
 * Copyright (C) 2026 Red Hat
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

#include "backends/native/meta-cast-kms-grant-broker.h"

#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-dbus-access-checker.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-cast-kms-grant-policy.h"
#include "backends/native/meta-kms-cast-grant.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms.h"
#include "core/util-private.h"

#define META_CAST_KMS_DBUS_SERVICE "org.gnome.Mutter.CastKms"
#define META_CAST_KMS_DBUS_PATH "/org/gnome/Mutter/CastKms"

#define PRONK_DBUS_SERVICE "io.github.halfline.Pronk1"

enum
{
  PROP_0,
  PROP_BACKEND,

  N_PROPS,
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaCastKmsBrokeredGrant MetaCastKmsBrokeredGrant;

struct _MetaCastKmsBrokeredGrant
{
  /* The broker owns every grant in its hash set. */
  MetaCastKmsGrantBroker *broker;
  MetaKmsCastGrant *grant;
  /* The grant pins this device for at least as long as this object exists. */
  MetaKmsDevice *kms_device;
  uint32_t connector_id;

  GSource *control_source;
  GSource *revoke_retry_source;
  guint name_watch_id;
};

struct _MetaCastKmsGrantBroker
{
  MetaDBusCastKmsSkeleton parent;

  /*
   * The backend owns the broker; keeping this weak avoids an ownership cycle.
   */
  MetaBackendNative *backend_native;
  MetaDbusAccessChecker *access_checker;
  GHashTable *grants;
  guint dbus_name_id;
  gulong prepare_shutdown_handler_id;
};

static void
meta_cast_kms_grant_broker_init_iface (MetaDBusCastKmsIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaCastKmsGrantBroker,
                         meta_cast_kms_grant_broker,
                         META_DBUS_TYPE_CAST_KMS_SKELETON,
                         G_IMPLEMENT_INTERFACE (
                           META_DBUS_TYPE_CAST_KMS,
                           meta_cast_kms_grant_broker_init_iface))

static gboolean
check_access (GDBusInterfaceSkeleton *skeleton,
              GDBusMethodInvocation  *invocation,
              gpointer                user_data)
{
  MetaCastKmsGrantBroker *broker = META_CAST_KMS_GRANT_BROKER (user_data);
  const char *sender = g_dbus_method_invocation_get_sender (invocation);

  if (broker->access_checker &&
      meta_dbus_access_checker_is_sender_allowed (broker->access_checker,
                                                  sender))
    return TRUE;

  g_dbus_method_invocation_return_error (invocation,
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_ACCESS_DENIED,
                                         "Access denied");
  return FALSE;
}

static MetaKmsDevice *
find_cast_kms_device (MetaCastKmsGrantBroker  *broker,
                      uint32_t                 device_major,
                      uint32_t                 device_minor,
                      GError                 **error)
{
  MetaKms *kms = meta_backend_native_get_kms (broker->backend_native);
  dev_t requested_device_id = makedev (device_major, device_minor);
  GList *l;

  if (major (requested_device_id) != device_major ||
      minor (requested_device_id) != device_minor)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Invalid DRM device number");
      return NULL;
    }

  for (l = meta_kms_get_devices (kms); l; l = l->next)
    {
      MetaKmsDevice *kms_device = l->data;
      struct stat status;

      if (stat (meta_kms_device_get_path (kms_device), &status) == -1 ||
          !S_ISCHR (status.st_mode) ||
          status.st_rdev != requested_device_id)
        continue;

      if (g_strcmp0 (meta_kms_device_get_driver_name (kms_device),
                     "castkms") != 0)
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_SUPPORTED,
                               "Requested DRM device is not CastKMS");
          return NULL;
        }

      return kms_device;
    }

  g_set_error (error,
               G_IO_ERROR,
               G_IO_ERROR_NOT_FOUND,
               "Mutter does not own DRM device %u:%u",
               device_major,
               device_minor);
  return NULL;
}

static void request_grant_revoke (MetaCastKmsBrokeredGrant *brokered_grant);

static void
clear_source (GSource **source)
{
  if (!*source)
    return;

  g_source_destroy (*source);
  g_clear_pointer (source, g_source_unref);
}

static gboolean
on_control_fd_hangup (int          fd,
                      GIOCondition condition,
                      gpointer     user_data)
{
  MetaCastKmsBrokeredGrant *brokered_grant = user_data;

  request_grant_revoke (brokered_grant);
  return G_SOURCE_REMOVE;
}

static gboolean
on_revoke_retry (gpointer user_data)
{
  MetaCastKmsBrokeredGrant *brokered_grant = user_data;

  /* The main context keeps the source alive while its callback is running. */
  g_clear_pointer (&brokered_grant->revoke_retry_source, g_source_unref);
  request_grant_revoke (brokered_grant);
  return G_SOURCE_REMOVE;
}

static void
on_client_name_vanished (GDBusConnection *connection,
                         const char      *name,
                         gpointer         user_data)
{
  MetaCastKmsBrokeredGrant *brokered_grant = user_data;

  g_clear_handle_id (&brokered_grant->name_watch_id, g_bus_unwatch_name);
  request_grant_revoke (brokered_grant);
}

static void
meta_cast_kms_brokered_grant_free (MetaCastKmsBrokeredGrant *brokered_grant)
{
  clear_source (&brokered_grant->control_source);
  clear_source (&brokered_grant->revoke_retry_source);
  if (brokered_grant->name_watch_id)
    g_bus_unwatch_name (brokered_grant->name_watch_id);

  g_clear_object (&brokered_grant->grant);
  g_free (brokered_grant);
}

static void
request_grant_revoke (MetaCastKmsBrokeredGrant *brokered_grant)
{
  g_autoptr (GError) error = NULL;

  clear_source (&brokered_grant->control_source);

  if (!meta_kms_cast_grant_revoke (brokered_grant->grant, &error))
    {
      const MetaKmsCastGrantInfo *info =
        meta_kms_cast_grant_get_info (brokered_grant->grant);

      g_warning ("CastKMS grant %u was revoked, but its device fd could not "
                 "be released; will retry: %s",
                 info->grant_id,
                 error ? error->message : "unknown error");
      if (!brokered_grant->revoke_retry_source)
        {
          brokered_grant->revoke_retry_source = g_timeout_source_new_seconds (1);
          g_source_set_callback (brokered_grant->revoke_retry_source,
                                 on_revoke_retry,
                                 brokered_grant,
                                 NULL);
          g_source_attach (brokered_grant->revoke_retry_source, NULL);
        }
      return;
    }

  g_hash_table_remove (brokered_grant->broker->grants, brokered_grant);
}

static MetaCastKmsBrokeredGrant *
meta_cast_kms_brokered_grant_new (MetaCastKmsGrantBroker  *broker,
                                  GDBusConnection         *connection,
                                  const char              *sender,
                                  MetaKmsDevice           *kms_device,
                                  uint32_t                 connector_id,
                                  MetaKmsCastGrant        *grant)
{
  MetaCastKmsBrokeredGrant *brokered_grant;
  int control_fd;

  brokered_grant = g_new0 (MetaCastKmsBrokeredGrant, 1);
  brokered_grant->broker = broker;
  brokered_grant->grant = g_object_ref (grant);
  brokered_grant->kms_device = kms_device;
  brokered_grant->connector_id = connector_id;

  control_fd = meta_kms_cast_grant_get_control_fd (grant);
  g_assert (control_fd >= 0);

  /*
   * Install the level-triggered hangup watch before replying. This also catches
   * a holder that closes its descriptor as soon as it receives the reply.
   */
  brokered_grant->control_source =
    g_unix_fd_source_new (control_fd,
                          G_IO_HUP | G_IO_ERR | G_IO_NVAL);
  g_source_set_callback (brokered_grant->control_source,
                         G_SOURCE_FUNC (on_control_fd_hangup),
                         brokered_grant,
                         NULL);
  g_source_attach (brokered_grant->control_source, NULL);

  brokered_grant->name_watch_id =
    g_bus_watch_name_on_connection (connection,
                                    sender,
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    NULL,
                                    on_client_name_vanished,
                                    brokered_grant,
                                    NULL);

  return brokered_grant;
}

static gboolean
has_grant_for_connector (MetaCastKmsGrantBroker *broker,
                         MetaKmsDevice          *kms_device,
                         uint32_t                connector_id)
{
  GHashTableIter iter;
  MetaCastKmsBrokeredGrant *brokered_grant;

  g_hash_table_iter_init (&iter, broker->grants);
  while (g_hash_table_iter_next (&iter, (gpointer *) &brokered_grant, NULL))
    {
      if (brokered_grant->kms_device == kms_device &&
          brokered_grant->connector_id == connector_id)
        return TRUE;
    }

  return FALSE;
}

static void
create_capture_grant (MetaCastKmsGrantBroker *broker,
                      GDBusMethodInvocation  *invocation,
                      const char             *sender,
                      uint32_t                device_major,
                      uint32_t                device_minor,
                      uint32_t                connector_id,
                      uint32_t                rights)
{
  GDBusConnection *connection =
    g_dbus_method_invocation_get_connection (invocation);
  g_autoptr (MetaKmsCastGrant) grant = NULL;
  g_autoptr (GUnixFDList) out_fd_list = NULL;
  g_autoptr (GError) error = NULL;
  g_autofd int holder_fd = -1;
  MetaKmsDevice *kms_device;
  const MetaKmsCastGrantInfo *info;
  MetaCastKmsBrokeredGrant *brokered_grant;
  int holder_index;

  if (!broker->backend_native)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation,
        G_DBUS_ERROR,
        G_DBUS_ERROR_FAILED,
        "CastKMS grant broker is shutting down");
      return;
    }

  kms_device = find_cast_kms_device (broker,
                                     device_major,
                                     device_minor,
                                     &error);
  if (!kms_device)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }

  if (has_grant_for_connector (broker, kms_device, connector_id))
    {
      g_dbus_method_invocation_return_error_literal (
        invocation,
        G_IO_ERROR,
        G_IO_ERROR_BUSY,
        "CastKMS connector already has a capture grant");
      return;
    }

  grant = meta_kms_cast_grant_new (kms_device,
                                   connector_id,
                                   rights,
                                   &error);
  if (!grant)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }

  holder_fd = meta_kms_cast_grant_steal_holder_fd (grant);
  info = meta_kms_cast_grant_get_info (grant);

  out_fd_list = g_unix_fd_list_new ();
  holder_index = g_unix_fd_list_append (out_fd_list, holder_fd, &error);
  if (holder_index == -1)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }
  brokered_grant = meta_cast_kms_brokered_grant_new (broker,
                                                     connection,
                                                     sender,
                                                     kms_device,
                                                     connector_id,
                                                     grant);
  g_hash_table_add (broker->grants, brokered_grant);

  meta_topic (META_DEBUG_DBUS,
              "Created normal CastKMS grant %u for %s",
              info->grant_id,
              sender);
  meta_dbus_cast_kms_complete_create_capture_grant (
    META_DBUS_CAST_KMS (broker),
    invocation,
    out_fd_list,
    g_variant_new_handle (holder_index),
    info->grant_id,
    info->output_index,
    info->rights,
    info->flags,
    info->initial_state,
    info->capture_uapi_major,
    info->capture_uapi_minor);
}

static gboolean
handle_create_capture_grant (MetaDBusCastKms       *object,
                             GDBusMethodInvocation *invocation,
                             GUnixFDList           *in_fd_list,
                             uint32_t               device_major,
                             uint32_t               device_minor,
                             uint32_t               connector_id,
                             uint16_t               profile)
{
  MetaCastKmsGrantBroker *broker = META_CAST_KMS_GRANT_BROKER (object);
  const char *sender;
  uint32_t rights;

  if (!broker->backend_native)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation,
        G_DBUS_ERROR,
        G_DBUS_ERROR_FAILED,
        "CastKMS grant broker is shutting down");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (connector_id == 0 ||
      !meta_cast_kms_grant_profile_get_rights (profile, &rights))
    {
      g_dbus_method_invocation_return_error_literal (
        invocation,
        G_DBUS_ERROR,
        G_DBUS_ERROR_INVALID_ARGS,
        "Invalid CastKMS grant request");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  sender = g_dbus_method_invocation_get_sender (invocation);
  create_capture_grant (broker,
                        invocation,
                        sender,
                        device_major,
                        device_minor,
                        connector_id,
                        rights);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
meta_cast_kms_grant_broker_init_iface (MetaDBusCastKmsIface *iface)
{
  iface->handle_create_capture_grant = handle_create_capture_grant;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaCastKmsGrantBroker *broker = user_data;
  MetaContext *context;
  g_autoptr (GError) error = NULL;

  if (!broker->backend_native)
    return;

  context = meta_backend_get_context (META_BACKEND (broker->backend_native));
  g_clear_object (&broker->access_checker);
  broker->access_checker = meta_dbus_access_checker_new (connection, context);
  meta_dbus_access_checker_allow_sender (broker->access_checker,
                                         PRONK_DBUS_SERVICE);

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (broker),
                                         connection,
                                         META_CAST_KMS_DBUS_PATH,
                                         &error))
    g_warning ("Failed to export CastKMS grant broker: %s", error->message);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  meta_topic (META_DEBUG_DBUS, "Acquired name %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  MetaCastKmsGrantBroker *broker = user_data;
  g_autoptr (GList) grants = NULL;
  GList *l;

  meta_topic (META_DEBUG_DBUS, "Lost or failed to acquire name %s", name);
  if (!broker->grants)
    return;

  grants = g_hash_table_get_keys (broker->grants);
  for (l = grants; l; l = l->next)
    request_grant_revoke (l->data);
}

static void
on_prepare_shutdown (MetaBackend            *backend,
                     MetaCastKmsGrantBroker *broker)
{
  /* Stop serving requests and revoke grants while the KMS thread is alive. */
  g_object_run_dispose (G_OBJECT (broker));
}

static void
meta_cast_kms_grant_broker_constructed (GObject *object)
{
  MetaCastKmsGrantBroker *broker = META_CAST_KMS_GRANT_BROKER (object);

  G_OBJECT_CLASS (meta_cast_kms_grant_broker_parent_class)->constructed (
    object);

  g_signal_connect (broker, "g-authorize-method",
                    G_CALLBACK (check_access), broker);

  broker->grants =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           (GDestroyNotify) meta_cast_kms_brokered_grant_free,
                           NULL);
  broker->prepare_shutdown_handler_id =
    g_signal_connect (broker->backend_native,
                      "prepare-shutdown",
                      G_CALLBACK (on_prepare_shutdown),
                      broker);
  broker->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    META_CAST_KMS_DBUS_SERVICE,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    broker,
                    NULL);
}

static void
meta_cast_kms_grant_broker_dispose (GObject *object)
{
  MetaCastKmsGrantBroker *broker = META_CAST_KMS_GRANT_BROKER (object);

  if (broker->backend_native)
    g_clear_signal_handler (&broker->prepare_shutdown_handler_id,
                            broker->backend_native);
  broker->backend_native = NULL;
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (broker));
  g_clear_handle_id (&broker->dbus_name_id, g_bus_unown_name);
  g_clear_pointer (&broker->grants, g_hash_table_unref);
  g_clear_object (&broker->access_checker);

  G_OBJECT_CLASS (meta_cast_kms_grant_broker_parent_class)->dispose (object);
}

static void
meta_cast_kms_grant_broker_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  MetaCastKmsGrantBroker *broker = META_CAST_KMS_GRANT_BROKER (object);

  switch (prop_id)
    {
    case PROP_BACKEND:
      broker->backend_native = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_cast_kms_grant_broker_class_init (MetaCastKmsGrantBrokerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_cast_kms_grant_broker_constructed;
  object_class->dispose = meta_cast_kms_grant_broker_dispose;
  object_class->set_property = meta_cast_kms_grant_broker_set_property;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND_NATIVE,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
}

static void
meta_cast_kms_grant_broker_init (MetaCastKmsGrantBroker *broker)
{
}

MetaCastKmsGrantBroker *
meta_cast_kms_grant_broker_new (MetaBackendNative *backend_native)
{
  g_return_val_if_fail (META_IS_BACKEND_NATIVE (backend_native), NULL);

  return g_object_new (META_TYPE_CAST_KMS_GRANT_BROKER,
                       "backend", backend_native,
                       NULL);
}
