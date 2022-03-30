/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "backends/meta-input-capture-session.h"

#include <stdint.h>

#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-dbus-session-manager.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-remote-access-controller-private.h"
#include "meta/boxes.h"
#include "meta/meta-backend.h"

#include "meta-dbus-input-capture.h"

#define META_INPUT_CAPTURE_SESSION_DBUS_PATH "/org/gnome/Mutter/InputCapture/Session"

enum
{
  PROP_0,

  N_PROPS
};

struct _MetaInputCaptureSession
{
  MetaDBusInputCaptureSessionSkeleton parent;

  MetaDbusSessionManager *session_manager;

  GDBusConnection *connection;
  char *peer_name;

  char *session_id;
  char *object_path;

  gboolean enabled;

  uint32_t serial;

  MetaInputCaptureSessionHandle *handle;
};

static void initable_init_iface (GInitableIface *iface);

static void meta_input_capture_session_init_iface (MetaDBusInputCaptureSessionIface *iface);

static void meta_dbus_session_init_iface (MetaDbusSessionInterface *iface);

static MetaInputCaptureSessionHandle * meta_input_capture_session_handle_new (MetaInputCaptureSession *session);

G_DEFINE_TYPE_WITH_CODE (MetaInputCaptureSession,
                         meta_input_capture_session,
                         META_DBUS_TYPE_INPUT_CAPTURE_SESSION_SKELETON,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_init_iface)
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_INPUT_CAPTURE_SESSION,
                                                meta_input_capture_session_init_iface)
                         G_IMPLEMENT_INTERFACE (META_TYPE_DBUS_SESSION,
                                                meta_dbus_session_init_iface))

struct _MetaInputCaptureSessionHandle
{
  MetaRemoteAccessHandle parent;

  MetaInputCaptureSession *session;
};

G_DEFINE_TYPE (MetaInputCaptureSessionHandle,
               meta_input_capture_session_handle,
               META_TYPE_REMOTE_ACCESS_HANDLE)

static void
init_remote_access_handle (MetaInputCaptureSession *session)
{
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  MetaRemoteAccessController *remote_access_controller;
  MetaRemoteAccessHandle *remote_access_handle;

  session->handle = meta_input_capture_session_handle_new (session);

  remote_access_controller = meta_backend_get_remote_access_controller (backend);
  remote_access_handle = META_REMOTE_ACCESS_HANDLE (session->handle);
  meta_remote_access_controller_notify_new_handle (remote_access_controller,
                                                   remote_access_handle);
}

static gboolean
meta_input_capture_session_enable (MetaInputCaptureSession  *session,
                                   GError                  **error)
{
  g_assert (!session->enabled);

  session->enabled = TRUE;

  init_remote_access_handle (session);

  return TRUE;
}

static void
meta_input_capture_session_disable (MetaInputCaptureSession *session)
{
  session->enabled = FALSE;

  if (session->handle)
    {
      MetaRemoteAccessHandle *remote_access_handle =
        META_REMOTE_ACCESS_HANDLE (session->handle);

      meta_remote_access_handle_notify_stopped (remote_access_handle);
    }
}

static void
meta_input_capture_session_close (MetaDbusSession *dbus_session)
{
  MetaInputCaptureSession *session =
    META_INPUT_CAPTURE_SESSION (dbus_session);
  MetaDBusInputCaptureSession *skeleton =
    META_DBUS_INPUT_CAPTURE_SESSION (session);

  if (session->enabled)
    meta_input_capture_session_disable (session);

  meta_dbus_session_notify_closed (META_DBUS_SESSION (session));
  meta_dbus_input_capture_session_emit_closed (skeleton);
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (session));

  g_object_unref (session);
}

static gboolean
check_permission (MetaInputCaptureSession *session,
                  GDBusMethodInvocation   *invocation)
{
  return g_strcmp0 (session->peer_name,
                    g_dbus_method_invocation_get_sender (invocation)) == 0;
}

static gboolean
handle_add_barrier (MetaDBusInputCaptureSession *object,
                    GDBusMethodInvocation       *invocation,
                    unsigned int                 serial,
                    GVariant                    *position)
{
  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                         G_DBUS_ERROR_FAILED,
                                         "Not implemented");
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_get_zones (MetaDBusInputCaptureSession *object,
                  GDBusMethodInvocation       *invocation)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (object);
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GVariant *zones_variant;
  GVariantBuilder zones_builder;
  GList *logical_monitors;
  GList *l;

  g_variant_builder_init (&zones_builder, G_VARIANT_TYPE ("a(uuii)"));
  logical_monitors = meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaRectangle layout;

      layout = meta_logical_monitor_get_layout (logical_monitor);
      g_variant_builder_add (&zones_builder, "(uuii)",
                             layout.width,
                             layout.height,
                             layout.x,
                             layout.y);
    }

  zones_variant = g_variant_builder_end (&zones_builder);

  meta_dbus_input_capture_session_complete_get_zones (object, invocation,
                                                      session->serial,
                                                      zones_variant);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_enable (MetaDBusInputCaptureSession *skeleton,
               GDBusMethodInvocation       *invocation)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (skeleton);
  g_autoptr (GError) error = NULL;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (session->enabled)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Already enabled");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!meta_input_capture_session_enable (session, &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to enable input capture: %s",
                                             error->message);

      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_dbus_input_capture_session_complete_enable (skeleton, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_disable (MetaDBusInputCaptureSession *skeleton,
                GDBusMethodInvocation       *invocation)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (skeleton);

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!session->enabled)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Session not enabled");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_input_capture_session_disable (session);

  meta_dbus_input_capture_session_complete_disable (skeleton, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_release (MetaDBusInputCaptureSession *object,
                GDBusMethodInvocation       *invocation,
                GVariant                    *position)
{
  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                         G_DBUS_ERROR_ACCESS_DENIED,
                                         "Not implemented");
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_close (MetaDBusInputCaptureSession *object,
              GDBusMethodInvocation       *invocation)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (object);

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_dbus_session_close (META_DBUS_SESSION (session));

  meta_dbus_input_capture_session_complete_close (object, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
on_monitors_changed (MetaMonitorManager      *monitor_manager,
                     MetaInputCaptureSession *session)
{
  MetaDBusInputCaptureSession *skeleton =
    META_DBUS_INPUT_CAPTURE_SESSION (session);

  session->serial++;
  meta_input_capture_session_disable (session);
  meta_dbus_input_capture_session_emit_zones_changed (skeleton);
}

static gboolean
meta_input_capture_session_initable_init (GInitable     *initable,
                                          GCancellable  *cancellable,
                                          GError       **error)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (initable);
  GDBusInterfaceSkeleton *interface_skeleton = G_DBUS_INTERFACE_SKELETON (session);
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  session->connection =
    meta_dbus_session_manager_get_connection (session->session_manager);
  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         session->connection,
                                         session->object_path,
                                         error))
    return FALSE;

  g_signal_connect_object (monitor_manager, "monitors-changed",
                           G_CALLBACK (on_monitors_changed),
                           session, 0);

  return TRUE;
}

static void
initable_init_iface (GInitableIface *iface)
{
  iface->init = meta_input_capture_session_initable_init;
}

static void
meta_input_capture_session_init_iface (MetaDBusInputCaptureSessionIface *iface)
{
  iface->handle_add_barrier = handle_add_barrier;
  iface->handle_enable = handle_enable;
  iface->handle_disable = handle_disable;
  iface->handle_release = handle_release;
  iface->handle_close = handle_close;
  iface->handle_get_zones = handle_get_zones;
}

static void
meta_dbus_session_init_iface (MetaDbusSessionInterface *iface)
{
  iface->close = meta_input_capture_session_close;
}

static void
meta_input_capture_session_finalize (GObject *object)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (object);

  g_clear_object (&session->handle);
  g_free (session->peer_name);
  g_free (session->session_id);
  g_free (session->object_path);

  G_OBJECT_CLASS (meta_input_capture_session_parent_class)->finalize (object);
}

static void
meta_input_capture_session_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (object);

  switch (prop_id)
    {
    case N_PROPS + META_DBUS_SESSION_PROP_SESSION_MANAGER:
      session->session_manager = g_value_get_object (value);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_PEER_NAME:
      session->peer_name = g_value_dup_string (value);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_ID:
      session->session_id = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_input_capture_session_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (object);

  switch (prop_id)
    {
    case N_PROPS + META_DBUS_SESSION_PROP_SESSION_MANAGER:
      g_value_set_object (value, session->session_manager);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_PEER_NAME:
      g_value_set_string (value, session->peer_name);
      break;
    case N_PROPS + META_DBUS_SESSION_PROP_ID:
      g_value_set_string (value, session->session_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_input_capture_session_class_init (MetaInputCaptureSessionClass *klass)
{

  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_input_capture_session_finalize;
  object_class->set_property = meta_input_capture_session_set_property;
  object_class->get_property = meta_input_capture_session_get_property;

  meta_dbus_session_install_properties (object_class, N_PROPS);
}

static void
meta_input_capture_session_init (MetaInputCaptureSession *session)
{
  static unsigned int global_session_number = 0;

  session->object_path =
    g_strdup_printf (META_INPUT_CAPTURE_SESSION_DBUS_PATH "/u%u",
                     ++global_session_number);
}

char *
meta_input_capture_session_get_object_path (MetaInputCaptureSession *session)
{
  return session->object_path;
}

static MetaInputCaptureSessionHandle *
meta_input_capture_session_handle_new (MetaInputCaptureSession *session)
{
  MetaInputCaptureSessionHandle *handle;

  handle = g_object_new (META_TYPE_INPUT_CAPTURE_SESSION_HANDLE, NULL);
  handle->session = session;

  return handle;
}

static void
meta_input_capture_session_handle_stop (MetaRemoteAccessHandle *handle)
{
  MetaInputCaptureSession *session;

  session = META_INPUT_CAPTURE_SESSION_HANDLE (handle)->session;
  if (!session)
    return;

  meta_dbus_session_close (META_DBUS_SESSION (session));
}

static void
meta_input_capture_session_handle_class_init (MetaInputCaptureSessionHandleClass *klass)
{
  MetaRemoteAccessHandleClass *remote_access_handle_class =
    META_REMOTE_ACCESS_HANDLE_CLASS (klass);

  remote_access_handle_class->stop = meta_input_capture_session_handle_stop;
}

static void
meta_input_capture_session_handle_init (MetaInputCaptureSessionHandle *handle)
{
}
