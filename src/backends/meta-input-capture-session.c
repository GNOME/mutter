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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/meta-input-capture-session.h"

#include <gio/gunixfdlist.h>
#include <libeis.h>
#include <stdint.h>

#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-dbus-session-manager.h"
#include "backends/meta-fd-source.h"
#include "backends/meta-input-capture-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-remote-access-controller-private.h"
#include "core/meta-anonymous-file.h"
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

  struct eis *eis;
  struct eis_client *eis_client;
  struct eis_seat *eis_seat;
  struct eis_device *eis_pointer;
  struct eis_device *eis_keyboard;
  GSource *eis_source;

  MetaAnonymousFile *keymap_file;

  MetaViewportInfo *viewports;

  gboolean cancel_requested;
  unsigned int buttons_pressed;
  unsigned int keys_pressed;
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
setup_client (MetaInputCaptureSession *session,
              struct eis_client       *eis_client)
{
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  struct eis_seat *eis_seat;

  session->eis_client = eis_client_ref (eis_client);

  eis_client_connect (eis_client);

  eis_seat = eis_client_new_seat (eis_client, clutter_seat_get_name (seat));
  eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_POINTER);
  eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_BUTTON);
  eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_SCROLL);
  eis_seat_configure_capability (eis_seat, EIS_DEVICE_CAP_KEYBOARD);
  eis_seat_add (eis_seat);

  session->eis_seat = eis_seat;
}

static void
ensure_eis_pointer_regions (MetaInputCaptureSession *session,
                            struct eis_device       *eis_pointer)
{
  int idx = 0;
  MtkRectangle rect;
  float scale;

  if (!session->viewports)
    return;

  while (meta_viewport_info_get_view_info (session->viewports, idx++, &rect, &scale))
    {
      struct eis_region *r = eis_device_new_region (eis_pointer);

      eis_region_set_offset (r, rect.x, rect.y);
      eis_region_set_size (r, rect.width, rect.height);
      eis_region_set_physical_scale (r, scale);
      eis_region_add (r);
      eis_region_unref (r);
    }
}

static void
ensure_eis_pointer (MetaInputCaptureSession *session)
{
  struct eis_device *eis_pointer;

  if (session->eis_pointer)
    return;

  eis_pointer = eis_seat_new_device (session->eis_seat);
  eis_device_configure_name (eis_pointer, "captured relative pointer");
  eis_device_configure_capability (eis_pointer, EIS_DEVICE_CAP_POINTER);
  eis_device_configure_capability (eis_pointer, EIS_DEVICE_CAP_BUTTON);
  eis_device_configure_capability (eis_pointer, EIS_DEVICE_CAP_SCROLL);
  ensure_eis_pointer_regions (session, eis_pointer);
  eis_device_add (eis_pointer);
  eis_device_resume (eis_pointer);

  session->eis_pointer = eis_pointer;
}

static MetaAnonymousFile *
ensure_xkb_keymap_file (MetaInputCaptureSession  *session,
                        GError                  **error)
{
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  struct xkb_keymap *keymap;
  g_autofree char *keymap_string = NULL;
  size_t keymap_size;

  if (session->keymap_file)
    return session->keymap_file;

  keymap = meta_backend_get_keymap (backend);
  if (!keymap)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Backend has no keymap");
      return NULL;
    }

  keymap_string = xkb_keymap_get_as_string (keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  keymap_size = strlen (keymap_string) + 1;

  session->keymap_file =
    meta_anonymous_file_new (keymap_size, (const uint8_t *) keymap_string);

  return session->keymap_file;
}

static void
ensure_eis_keyboard (MetaInputCaptureSession *session)
{
  struct eis_device *eis_keyboard;
  g_autoptr (GError) error = NULL;
  struct eis_keymap *eis_keymap;
  MetaAnonymousFile *keymap_file;
  int keymap_fd;
  size_t keymap_size;

  if (session->eis_keyboard)
    return;

  keymap_file = ensure_xkb_keymap_file (session, &error);
  if (!keymap_file)
    {
      g_warning ("Failed to create input capture keymap file: %s",
                 error->message);
      return;
    }

  eis_keyboard = eis_seat_new_device (session->eis_seat);
  eis_device_configure_name (eis_keyboard, "captured keyboard");
  eis_device_configure_capability (eis_keyboard, EIS_DEVICE_CAP_KEYBOARD);

  keymap_fd = meta_anonymous_file_open_fd (keymap_file,
                                           META_ANONYMOUS_FILE_MAPMODE_PRIVATE);
  keymap_size = meta_anonymous_file_size (keymap_file);
  eis_keymap = eis_device_new_keymap (eis_keyboard,
                                      EIS_KEYMAP_TYPE_XKB,
                                      keymap_fd, keymap_size);
  eis_keymap_add (eis_keymap);
  eis_keymap_unref (eis_keymap);
  meta_anonymous_file_close_fd (keymap_fd);

  eis_device_add (eis_keyboard);
  eis_device_resume (eis_keyboard);

  session->eis_keyboard = eis_keyboard;
}

static void
clear_eis_pointer (MetaInputCaptureSession *session)
{
  if (!session->eis_pointer)
    return;

  eis_device_remove (session->eis_pointer);
  g_clear_pointer (&session->eis_pointer, eis_device_unref);
}

static void
remove_eis_pointer (MetaInputCaptureSession *session)
{
  clear_eis_pointer (session);

  /* The pointer is removed, all its buttons are cleared */
  session->buttons_pressed = 0;
}

static void
clear_eis_keyboard (MetaInputCaptureSession *session)
{
  if (!session->eis_keyboard)
    return;

  eis_device_remove (session->eis_keyboard);
  g_clear_pointer (&session->eis_keyboard, eis_device_unref);
}

static void
remove_eis_keyboard (MetaInputCaptureSession *session)
{
  clear_eis_keyboard (session);

  /* The pointer is removed, all its buttons are cleared */
  session->keys_pressed = 0;
}

static void
on_keymap_changed (MetaBackend *backend,
                   gpointer     user_data)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (user_data);

  g_clear_pointer (&session->keymap_file, meta_anonymous_file_free);

  if (session->eis_keyboard)
    {
      clear_eis_keyboard (session);
      ensure_eis_keyboard (session);
    }
}

static void
process_eis_event (MetaInputCaptureSession *session,
                   struct eis_event        *eis_event)
{
  struct eis_client *eis_client;
  struct eis_device *eis_device;

  switch (eis_event_get_type (eis_event))
    {
    case EIS_EVENT_CLIENT_CONNECT:
      eis_client = eis_event_get_client (eis_event);
      if (eis_client_is_sender (eis_client))
        {
          g_warning ("Unexpected sender libei client '%s' connected to "
                     "input capture session",
                     eis_client_get_name (eis_client));
          eis_client_disconnect (eis_client);
          return;
        }

      if (session->eis_client)
        {
          g_warning ("Unexpected additional libei client '%s' connected to "
                     "input capture session",
                     eis_client_get_name (eis_client));
          eis_client_disconnect (eis_client);
          return;
        }

      setup_client (session, eis_client);
      break;

    case EIS_EVENT_CLIENT_DISCONNECT:
      g_clear_pointer (&session->eis_seat, eis_seat_unref);
      g_clear_pointer (&session->eis_client, eis_client_unref);
      break;
    case EIS_EVENT_SEAT_BIND:
      if (eis_event_seat_has_capability (eis_event, EIS_DEVICE_CAP_POINTER) &&
          eis_event_seat_has_capability (eis_event, EIS_DEVICE_CAP_BUTTON) &&
          eis_event_seat_has_capability (eis_event, EIS_DEVICE_CAP_SCROLL))
        ensure_eis_pointer (session);
      else if (session->eis_pointer)
        clear_eis_pointer (session);

      if (eis_event_seat_has_capability (eis_event, EIS_DEVICE_CAP_KEYBOARD))
        ensure_eis_keyboard (session);
      else if (session->eis_keyboard)
        clear_eis_keyboard (session);
      break;
    case EIS_EVENT_DEVICE_CLOSED:
      eis_device = eis_event_get_device (eis_event);

      if (eis_device == session->eis_pointer)
        remove_eis_pointer (session);
      else if (eis_device == session->eis_keyboard)
        remove_eis_keyboard (session);
      break;
    default:
      break;
    }
}

static void
on_barrier_hit (MetaBarrier             *barrier,
                const MetaBarrierEvent  *event,
                MetaInputCaptureSession *session)
{
  MetaDBusInputCaptureSession *skeleton =
    META_DBUS_INPUT_CAPTURE_SESSION (session);
  MetaInputCapture *input_capture =
    META_INPUT_CAPTURE (session->session_manager);
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

  meta_input_capture_activate (input_capture, session);

  meta_dbus_input_capture_session_emit_activated (skeleton,
                                                  barrier_id,
                                                  ++session->activation_id,
                                                  cursor_position);
  if (session->eis_pointer)
    eis_device_start_emulating (session->eis_pointer, session->activation_id);
  if (session->eis_keyboard)
    eis_device_start_emulating (session->eis_keyboard, session->activation_id);

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
  session->cancel_requested = FALSE;

  return TRUE;

err:
  clear_all_barriers (session);
  return FALSE;
}

static void
meta_input_capture_session_deactivate (MetaInputCaptureSession *session)
{
  MetaDBusInputCaptureSession *skeleton =
    META_DBUS_INPUT_CAPTURE_SESSION (session);
  MetaInputCapture *input_capture =
    META_INPUT_CAPTURE (session->session_manager);

  meta_input_capture_deactivate (input_capture, session);

  if (session->eis_pointer)
    eis_device_stop_emulating (session->eis_pointer);
  if (session->eis_keyboard)
    eis_device_stop_emulating (session->eis_keyboard);
  meta_dbus_input_capture_session_emit_deactivated (skeleton,
                                                    session->activation_id);

  session->state = INPUT_CAPTURE_STATE_ENABLED;
}

static void
meta_input_capture_session_disable (MetaInputCaptureSession *session)
{
  switch (session->state)
    {
    case INPUT_CAPTURE_STATE_INIT:
      return;
    case INPUT_CAPTURE_STATE_ACTIVATED:
      meta_input_capture_session_deactivate (session);
      G_GNUC_FALLTHROUGH;
    case INPUT_CAPTURE_STATE_ENABLED:
      break;
    case INPUT_CAPTURE_STATE_CLOSED:
      g_warn_if_reached ();
      return;
    }

  clear_all_barriers (session);

  g_clear_pointer (&session->eis_pointer, eis_device_unref);
  g_clear_pointer (&session->eis_keyboard, eis_device_unref);
  g_clear_pointer (&session->eis_seat, eis_seat_unref);

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
get_barrier_adjacency (MtkRectangle   *rect,
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
          y_min >= rect->y + rect->height)
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
          x_min >= rect->x + rect->width)
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
  gboolean has_adjacent_monitor = FALSE;
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
      MtkRectangle layout;

      layout = meta_logical_monitor_get_layout (logical_monitor);
      switch (get_barrier_adjacency (&layout, x1, y1, x2, y2, error))
        {
        case LINE_ADJACENCY_ERROR:
          return FALSE;
        case LINE_ADJACENCY_NONE:
          break;
        case LINE_ADJACENCY_CONTAINED:
          if (has_adjacent_monitor)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                           "Adjacent to multiple monitor edges");
              return FALSE;
            }
          has_adjacent_monitor = TRUE;
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

  if (has_adjacent_monitor && y1 == y2)
    {
      MetaLogicalMonitor *monitor;
      MetaLogicalMonitor *next;
      MtkRectangle layout, fake_layout;

      monitor = meta_monitor_manager_get_logical_monitor_at (monitor_manager, 0, 0);
      while ((next = meta_monitor_manager_get_logical_monitor_neighbor (monitor_manager, monitor, META_DISPLAY_RIGHT)))
        monitor = next;

      layout = meta_logical_monitor_get_layout (monitor);
      fake_layout = (MtkRectangle) {
        .x = layout.x + layout.width,
        .y = layout.y,
        .width = layout.width,
        .height = layout.height,
      };

      LineAdjacency adjacency = get_barrier_adjacency (&fake_layout, x1, y1, x2, y2, error);
      if (adjacency != LINE_ADJACENCY_NONE)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                       "Line extends into nonexisting monitor region");
          return FALSE;
        }
    }

  return has_adjacent_monitor;
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
      MtkRectangle layout;

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
                GVariant                    *arg_options)
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
  meta_input_capture_session_deactivate (session);

  if (g_variant_lookup (arg_options, "cursor_position", "(dd)", &x, &y))
    clutter_seat_warp_pointer (seat, x, y);

  if (session->handle)
    release_remote_access_handle (session);

  meta_dbus_input_capture_session_complete_release (object, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_connect_to_eis (MetaDBusInputCaptureSession *object,
                       GDBusMethodInvocation       *invocation,
                       GUnixFDList                 *fd_list_in)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (object);
  int fd;
  g_autoptr (GUnixFDList) fd_list = NULL;
  int fd_idx;
  GVariant *fd_variant;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  fd = eis_backend_fd_add_client (session->eis);
  if (fd < 0)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to create socket: %s",
                                             g_strerror (-fd));
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  fd_list = g_unix_fd_list_new ();
  fd_idx = g_unix_fd_list_append (fd_list, fd, NULL);
  close (fd);
  fd_variant = g_variant_new_handle (fd_idx);

  meta_dbus_input_capture_session_complete_connect_to_eis (object,
                                                           invocation,
                                                           fd_list,
                                                           fd_variant);
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
meta_input_capture_session_set_viewports (MetaInputCaptureSession *session,
                                          MetaViewportInfo        *viewports)
{
  g_clear_object (&session->viewports);
  session->viewports = g_object_ref (viewports);

  if (!session->eis_pointer)
    return;

  clear_eis_pointer (session);
  ensure_eis_pointer (session);
}

static void
on_monitors_changed (MetaMonitorManager      *monitor_manager,
                     MetaInputCaptureSession *session)
{
  MetaDBusInputCaptureSession *skeleton =
    META_DBUS_INPUT_CAPTURE_SESSION (session);
  MetaViewportInfo *viewports;

  viewports = meta_monitor_manager_get_viewports (monitor_manager);
  meta_input_capture_session_set_viewports (session, viewports);

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
  MetaViewportInfo *viewports =
    meta_monitor_manager_get_viewports (monitor_manager);

  session->connection =
    meta_dbus_session_manager_get_connection (session->session_manager);
  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         session->connection,
                                         session->object_path,
                                         error))
    return FALSE;

  meta_input_capture_session_set_viewports (session, viewports);
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
  iface->handle_connect_to_eis = handle_connect_to_eis;
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
  g_clear_object (&session->viewports);
  g_clear_pointer (&session->keymap_file, meta_anonymous_file_free);
  g_clear_pointer (&session->eis_source, g_source_destroy);
  g_clear_pointer (&session->eis, eis_unref);

  G_OBJECT_CLASS (meta_input_capture_session_parent_class)->finalize (object);
}

static void
meta_eis_log_handler (struct eis             *eis,
                      enum eis_log_priority   priority,
                      const char             *message,
                      struct eis_log_context *ctx)
{
  int message_length = strlen (message);

  if (priority >= EIS_LOG_PRIORITY_ERROR)
    g_critical ("EIS: %.*s", message_length, message);
  else if (priority >= EIS_LOG_PRIORITY_WARNING)
    g_warning ("EIS: %.*s", message_length, message);
  else if (priority >= EIS_LOG_PRIORITY_INFO)
    g_info ("EIS: %.*s", message_length, message);
  else
    meta_topic (META_DEBUG_INPUT, "EIS: %.*s", message_length, message);
}

static gboolean
meta_eis_source_prepare (gpointer user_data)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (user_data);
  struct eis_event *eis_event;
  gboolean retval;

  eis_event = eis_peek_event (session->eis);
  retval = !!eis_event;
  eis_event_unref (eis_event);

  return retval;
}

static gboolean
meta_eis_source_dispatch (gpointer user_data)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (user_data);

  eis_dispatch (session->eis);

  while (TRUE)
    {
      struct eis_event *eis_event;

      eis_event = eis_get_event (session->eis);
      if (!eis_event)
        break;

      process_eis_event (session, eis_event);
      eis_event_unref (eis_event);
    }

  return G_SOURCE_CONTINUE;
}

static void
meta_input_capture_session_constructed (GObject *object)
{
  MetaInputCaptureSession *session = META_INPUT_CAPTURE_SESSION (object);
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  static unsigned int global_session_number = 0;
  int fd;
  GSource *source;

  session->object_path =
    g_strdup_printf (META_INPUT_CAPTURE_SESSION_DBUS_PATH "/u%u",
                     ++global_session_number);

  session->barriers = g_hash_table_new_full (NULL, NULL, NULL,
                                             input_capture_barrier_free);

  session->eis = eis_new (session);
  eis_log_set_handler (session->eis, meta_eis_log_handler);
  eis_log_set_priority (session->eis, EIS_LOG_PRIORITY_DEBUG);
  eis_setup_backend_fd (session->eis);

  fd = eis_get_fd (session->eis);
  source = meta_create_fd_source (fd,
                                  "[mutter] eis",
                                  meta_eis_source_prepare,
                                  meta_eis_source_dispatch,
                                  session,
                                  NULL);
  session->eis_source = source;
  g_source_attach (source, NULL);
  g_source_unref (source);

  g_signal_connect (backend, "keymap-changed",
                    G_CALLBACK (on_keymap_changed), session);

  G_OBJECT_CLASS (meta_input_capture_session_parent_class)->constructed (object);
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
  object_class->constructed = meta_input_capture_session_constructed;
  object_class->set_property = meta_input_capture_session_set_property;
  object_class->get_property = meta_input_capture_session_get_property;

  meta_dbus_session_install_properties (object_class, N_PROPS);

  quark_barrier_id =
    g_quark_from_static_string ("meta-input-capture-barrier-id-quark");
}

static void
meta_input_capture_session_init (MetaInputCaptureSession *session)
{
}

char *
meta_input_capture_session_get_object_path (MetaInputCaptureSession *session)
{
  return session->object_path;
}

static void
maybe_disable_cancelled_session (MetaInputCaptureSession *session)
{
  if (!session->cancel_requested)
    return;

  if (session->keys_pressed == 0 && session->buttons_pressed == 0)
    meta_input_capture_session_disable (session);
}

static void
update_keys_pressed (MetaInputCaptureSession *session,
                     gboolean                 is_pressed)
{
  if (is_pressed)
    session->keys_pressed++;
  else if (session->keys_pressed > 0)
    session->keys_pressed--;
  else
    g_warning ("Unbalanced key release");

  maybe_disable_cancelled_session (session);
}

static void
update_buttons_pressed (MetaInputCaptureSession *session,
                        gboolean                 is_pressed)
{
  if (is_pressed)
    session->buttons_pressed++;
  else if (session->buttons_pressed > 0)
    session->buttons_pressed--;
  else
    g_warning ("Unbalanced button release");

  maybe_disable_cancelled_session (session);
}

gboolean
meta_input_capture_session_process_event (MetaInputCaptureSession *session,
                                          const ClutterEvent      *event)
{
  double dx, dy, dx_constrained, dy_constrained;

  switch (clutter_event_type (event))
    {
    case CLUTTER_MOTION:
      if (!session->eis_pointer)
        return TRUE;

      clutter_event_get_relative_motion (event, &dx, &dy, NULL, NULL,
                                         &dx_constrained, &dy_constrained);

      eis_device_pointer_motion (session->eis_pointer,
                                 dx - dx_constrained,
                                 dy - dy_constrained);
      eis_device_frame (session->eis_pointer, eis_now (session->eis));
      break;
    case CLUTTER_BUTTON_PRESS:
      update_buttons_pressed (session, TRUE);

      if (!session->eis_pointer)
        return TRUE;

      eis_device_button_button (session->eis_pointer,
                                clutter_event_get_event_code (event),
                                true);
      eis_device_frame (session->eis_pointer, eis_now (session->eis));
      break;
    case CLUTTER_BUTTON_RELEASE:
      update_buttons_pressed (session, FALSE);

      if (!session->eis_pointer)
        return TRUE;

      eis_device_button_button (session->eis_pointer,
                                clutter_event_get_event_code (event),
                                false);
      eis_device_frame (session->eis_pointer, eis_now (session->eis));
      break;
    case CLUTTER_SCROLL:
      {
        ClutterScrollFinishFlags finish_flags;
        const double factor = 10.0;
        bool stop_x = false, stop_y = false;
        double dx, dy;

        if (!session->eis_pointer)
          return TRUE;

        finish_flags = clutter_event_get_scroll_finish_flags (event);

        if ((finish_flags & CLUTTER_SCROLL_FINISHED_HORIZONTAL))
          stop_x = true;
        if ((finish_flags & CLUTTER_SCROLL_FINISHED_HORIZONTAL))
          stop_y = true;

        if (stop_x || stop_y)
          eis_device_scroll_stop (session->eis_pointer, stop_x, stop_y);

        switch (clutter_event_get_scroll_direction (event))
          {
          case CLUTTER_SCROLL_UP:
            eis_device_scroll_discrete (session->eis_pointer, 0, -120);
            break;
          case CLUTTER_SCROLL_DOWN:
            eis_device_scroll_discrete (session->eis_pointer, 0, 120);
            break;
          case CLUTTER_SCROLL_LEFT:
            eis_device_scroll_discrete (session->eis_pointer, -120, 0);
            break;
          case CLUTTER_SCROLL_RIGHT:
            eis_device_scroll_discrete (session->eis_pointer, 120, 0);
            break;
          case CLUTTER_SCROLL_SMOOTH:
            clutter_event_get_scroll_delta (event, &dx, &dy);
            eis_device_scroll_delta (session->eis_pointer,
                                     dx * factor,
                                     dy * factor);
            break;
          }
        eis_device_frame (session->eis_pointer, eis_now (session->eis));
        break;
      }
    case CLUTTER_KEY_PRESS:
      update_keys_pressed (session, TRUE);

      if (!session->eis_keyboard)
        return TRUE;

      eis_device_keyboard_key (session->eis_keyboard,
                               clutter_event_get_event_code (event),
                               true);
      eis_device_frame (session->eis_keyboard, eis_now (session->eis));
      break;
    case CLUTTER_KEY_RELEASE:
      update_keys_pressed (session, FALSE);

      if (!session->eis_keyboard)
        return TRUE;

      eis_device_keyboard_key (session->eis_keyboard,
                               clutter_event_get_event_code (event),
                               false);
      eis_device_frame (session->eis_keyboard, eis_now (session->eis));
      break;
    default:
      return FALSE;
    }

  return TRUE;
}

void
meta_input_capture_session_notify_cancelled (MetaInputCaptureSession *session)
{
  if (session->cancel_requested)
    return;

  session->cancel_requested = TRUE;

  maybe_disable_cancelled_session (session);
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
