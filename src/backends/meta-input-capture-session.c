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
#include "meta/barrier.h"
#include "meta/boxes.h"
#include "meta/meta-backend.h"

#include "meta-dbus-input-capture.h"

#define META_INPUT_CAPTURE_SESSION_DBUS_PATH "/org/gnome/Mutter/InputCapture/Session"

static GQuark quark_barrier_id;

enum
{
  PROP_0,

  N_PROPS
};

typedef enum _InputCaptureState
{
  INPUT_CAPTURE_STATE_INIT,
  INPUT_CAPTURE_STATE_ENABLED,
  INPUT_CAPTURE_STATE_ACTIVATED,
  INPUT_CAPTURE_STATE_CLOSED,
} InputCaptureState;

typedef struct _InputCaptureBarrier
{
  int x1;
  int y1;
  int x2;
  int y2;

  unsigned int id;
  MetaBarrier *barrier;
} InputCaptureBarrier;

struct _MetaInputCaptureSession
{
  MetaDBusInputCaptureSessionSkeleton parent;

  MetaDbusSessionManager *session_manager;

  GDBusConnection *connection;
  char *peer_name;

  char *session_id;
  char *object_path;

  InputCaptureState state;
  GHashTable *barriers;

  uint32_t zones_serial;
  uint32_t activation_id;

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

static void
release_remote_access_handle (MetaInputCaptureSession *session)
{
  MetaRemoteAccessHandle *remote_access_handle =
    META_REMOTE_ACCESS_HANDLE (session->handle);

  meta_remote_access_handle_notify_stopped (remote_access_handle);
  g_clear_object (&session->handle);
}

static void
on_barrier_hit (MetaBarrier             *barrier,
                const MetaBarrierEvent  *event,
                MetaInputCaptureSession *session)
{
  MetaDBusInputCaptureSession *skeleton =
    META_DBUS_INPUT_CAPTURE_SESSION (session);
  GVariant *cursor_position;
  unsigned int barrier_id;

  switch (session->state)
    {
    case INPUT_CAPTURE_STATE_ACTIVATED:
      return;
    case INPUT_CAPTURE_STATE_ENABLED:
      break;
    case INPUT_CAPTURE_STATE_INIT:
    case INPUT_CAPTURE_STATE_CLOSED:
      g_warn_if_reached ();
      return;
    }

  session->state = INPUT_CAPTURE_STATE_ACTIVATED;

  barrier_id = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (barrier),
                                                     quark_barrier_id));
  cursor_position = g_variant_new ("(dd)", event->x, event->y);

  meta_dbus_input_capture_session_emit_activated (skeleton,
                                                  barrier_id,
                                                  ++session->activation_id,
                                                  cursor_position);

  init_remote_access_handle (session);
}

static void
clear_all_barriers (MetaInputCaptureSession *session)
{
  GHashTableIter iter;
  InputCaptureBarrier *input_capture_barrier;

  g_hash_table_iter_init (&iter, session->barriers);
  while (g_hash_table_iter_next (&iter, NULL,
                                 (gpointer *) &input_capture_barrier))
    g_clear_pointer (&input_capture_barrier->barrier, meta_barrier_destroy);
}

static void
release_all_barriers (MetaInputCaptureSession *session)
{
  GHashTableIter iter;
  InputCaptureBarrier *input_capture_barrier;

  g_hash_table_iter_init (&iter, session->barriers);
  while (g_hash_table_iter_next (&iter, NULL,
                                 (gpointer *) &input_capture_barrier))
    {
      MetaBarrier *barrier;

      barrier = input_capture_barrier->barrier;
      if (!barrier)
        continue;

      meta_barrier_release (barrier, NULL);
    }
}

static gboolean
meta_input_capture_session_enable (MetaInputCaptureSession  *session,
                                   GError                  **error)
{
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  GHashTableIter iter;
  gpointer key, value;

  g_warn_if_fail (session->state == INPUT_CAPTURE_STATE_INIT);

  g_hash_table_iter_init (&iter, session->barriers);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      unsigned int barrier_id = GPOINTER_TO_UINT (key);
      InputCaptureBarrier *input_capture_barrier = value;
      g_autoptr (MetaBarrier) barrier = NULL;

      barrier = meta_barrier_new (backend,
                                  input_capture_barrier->x1,
                                  input_capture_barrier->y1,
                                  input_capture_barrier->x2,
                                  input_capture_barrier->y2,
                                  0,
                                  META_BARRIER_FLAG_STICKY,
                                  error);
      if (!barrier)
        goto err;

      g_object_set_qdata (G_OBJECT (barrier), quark_barrier_id,
                          GUINT_TO_POINTER (barrier_id));
      g_signal_connect (barrier, "hit", G_CALLBACK (on_barrier_hit), session);
      input_capture_barrier->barrier = barrier;
    }

  session->state = INPUT_CAPTURE_STATE_ENABLED;

  return TRUE;

err:
  clear_all_barriers (session);
  return FALSE;
}

static void
meta_input_capture_session_disable (MetaInputCaptureSession *session)
{
  switch (session->state)
    {
    case INPUT_CAPTURE_STATE_INIT:
      return;
    case INPUT_CAPTURE_STATE_ACTIVATED:
    case INPUT_CAPTURE_STATE_ENABLED:
      break;
    case INPUT_CAPTURE_STATE_CLOSED:
      g_warn_if_reached ();
      return;
    }

  clear_all_barriers (session);

  session->state = INPUT_CAPTURE_STATE_INIT;

  if (session->handle)
    release_remote_access_handle (session);
}

static void
meta_input_capture_session_close (MetaDbusSession *dbus_session)
{
  MetaInputCaptureSession *session =
    META_INPUT_CAPTURE_SESSION (dbus_session);
  MetaDBusInputCaptureSession *skeleton =
    META_DBUS_INPUT_CAPTURE_SESSION (session);

  meta_input_capture_session_disable (session);
  session->state = INPUT_CAPTURE_STATE_CLOSED;

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

typedef enum
{
  LINE_ADJACENCY_ERROR,
  LINE_ADJACENCY_NONE,
  LINE_ADJACENCY_OVERLAP,
  LINE_ADJACENCY_CONTAINED,
  LINE_ADJACENCY_PARTIAL,
} LineAdjacency;

static LineAdjacency
get_barrier_adjacency (MetaRectangle  *rect,
                       int             x1,
                       int             y1,
                       int             x2,
                       int             y2,
                       GError        **error)
{
  int x_min, x_max;
  int y_min, y_max;

  x_min = MIN (x1, x2);
  x_max = MAX (x1, x2);
  y_min = MIN (y1, y2);
  y_max = MAX (y1, y2);

  if (x1 == x2)
    {
      int x = x1;

      if (x < rect->x || x > rect->x + rect->width)
        return LINE_ADJACENCY_NONE;

      if (y_max < rect->y ||
          y_min > rect->y + rect->height)
        return LINE_ADJACENCY_NONE;

      if (rect->x + rect->width == x || rect->x == x)
        {
          if (y_max > rect->y + rect->height ||
              y_min < rect->y)
            return LINE_ADJACENCY_PARTIAL;
          else
            return LINE_ADJACENCY_CONTAINED;
        }
      else
        {
          return LINE_ADJACENCY_OVERLAP;
        }
    }
  else if (y1 == y2)
    {
      int y = y1;

      if (y < rect->y || y > rect->y + rect->height)
        return LINE_ADJACENCY_NONE;

      if (x_max < rect->x ||
          x_min > rect->x + rect->width)
        return LINE_ADJACENCY_NONE;

      if (rect->y + rect->height == y || rect->y == y)
        {
          if (x_max > rect->x + rect->width ||
              x_min < rect->x)
            return LINE_ADJACENCY_PARTIAL;
          else
            return LINE_ADJACENCY_CONTAINED;
        }
      else
        {
          return LINE_ADJACENCY_OVERLAP;
        }
    }

  return LINE_ADJACENCY_NONE;
}

static gboolean
check_barrier (MetaInputCaptureSession  *session,
               int                       x1,
               int                       y1,
               int                       x2,
               int                       y2,
               GError                  **error)
{
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  gboolean has_adjecent_monitor = FALSE;
  GList *logical_monitors;
  GList *l;

  if (x1 != x2 && y1 != y2)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Barrier coordinates not axis aligned");
      return FALSE;
    }

  if (x1 == x2 && y1 == y2)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Barrier cannot be a singularity");
      return FALSE;
    }

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  for (l = logical_monitors; l; l = l->next)
    {
      MetaLogicalMonitor *logical_monitor = l->data;
      MetaRectangle layout;

      layout = meta_logical_monitor_get_layout (logical_monitor);
      switch (get_barrier_adjacency (&layout, x1, y1, x2, y2, error))
        {
        case LINE_ADJACENCY_ERROR:
          return FALSE;
        case LINE_ADJACENCY_NONE:
          break;
        case LINE_ADJACENCY_CONTAINED:
          if (has_adjecent_monitor)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Adjecent to multiple monitor edges");
              return FALSE;
            }
          has_adjecent_monitor = TRUE;
          break;
        case LINE_ADJACENCY_OVERLAP:
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                       "Line overlaps with monitor region");
          return FALSE;
        case LINE_ADJACENCY_PARTIAL:
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                       "Line partially with monitor region");
          return FALSE;
        }
    }

  return has_adjecent_monitor;
}

static unsigned int
find_available_barrier_id (MetaInputCaptureSession *session)
{
  unsigned int id;

  for (id = 1;; id++)
    {
      if (!g_hash_table_contains (session->barriers, GUINT_TO_POINTER (id)))
        return id;
    }
}

static void
input_capture_barrier_free (gpointer user_data)
{
  InputCaptureBarrier *input_capture_barrier = user_data;

  g_clear_pointer (&input_capture_barrier->barrier, meta_barrier_destroy);
  g_free (input_capture_barrier);
}

static gboolean
handle_add_barrier (MetaDBusInputCaptureSession *object,
                    GDBusMethodInvocation       *invocation,
                    unsigned int                 serial,
                    GVariant                    *position)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (object);
  int x1, y1, x2, y2;
  g_autoptr (GError) error = NULL;
  InputCaptureBarrier *input_capture_barrier;
  unsigned int barrier_id;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (session->zones_serial != serial)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_BAD_ADDRESS,
                                             "State out of date");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (session->state != INPUT_CAPTURE_STATE_INIT)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Session already enabled");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  g_variant_get (position, "(iiii)", &x1, &y1, &x2, &y2);
  if (!check_barrier (session, x1, y1, x2, y2, &error))
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_ACCESS_DENIED,
                                                     error->message);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  barrier_id = find_available_barrier_id (session);

  input_capture_barrier = g_new0 (InputCaptureBarrier, 1);
  *input_capture_barrier = (InputCaptureBarrier) {
    .id = barrier_id,
    .x1 = x1,
    .y1 = y1,
    .x2 = x2,
    .y2 = y2,
  };
  g_hash_table_insert (session->barriers,
                       GUINT_TO_POINTER (barrier_id),
                       input_capture_barrier);

  meta_dbus_input_capture_session_complete_add_barrier (object, invocation,
                                                        barrier_id);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_clear_barriers (MetaDBusInputCaptureSession *object,
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

  g_hash_table_remove_all (session->barriers);

  meta_dbus_input_capture_session_complete_clear_barriers (object, invocation);

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
                                                      session->zones_serial,
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

  if (session->state != INPUT_CAPTURE_STATE_INIT)
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

  if (session->state != INPUT_CAPTURE_STATE_ENABLED &&
      session->state != INPUT_CAPTURE_STATE_ACTIVATED)
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
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (object);
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  double x, y;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (session->state != INPUT_CAPTURE_STATE_ACTIVATED)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Capture not active");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  release_all_barriers (session);

  session->state = INPUT_CAPTURE_STATE_ENABLED;

  g_variant_get (position, "(dd)", &x, &y);
  clutter_seat_warp_pointer (seat, x, y);

  if (session->handle)
    release_remote_access_handle (session);

  meta_dbus_input_capture_session_complete_release (object, invocation);

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

  session->zones_serial++;
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
  iface->handle_clear_barriers = handle_clear_barriers;
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

  g_clear_pointer (&session->barriers, g_hash_table_unref);

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

  quark_barrier_id =
    g_quark_from_static_string ("meta-input-capture-barrier-id-quark");
}

static void
meta_input_capture_session_init (MetaInputCaptureSession *session)
{
  static unsigned int global_session_number = 0;

  session->object_path =
    g_strdup_printf (META_INPUT_CAPTURE_SESSION_DBUS_PATH "/u%u",
                     ++global_session_number);

  session->barriers = g_hash_table_new_full (NULL, NULL, NULL,
                                             input_capture_barrier_free);
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
