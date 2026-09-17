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
#include <xf86drm.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-dbus-access-checker.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-device-pool.h"
#include "backends/native/meta-kms-capture-grant.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-monitor-control.h"
#include "backends/native/meta-kms-renderer-control.h"
#include "backends/native/meta-kms-update.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-renderer-native-private.h"
#include "core/util-private.h"

#define META_CAST_KMS_DBUS_SERVICE "org.gnome.Mutter.CastKms"
#define META_CAST_KMS_DBUS_PATH "/org/gnome/Mutter/CastKms"

#define PRONK_DBUS_SERVICE "io.github.pronkproject.Pronk1"
#define MAX_RENDERER_ENDPOINTS 4

enum
{
  PROP_0,
  PROP_BACKEND,

  N_PROPS,
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaCastKmsDisplaySession MetaCastKmsDisplaySession;

typedef struct
{
  uint64_t id;
  MetaKmsRendererControl *control;
} MetaCastKmsRendererEndpoint;

struct _MetaCastKmsDisplaySession
{
  /* The broker owns every session in its hash set. */
  MetaCastKmsGrantBroker *broker;
  MetaKmsCaptureGrant *capture_grant;
  MetaKmsMonitorControl *monitor_control;
  GPtrArray *renderer_controls;
  uint64_t last_renderer_id;
  uint64_t session_id;
  char *sender;
  /* The capture grant pins this device while this object exists. */
  MetaKmsDevice *kms_device;
  uint32_t crtc_id;
  uint32_t connector_id;
  gboolean revoking;

  GSource *capture_control_source;
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
  GHashTable *sessions;
  uint64_t last_session_id;
  char *pronk_name_owner;
  guint pronk_name_watch_id;
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

static void request_session_revoke (MetaCastKmsDisplaySession *session);

static void
meta_cast_kms_renderer_endpoint_free (MetaCastKmsRendererEndpoint *endpoint)
{
  meta_kms_renderer_control_free (endpoint->control);
  g_free (endpoint);
}

static uint64_t
add_renderer_control (MetaCastKmsDisplaySession *session,
                      MetaKmsRendererControl    *control)
{
  MetaCastKmsRendererEndpoint *endpoint;

  g_return_val_if_fail (session->last_renderer_id != G_MAXUINT64, 0);

  endpoint = g_new0 (MetaCastKmsRendererEndpoint, 1);
  endpoint->id = ++session->last_renderer_id;
  endpoint->control = control;
  g_ptr_array_add (session->renderer_controls, endpoint);
  return endpoint->id;
}

static gboolean
remove_renderer_control (MetaCastKmsDisplaySession *session,
                         uint64_t                   renderer_id)
{
  guint i;

  for (i = 0; i < session->renderer_controls->len; i++)
    {
      MetaCastKmsRendererEndpoint *endpoint =
        g_ptr_array_index (session->renderer_controls, i);

      if (endpoint->id == renderer_id)
        {
          g_ptr_array_remove_index (session->renderer_controls, i);
          return TRUE;
        }
    }

  return FALSE;
}

static void
revoke_sessions_for_sender (MetaCastKmsGrantBroker *broker,
                            const char             *sender)
{
  g_autoptr (GList) sessions = NULL;
  GList *l;

  if (!sender)
    return;

  sessions = g_hash_table_get_keys (broker->sessions);
  for (l = sessions; l; l = l->next)
    {
      MetaCastKmsDisplaySession *session = l->data;

      if (g_strcmp0 (session->sender, sender) == 0)
        request_session_revoke (session);
    }
}

static void
on_pronk_name_appeared (GDBusConnection *connection,
                        const char      *name,
                        const char      *name_owner,
                        gpointer         user_data)
{
  MetaCastKmsGrantBroker *broker = user_data;

  if (g_strcmp0 (broker->pronk_name_owner, name_owner) == 0)
    return;

  revoke_sessions_for_sender (broker, broker->pronk_name_owner);
  g_set_str (&broker->pronk_name_owner, name_owner);
}

static void
on_pronk_name_vanished (GDBusConnection *connection,
                        const char      *name,
                        gpointer         user_data)
{
  MetaCastKmsGrantBroker *broker = user_data;

  revoke_sessions_for_sender (broker, broker->pronk_name_owner);
  g_clear_pointer (&broker->pronk_name_owner, g_free);
}

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
  MetaCastKmsDisplaySession *session = user_data;

  request_session_revoke (session);
  return G_SOURCE_REMOVE;
}

static gboolean
on_revoke_retry (gpointer user_data)
{
  MetaCastKmsDisplaySession *session = user_data;

  /* The main context keeps the source alive while its callback is running. */
  g_clear_pointer (&session->revoke_retry_source, g_source_unref);
  request_session_revoke (session);
  return G_SOURCE_REMOVE;
}

static void
on_client_name_vanished (GDBusConnection *connection,
                         const char      *name,
                         gpointer         user_data)
{
  MetaCastKmsDisplaySession *session = user_data;

  g_clear_handle_id (&session->name_watch_id, g_bus_unwatch_name);
  request_session_revoke (session);
}

static void
meta_cast_kms_display_session_free (MetaCastKmsDisplaySession *session)
{
  clear_source (&session->capture_control_source);
  clear_source (&session->revoke_retry_source);
  if (session->name_watch_id)
    g_bus_unwatch_name (session->name_watch_id);

  g_clear_pointer (&session->renderer_controls, g_ptr_array_unref);
  g_clear_pointer (&session->monitor_control,
                   meta_kms_monitor_control_free);
  g_clear_object (&session->capture_grant);
  g_clear_pointer (&session->sender, g_free);
  g_free (session);
}

static void
request_session_revoke (MetaCastKmsDisplaySession *session)
{
  g_autoptr (GError) error = NULL;

  session->revoking = TRUE;
  clear_source (&session->capture_control_source);
  g_clear_pointer (&session->renderer_controls, g_ptr_array_unref);
  g_clear_pointer (&session->monitor_control,
                   meta_kms_monitor_control_free);

  if (!meta_kms_capture_grant_revoke (session->capture_grant, &error))
    {
      g_warning ("CastKMS display session %" G_GUINT64_FORMAT
                 " was revoked, but its device fd could not "
                 "be released; will retry: %s",
                 session->session_id,
                 error ? error->message : "unknown error");
      if (!session->revoke_retry_source)
        {
          session->revoke_retry_source = g_timeout_source_new_seconds (1);
          g_source_set_callback (session->revoke_retry_source,
                                 on_revoke_retry,
                                 session,
                                 NULL);
          g_source_attach (session->revoke_retry_source, NULL);
        }
      return;
    }

  g_hash_table_remove (session->broker->sessions, session);
}

static MetaCastKmsDisplaySession *
meta_cast_kms_display_session_new (MetaCastKmsGrantBroker  *broker,
                                   GDBusConnection         *connection,
                                   const char              *sender,
                                   MetaKmsDevice           *kms_device,
                                   uint32_t                 crtc_id,
                                   uint32_t                 connector_id,
                                   uint64_t                 session_id,
                                   MetaKmsCaptureGrant     *capture_grant,
                                   MetaKmsMonitorControl   *monitor_control,
                                   MetaKmsRendererControl  *renderer_control)
{
  MetaCastKmsDisplaySession *session;
  int control_fd;

  session = g_new0 (MetaCastKmsDisplaySession, 1);
  session->broker = broker;
  session->capture_grant = g_object_ref (capture_grant);
  session->monitor_control = monitor_control;
  session->renderer_controls =
    g_ptr_array_new_with_free_func ((GDestroyNotify)
                                    meta_cast_kms_renderer_endpoint_free);
  g_assert (add_renderer_control (session, renderer_control) == 1);
  session->sender = g_strdup (sender);
  session->kms_device = kms_device;
  session->crtc_id = crtc_id;
  session->connector_id = connector_id;
  session->session_id = session_id;

  control_fd = meta_kms_capture_grant_get_control_fd (capture_grant);
  g_assert (control_fd >= 0);

  /*
   * Observe kernel revocation before replying. Consumer close does not revoke
   * the grant; explicit session release or caller disappearance does.
   */
  session->capture_control_source =
    g_unix_fd_source_new (control_fd,
                          G_IO_HUP | G_IO_ERR | G_IO_NVAL);
  g_source_set_callback (session->capture_control_source,
                         G_SOURCE_FUNC (on_control_fd_hangup),
                         session,
                         NULL);
  g_source_attach (session->capture_control_source, NULL);

  session->name_watch_id =
    g_bus_watch_name_on_connection (connection,
                                    sender,
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    NULL,
                                    on_client_name_vanished,
                                    session,
                                    NULL);

  return session;
}

static gboolean
has_session_for_connector (MetaCastKmsGrantBroker *broker,
                           MetaKmsDevice          *kms_device,
                           uint32_t                connector_id)
{
  GHashTableIter iter;
  MetaCastKmsDisplaySession *session;

  g_hash_table_iter_init (&iter, broker->sessions);
  while (g_hash_table_iter_next (&iter, (gpointer *) &session, NULL))
    {
      if (session->kms_device == kms_device &&
          session->connector_id == connector_id)
        return TRUE;
    }

  return FALSE;
}

static MetaCastKmsDisplaySession *
find_session_for_sender (MetaCastKmsGrantBroker *broker,
                         const char             *sender,
                         uint64_t                session_id)
{
  GHashTableIter iter;
  MetaCastKmsDisplaySession *session;

  if (!broker->sessions || session_id == 0)
    return NULL;

  g_hash_table_iter_init (&iter, broker->sessions);
  while (g_hash_table_iter_next (&iter, (gpointer *) &session, NULL))
    {
      if (!session->revoking &&
          session->session_id == session_id &&
          g_strcmp0 (session->sender, sender) == 0)
        return session;
    }

  return NULL;
}

static void
create_display_session (MetaCastKmsGrantBroker *broker,
                        GDBusMethodInvocation  *invocation,
                        const char             *sender,
                        uint32_t                device_major,
                        uint32_t                device_minor,
                        uint32_t                crtc_id,
                        uint32_t                connector_id)
{
  GDBusConnection *connection =
    g_dbus_method_invocation_get_connection (invocation);
  g_autoptr (MetaKmsCaptureGrant) capture_grant = NULL;
  g_autoptr (MetaKmsMonitorControl) monitor_control = NULL;
  g_autoptr (MetaKmsRendererControl) renderer_control = NULL;
  g_autoptr (GUnixFDList) out_fd_list = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *render_node = NULL;
  g_autofd int monitor_fd = -1;
  g_autofd int renderer_fd = -1;
  g_autofd int capture_fd = -1;
  MetaKmsDevice *kms_device;
  MetaRenderer *renderer;
  MetaDeviceFile *render_device_file;
  MetaCastKmsDisplaySession *session;
  int monitor_index;
  int renderer_index;
  int capture_index;
  uint64_t session_id;

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

  renderer = meta_backend_get_renderer (META_BACKEND (broker->backend_native));
  render_device_file = meta_renderer_native_get_primary_device_file (
    META_RENDERER_NATIVE (renderer));
  render_node = drmGetRenderDeviceNameFromFd (
    meta_device_file_get_fd (render_device_file));
  if (!render_node)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation,
        G_IO_ERROR,
        G_IO_ERROR_NOT_SUPPORTED,
        "The compositor GPU has no DRM render node");
      return;
    }

  if (has_session_for_connector (broker, kms_device, connector_id))
    {
      g_dbus_method_invocation_return_error_literal (
        invocation,
        G_IO_ERROR,
        G_IO_ERROR_BUSY,
        "CastKMS connector already has a display session");
      return;
    }

  if (broker->last_session_id == G_MAXUINT64)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
        "Display session identifiers exhausted");
      return;
    }

  renderer_control =
    meta_kms_renderer_control_new (kms_device,
                                   crtc_id,
                                   connector_id,
                                   &error);
  if (!renderer_control)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }

  monitor_control =
    meta_kms_monitor_control_new (kms_device, connector_id, &error);
  if (!monitor_control)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }

  capture_grant =
    meta_kms_capture_grant_new (kms_device, crtc_id, connector_id, &error);
  if (!capture_grant)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }

  monitor_fd = meta_kms_monitor_control_steal_fd (monitor_control);
  renderer_fd = meta_kms_renderer_control_steal_fd (renderer_control);
  capture_fd = meta_kms_capture_grant_steal_capture_fd (capture_grant);

  out_fd_list = g_unix_fd_list_new ();
  monitor_index = g_unix_fd_list_append (out_fd_list, monitor_fd, &error);
  if (monitor_index == -1)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }
  renderer_index = g_unix_fd_list_append (out_fd_list, renderer_fd, &error);
  if (renderer_index == -1)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }
  capture_index = g_unix_fd_list_append (out_fd_list, capture_fd, &error);
  if (capture_index == -1)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return;
    }
  session_id = ++broker->last_session_id;
  session = meta_cast_kms_display_session_new (broker,
                                               connection,
                                               sender,
                                               kms_device,
                                               crtc_id,
                                               connector_id,
                                               session_id,
                                               capture_grant,
                                               g_steal_pointer (
                                                 &monitor_control),
                                               g_steal_pointer (
                                                 &renderer_control));
  g_hash_table_add (broker->sessions, session);

  meta_topic (META_DEBUG_DBUS,
              "Created CastKMS display session %" G_GUINT64_FORMAT " for %s",
              session_id,
              sender);
  meta_dbus_cast_kms_complete_create_display_session (
    META_DBUS_CAST_KMS (broker),
    invocation,
    out_fd_list,
    g_variant_new_handle (monitor_index),
    g_variant_new_handle (renderer_index),
    session->last_renderer_id,
    g_variant_new_handle (capture_index),
    render_node,
    session_id);
}

static gboolean
handle_create_display_session (MetaDBusCastKms       *object,
                               GDBusMethodInvocation *invocation,
                               GUnixFDList           *in_fd_list,
                               uint32_t               device_major,
                               uint32_t               device_minor,
                               uint32_t               crtc_id,
                               uint32_t               connector_id)
{
  MetaCastKmsGrantBroker *broker = META_CAST_KMS_GRANT_BROKER (object);
  const char *sender;

  if (!broker->backend_native)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation,
        G_DBUS_ERROR,
        G_DBUS_ERROR_FAILED,
        "CastKMS grant broker is shutting down");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (connector_id == 0 || crtc_id == 0)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation,
        G_DBUS_ERROR,
        G_DBUS_ERROR_INVALID_ARGS,
        "Invalid CastKMS display session request");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  sender = g_dbus_method_invocation_get_sender (invocation);
  create_display_session (broker,
                          invocation,
                          sender,
                          device_major,
                          device_minor,
                          crtc_id,
                          connector_id);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_acquire_renderer (MetaDBusCastKms       *object,
                         GDBusMethodInvocation *invocation,
                         GUnixFDList           *in_fd_list,
                         uint64_t               session_id)
{
  MetaCastKmsGrantBroker *broker = META_CAST_KMS_GRANT_BROKER (object);
  const char *sender = g_dbus_method_invocation_get_sender (invocation);
  MetaCastKmsDisplaySession *session;
  g_autoptr (MetaKmsRendererControl) renderer_control = NULL;
  g_autoptr (GUnixFDList) out_fd_list = NULL;
  g_autoptr (GError) error = NULL;
  g_autofd int renderer_fd = -1;
  int renderer_index;
  uint64_t renderer_id;

  session = find_session_for_sender (broker, sender, session_id);
  if (!session)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
        "Invalid renderer acquisition request");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (session->renderer_controls->len >= MAX_RENDERER_ENDPOINTS)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
        "Display session renderer endpoint limit reached");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  renderer_control =
    meta_kms_renderer_control_new (session->kms_device,
                                   session->crtc_id,
                                   session->connector_id,
                                   &error);
  if (!renderer_control)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  renderer_fd = meta_kms_renderer_control_steal_fd (renderer_control);
  out_fd_list = g_unix_fd_list_new ();
  renderer_index = g_unix_fd_list_append (out_fd_list, renderer_fd, &error);
  if (renderer_index == -1)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (session->last_renderer_id == G_MAXUINT64)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
        "Renderer endpoint identifiers exhausted");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }
  renderer_id = add_renderer_control (session,
                                      g_steal_pointer (&renderer_control));
  meta_dbus_cast_kms_complete_acquire_renderer (
    object,
    invocation,
    out_fd_list,
    g_variant_new_handle (renderer_index),
    renderer_id);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_release_renderer (MetaDBusCastKms       *object,
                         GDBusMethodInvocation *invocation,
                         uint64_t               session_id,
                         uint64_t               renderer_id)
{
  MetaCastKmsGrantBroker *broker = META_CAST_KMS_GRANT_BROKER (object);
  const char *sender = g_dbus_method_invocation_get_sender (invocation);
  MetaCastKmsDisplaySession *session;

  session = find_session_for_sender (broker, sender, session_id);
  if (!session || renderer_id == 0 ||
      renderer_id > session->last_renderer_id)
    {
      g_dbus_method_invocation_return_error_literal (
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
        "Invalid renderer release request");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  remove_renderer_control (session, renderer_id);
  meta_dbus_cast_kms_complete_release_renderer (object, invocation);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_release_display_session (MetaDBusCastKms       *object,
                                GDBusMethodInvocation *invocation,
                                uint64_t               session_id)
{
  MetaCastKmsGrantBroker *broker = META_CAST_KMS_GRANT_BROKER (object);
  const char *sender = g_dbus_method_invocation_get_sender (invocation);
  MetaCastKmsDisplaySession *session;

  session = find_session_for_sender (broker, sender, session_id);
  if (session)
    {
      request_session_revoke (session);
      meta_dbus_cast_kms_complete_release_display_session (object,
                                                           invocation);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  g_dbus_method_invocation_return_error_literal (
    invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
    "No display session belongs to that caller and identifier");
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
meta_cast_kms_grant_broker_init_iface (MetaDBusCastKmsIface *iface)
{
  iface->handle_create_display_session = handle_create_display_session;
  iface->handle_acquire_renderer = handle_acquire_renderer;
  iface->handle_release_renderer = handle_release_renderer;
  iface->handle_release_display_session = handle_release_display_session;
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
  g_clear_handle_id (&broker->pronk_name_watch_id, g_bus_unwatch_name);
  g_clear_pointer (&broker->pronk_name_owner, g_free);
  broker->pronk_name_watch_id =
    g_bus_watch_name_on_connection (connection,
                                    PRONK_DBUS_SERVICE,
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    on_pronk_name_appeared,
                                    on_pronk_name_vanished,
                                    broker,
                                    NULL);

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
  g_autoptr (GList) sessions = NULL;
  GList *l;

  meta_topic (META_DEBUG_DBUS, "Lost or failed to acquire name %s", name);
  if (!broker->sessions)
    return;

  sessions = g_hash_table_get_keys (broker->sessions);
  for (l = sessions; l; l = l->next)
    request_session_revoke (l->data);
}

static void
on_prepare_shutdown (MetaBackend            *backend,
                     MetaCastKmsGrantBroker *broker)
{
  /* Stop serving requests and revoke sessions while the KMS thread is alive. */
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

  broker->sessions =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           (GDestroyNotify) meta_cast_kms_display_session_free,
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
  if (g_dbus_interface_skeleton_get_connection (
        G_DBUS_INTERFACE_SKELETON (broker)))
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (broker));
  g_clear_handle_id (&broker->dbus_name_id, g_bus_unown_name);
  g_clear_handle_id (&broker->pronk_name_watch_id, g_bus_unwatch_name);
  g_clear_pointer (&broker->pronk_name_owner, g_free);
  g_clear_pointer (&broker->sessions, g_hash_table_unref);
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
