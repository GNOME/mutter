/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015-2017 Red Hat Inc.
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

#include "backends/meta-screen-cast-session.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-dbus-session-manager.h"
#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-remote-access-controller-private.h"
#include "backends/meta-remote-desktop-session.h"
#include "backends/meta-screen-cast-area-stream.h"
#include "backends/meta-screen-cast-monitor-stream.h"
#include "backends/meta-screen-cast-stream.h"
#include "backends/meta-screen-cast-virtual-stream.h"
#include "backends/meta-screen-cast-window-stream.h"
#include "core/display-private.h"

#include "meta-private-enum-types.h"

#define META_SCREEN_CAST_SESSION_DBUS_PATH "/org/gnome/Mutter/ScreenCast/Session"

enum
{
  STREAM_ADDED,
  STREAM_REMOVED,

  N_SIGNALS,
};

static int signals[N_SIGNALS];

enum
{
  PROP_0,

  PROP_REMOTE_DESKTOP_SESSION,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

struct _MetaScreenCastSession
{
  MetaDBusScreenCastSessionSkeleton parent;

  MetaDbusSessionManager *session_manager;

  char *peer_name;

  MetaScreenCastSessionType session_type;
  char *object_path;
  char *session_id;

  GList *streams;

  MetaScreenCastSessionHandle *handle;

  gboolean is_active;
  gboolean disable_animations;

  MetaRemoteDesktopSession *remote_desktop_session;
};

static void initable_init_iface (GInitableIface *iface);

static void
meta_screen_cast_session_init_iface (MetaDBusScreenCastSessionIface *iface);

static void
meta_dbus_session_init_iface (MetaDbusSessionInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaScreenCastSession,
                         meta_screen_cast_session,
                         META_DBUS_TYPE_SCREEN_CAST_SESSION_SKELETON,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_init_iface)
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_SCREEN_CAST_SESSION,
                                                meta_screen_cast_session_init_iface)
                         G_IMPLEMENT_INTERFACE (META_TYPE_DBUS_SESSION,
                                                meta_dbus_session_init_iface))

struct _MetaScreenCastSessionHandle
{
  MetaRemoteAccessHandle parent;

  MetaScreenCastSession *session;
};

G_DEFINE_TYPE (MetaScreenCastSessionHandle,
               meta_screen_cast_session_handle,
               META_TYPE_REMOTE_ACCESS_HANDLE)

static MetaScreenCastSessionHandle *
meta_screen_cast_session_handle_new (MetaScreenCastSession *session);

static void
init_remote_access_handle (MetaScreenCastSession *session)
{
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  MetaRemoteAccessController *remote_access_controller;
  MetaRemoteAccessHandle *remote_access_handle;

  session->handle = meta_screen_cast_session_handle_new (session);

  remote_access_controller = meta_backend_get_remote_access_controller (backend);
  remote_access_handle = META_REMOTE_ACCESS_HANDLE (session->handle);

  meta_remote_access_handle_set_disable_animations (remote_access_handle,
                                                    session->disable_animations);

  meta_remote_access_controller_notify_new_handle (remote_access_controller,
                                                   remote_access_handle);
}

gboolean
meta_screen_cast_session_start (MetaScreenCastSession  *session,
                                GError                **error)
{
  GList *l;

  for (l = session->streams; l; l = l->next)
    {
      MetaScreenCastStream *stream = l->data;

      if (!meta_screen_cast_stream_start (stream, error))
        return FALSE;
    }

  init_remote_access_handle (session);

  session->is_active = TRUE;

  return TRUE;
}

gboolean
meta_screen_cast_session_is_active (MetaScreenCastSession *session)
{
  return session->is_active;
}

static void
meta_screen_cast_session_close (MetaDbusSession *dbus_session)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (dbus_session);
  MetaDBusScreenCastSession *skeleton = META_DBUS_SCREEN_CAST_SESSION (session);

  session->is_active = FALSE;

  g_list_free_full (session->streams, g_object_unref);

  meta_dbus_session_notify_closed (META_DBUS_SESSION (session));

  switch (session->session_type)
    {
    case META_SCREEN_CAST_SESSION_TYPE_NORMAL:
      meta_dbus_screen_cast_session_emit_closed (skeleton);
      break;
    case META_SCREEN_CAST_SESSION_TYPE_REMOTE_DESKTOP:
      break;
    }

  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (session));

  if (session->handle)
    {
      MetaRemoteAccessHandle *remote_access_handle =
        META_REMOTE_ACCESS_HANDLE (session->handle);

      meta_remote_access_handle_notify_stopped (remote_access_handle);
    }

  g_object_unref (session);
}

GList *
meta_screen_cast_session_peek_streams (MetaScreenCastSession *session)
{
  return session->streams;
}

MetaScreenCastStream *
meta_screen_cast_session_get_stream (MetaScreenCastSession *session,
                                     const char            *path)
{
  GList *l;

  for (l = session->streams; l; l = l->next)
    {
      MetaScreenCastStream *stream = l->data;

      if (g_strcmp0 (meta_screen_cast_stream_get_object_path (stream),
                     path) == 0)
        return stream;
    }

  return NULL;
}

MetaScreenCast *
meta_screen_cast_session_get_screen_cast (MetaScreenCastSession *session)
{
  return META_SCREEN_CAST (session->session_manager);
}

void
meta_screen_cast_session_set_disable_animations (MetaScreenCastSession *session,
                                                 gboolean               disable_animations)
{
  session->disable_animations = disable_animations;
}

char *
meta_screen_cast_session_get_object_path (MetaScreenCastSession *session)
{
  return session->object_path;
}

char *
meta_screen_cast_session_get_peer_name (MetaScreenCastSession *session)
{
  return session->peer_name;
}

MetaScreenCastSessionType
meta_screen_cast_session_get_session_type (MetaScreenCastSession *session)
{
  return session->session_type;
}

MetaRemoteDesktopSession *
meta_screen_cast_session_get_remote_desktop_session (MetaScreenCastSession *session)
{
  return session->remote_desktop_session;
}

static gboolean
check_permission (MetaScreenCastSession *session,
                  GDBusMethodInvocation *invocation)
{
  return g_strcmp0 (session->peer_name,
                    g_dbus_method_invocation_get_sender (invocation)) == 0;
}

static gboolean
meta_screen_cast_session_initable_init (GInitable     *initable,
                                        GCancellable  *cancellable,
                                        GError       **error)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (initable);
  GDBusInterfaceSkeleton *interface_skeleton;
  GDBusConnection *connection;
  static unsigned int global_session_number = 0;

  if (session->remote_desktop_session)
    {
      if (!meta_remote_desktop_session_register_screen_cast (session->remote_desktop_session,
                                                             session,
                                                             error))
        return FALSE;
    }

  session->object_path =
    g_strdup_printf (META_SCREEN_CAST_SESSION_DBUS_PATH "/u%u",
                     ++global_session_number);

  interface_skeleton = G_DBUS_INTERFACE_SKELETON (session);
  connection =
    meta_dbus_session_manager_get_connection (session->session_manager);
  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         session->object_path,
                                         error))
    return FALSE;

  return TRUE;
}

static void
initable_init_iface (GInitableIface *iface)
{
  iface->init = meta_screen_cast_session_initable_init;
}

static gboolean
handle_start (MetaDBusScreenCastSession *skeleton,
              GDBusMethodInvocation     *invocation)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (skeleton);
  GError *error = NULL;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  switch (session->session_type)
    {
    case META_SCREEN_CAST_SESSION_TYPE_NORMAL:
      break;
    case META_SCREEN_CAST_SESSION_TYPE_REMOTE_DESKTOP:
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Must be started from remote desktop session");
      return TRUE;
    }

  if (!meta_screen_cast_session_start (session, &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to start screen cast: %s",
                                             error->message);
      g_error_free (error);

      return TRUE;
    }

  meta_dbus_screen_cast_session_complete_start (skeleton, invocation);

  return TRUE;
}

static gboolean
handle_stop (MetaDBusScreenCastSession *skeleton,
             GDBusMethodInvocation     *invocation)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (skeleton);

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  switch (session->session_type)
    {
    case META_SCREEN_CAST_SESSION_TYPE_NORMAL:
      break;
    case META_SCREEN_CAST_SESSION_TYPE_REMOTE_DESKTOP:
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Must be stopped from remote desktop session");
      return TRUE;
    }

  meta_dbus_session_close (META_DBUS_SESSION (session));

  meta_dbus_screen_cast_session_complete_stop (skeleton, invocation);

  return TRUE;
}

static void
on_stream_closed (MetaScreenCastStream  *stream,
                  MetaScreenCastSession *session)
{
  session->streams = g_list_remove (session->streams, stream);
  g_signal_emit (session, signals[STREAM_REMOVED], 0, stream);
  g_object_unref (stream);

  switch (session->session_type)
    {
    case META_SCREEN_CAST_SESSION_TYPE_NORMAL:
      meta_dbus_session_close (META_DBUS_SESSION (session));
      break;
    case META_SCREEN_CAST_SESSION_TYPE_REMOTE_DESKTOP:
      break;
    }
}

static gboolean
is_valid_cursor_mode (MetaScreenCastCursorMode cursor_mode)
{
  switch (cursor_mode)
    {
    case META_SCREEN_CAST_CURSOR_MODE_HIDDEN:
    case META_SCREEN_CAST_CURSOR_MODE_EMBEDDED:
    case META_SCREEN_CAST_CURSOR_MODE_METADATA:
      return TRUE;
    }

  return FALSE;
}

static void
add_stream (MetaScreenCastSession *session,
            MetaScreenCastStream  *stream)
{
  session->streams = g_list_append (session->streams, stream);
  g_signal_emit (session, signals[STREAM_ADDED], 0, stream);

  g_signal_connect (stream, "closed", G_CALLBACK (on_stream_closed), session);
}

static gboolean
handle_record_monitor (MetaDBusScreenCastSession *skeleton,
                       GDBusMethodInvocation     *invocation,
                       const char                *connector,
                       GVariant                  *properties_variant)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (skeleton);
  GDBusInterfaceSkeleton *interface_skeleton;
  GDBusConnection *connection;
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitor *monitor;
  MetaScreenCastCursorMode cursor_mode;
  gboolean is_recording;
  MetaScreenCastFlag flags;
  ClutterStage *stage;
  GError *error = NULL;
  MetaScreenCastMonitorStream *monitor_stream;
  MetaScreenCastStream *stream;
  char *stream_path;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  interface_skeleton = G_DBUS_INTERFACE_SKELETON (skeleton);
  connection = g_dbus_interface_skeleton_get_connection (interface_skeleton);

  if (g_str_equal (connector, ""))
    monitor = meta_monitor_manager_get_primary_monitor (monitor_manager);
  else
    monitor = meta_monitor_manager_get_monitor_from_connector (monitor_manager,
                                                               connector);

  if (!monitor)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Unknown monitor");
      return TRUE;
    }

  if (!g_variant_lookup (properties_variant, "cursor-mode", "u", &cursor_mode))
    {
      cursor_mode = META_SCREEN_CAST_CURSOR_MODE_HIDDEN;
    }
  else
    {
      if (!is_valid_cursor_mode (cursor_mode))
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Unknown cursor mode");
          return TRUE;
        }
    }

  if (!g_variant_lookup (properties_variant, "is-recording", "b", &is_recording))
    is_recording = FALSE;

  stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

  flags = META_SCREEN_CAST_FLAG_NONE;
  if (is_recording)
    flags |= META_SCREEN_CAST_FLAG_IS_RECORDING;

  monitor_stream = meta_screen_cast_monitor_stream_new (session,
                                                        connection,
                                                        monitor,
                                                        stage,
                                                        cursor_mode,
                                                        flags,
                                                        &error);
  if (!monitor_stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to record monitor: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }

  stream = META_SCREEN_CAST_STREAM (monitor_stream);
  stream_path = meta_screen_cast_stream_get_object_path (stream);

  add_stream (session, stream);

  meta_dbus_screen_cast_session_complete_record_monitor (skeleton,
                                                         invocation,
                                                         stream_path);

  return TRUE;
}

static gboolean
handle_record_window (MetaDBusScreenCastSession *skeleton,
                      GDBusMethodInvocation     *invocation,
                      GVariant                  *properties_variant)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (skeleton);
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session->session_manager);
  MetaContext *context = meta_backend_get_context (backend);
  MetaDisplay *display = meta_context_get_display (context);
  GDBusInterfaceSkeleton *interface_skeleton;
  GDBusConnection *connection;
  MetaWindow *window;
  MetaScreenCastCursorMode cursor_mode;
  gboolean is_recording;
  MetaScreenCastFlag flags;
  GError *error = NULL;
  GVariant *window_id_variant = NULL;
  MetaScreenCastWindowStream *window_stream;
  MetaScreenCastStream *stream;
  char *stream_path;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  if (properties_variant)
    window_id_variant = g_variant_lookup_value (properties_variant,
                                                "window-id",
                                                G_VARIANT_TYPE ("t"));

  if (window_id_variant)
    {
      uint64_t window_id;

      g_variant_get (window_id_variant, "t", &window_id);
      window = meta_display_get_window_from_id (display, window_id);
    }
  else
    {
      window = meta_display_get_focus_window (display);
    }

  if (!window)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Window not found");
      return TRUE;
    }

  if (!g_variant_lookup (properties_variant, "cursor-mode", "u", &cursor_mode))
    {
      cursor_mode = META_SCREEN_CAST_CURSOR_MODE_HIDDEN;
    }
  else
    {
      if (!is_valid_cursor_mode (cursor_mode))
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Unknown cursor mode");
          return TRUE;
        }
    }

  if (!g_variant_lookup (properties_variant, "is-recording", "b", &is_recording))
    is_recording = FALSE;

  interface_skeleton = G_DBUS_INTERFACE_SKELETON (skeleton);
  connection = g_dbus_interface_skeleton_get_connection (interface_skeleton);

  flags = META_SCREEN_CAST_FLAG_NONE;
  if (is_recording)
    flags |= META_SCREEN_CAST_FLAG_IS_RECORDING;

  window_stream = meta_screen_cast_window_stream_new (session,
                                                      connection,
                                                      window,
                                                      cursor_mode,
                                                      flags,
                                                      &error);
  if (!window_stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to record window: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }

  stream = META_SCREEN_CAST_STREAM (window_stream);
  stream_path = meta_screen_cast_stream_get_object_path (stream);

  add_stream (session, stream);

  meta_dbus_screen_cast_session_complete_record_window (skeleton,
                                                        invocation,
                                                        stream_path);

  return TRUE;
}

static gboolean
handle_record_area (MetaDBusScreenCastSession *skeleton,
                    GDBusMethodInvocation     *invocation,
                    int                        x,
                    int                        y,
                    int                        width,
                    int                        height,
                    GVariant                  *properties_variant)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (skeleton);
  GDBusInterfaceSkeleton *interface_skeleton;
  GDBusConnection *connection;
  MetaBackend *backend;
  ClutterStage *stage;
  MetaScreenCastCursorMode cursor_mode;
  gboolean is_recording;
  MetaScreenCastFlag flags;
  g_autoptr (GError) error = NULL;
  MtkRectangle rect;
  MetaScreenCastAreaStream *area_stream;
  MetaScreenCastStream *stream;
  char *stream_path;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  if (!g_variant_lookup (properties_variant, "cursor-mode", "u", &cursor_mode))
    {
      cursor_mode = META_SCREEN_CAST_CURSOR_MODE_HIDDEN;
    }
  else
    {
      if (!is_valid_cursor_mode (cursor_mode))
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Unknown cursor mode");
          return TRUE;
        }
    }

  if (!g_variant_lookup (properties_variant, "is-recording", "b", &is_recording))
    is_recording = FALSE;

  interface_skeleton = G_DBUS_INTERFACE_SKELETON (skeleton);
  connection = g_dbus_interface_skeleton_get_connection (interface_skeleton);
  backend = meta_dbus_session_manager_get_backend (session->session_manager);
  stage = CLUTTER_STAGE (meta_backend_get_stage (backend));

  flags = META_SCREEN_CAST_FLAG_NONE;
  if (is_recording)
    flags |= META_SCREEN_CAST_FLAG_IS_RECORDING;

  rect = (MtkRectangle) {
    .x = x,
    .y = y,
    .width = width,
    .height = height
  };
  area_stream = meta_screen_cast_area_stream_new (session,
                                                  connection,
                                                  &rect,
                                                  stage,
                                                  cursor_mode,
                                                  flags,
                                                  &error);
  if (!area_stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to record area: %s",
                                             error->message);
      return TRUE;
    }

  stream = META_SCREEN_CAST_STREAM (area_stream);
  stream_path = meta_screen_cast_stream_get_object_path (stream);

  add_stream (session, stream);

  meta_dbus_screen_cast_session_complete_record_area (skeleton,
                                                      invocation,
                                                      stream_path);

  return TRUE;
}

static gboolean
handle_record_virtual (MetaDBusScreenCastSession *skeleton,
                       GDBusMethodInvocation     *invocation,
                       GVariant                  *properties_variant)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (skeleton);
  GDBusInterfaceSkeleton *interface_skeleton;
  GDBusConnection *connection;
  MetaScreenCastCursorMode cursor_mode;
  gboolean is_platform;
  MetaScreenCastFlag flags;
  g_autoptr (GError) error = NULL;
  MetaScreenCastVirtualStream *virtual_stream;
  MetaScreenCastStream *stream;
  char *stream_path;

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  if (!g_variant_lookup (properties_variant, "cursor-mode", "u", &cursor_mode))
    {
      cursor_mode = META_SCREEN_CAST_CURSOR_MODE_HIDDEN;
    }
  else
    {
      if (!is_valid_cursor_mode (cursor_mode))
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Unknown cursor mode");
          return TRUE;
        }
    }

  if (!g_variant_lookup (properties_variant, "is-platform", "b", &is_platform))
    is_platform = FALSE;

  interface_skeleton = G_DBUS_INTERFACE_SKELETON (skeleton);
  connection = g_dbus_interface_skeleton_get_connection (interface_skeleton);

  flags = META_SCREEN_CAST_FLAG_NONE;
  if (is_platform)
    flags |= META_SCREEN_CAST_FLAG_IS_PLATFORM;

  virtual_stream = meta_screen_cast_virtual_stream_new (session,
                                                        connection,
                                                        cursor_mode,
                                                        flags,
                                                        &error);
  if (!virtual_stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to record virtual: %s",
                                             error->message);
      return TRUE;
    }

  stream = META_SCREEN_CAST_STREAM (virtual_stream);
  stream_path = meta_screen_cast_stream_get_object_path (stream);

  add_stream (session, stream);

  meta_dbus_screen_cast_session_complete_record_virtual (skeleton,
                                                         invocation,
                                                         stream_path);

  return TRUE;
}

static void
meta_screen_cast_session_init_iface (MetaDBusScreenCastSessionIface *iface)
{
  iface->handle_start = handle_start;
  iface->handle_stop = handle_stop;
  iface->handle_record_monitor = handle_record_monitor;
  iface->handle_record_window = handle_record_window;
  iface->handle_record_area = handle_record_area;
  iface->handle_record_virtual = handle_record_virtual;
}

static void
meta_dbus_session_init_iface (MetaDbusSessionInterface *iface)
{
  iface->close = meta_screen_cast_session_close;
}

static void
meta_screen_cast_session_finalize (GObject *object)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (object);

  g_clear_object (&session->handle);
  g_free (session->peer_name);
  g_free (session->object_path);
  g_free (session->session_id);

  G_OBJECT_CLASS (meta_screen_cast_session_parent_class)->finalize (object);
}

static void
meta_screen_cast_session_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (object);

  switch (prop_id)
    {
    case PROP_REMOTE_DESKTOP_SESSION:
      session->remote_desktop_session = g_value_get_object (value);
      if (session->remote_desktop_session)
        session->session_type = META_SCREEN_CAST_SESSION_TYPE_REMOTE_DESKTOP;
      else
        session->session_type = META_SCREEN_CAST_SESSION_TYPE_NORMAL;
      break;

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
meta_screen_cast_session_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  MetaScreenCastSession *session = META_SCREEN_CAST_SESSION (object);

  switch (prop_id)
    {
    case PROP_REMOTE_DESKTOP_SESSION:
      g_value_set_object (value, session->remote_desktop_session);
      break;

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
meta_screen_cast_session_init (MetaScreenCastSession *session)
{
}

static void
meta_screen_cast_session_class_init (MetaScreenCastSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_screen_cast_session_finalize;
  object_class->set_property = meta_screen_cast_session_set_property;
  object_class->get_property = meta_screen_cast_session_get_property;

  obj_props[PROP_REMOTE_DESKTOP_SESSION] =
    g_param_spec_object ("remote-desktop-session", NULL, NULL,
                         META_TYPE_REMOTE_DESKTOP_SESSION,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);
  meta_dbus_session_install_properties (object_class, N_PROPS);

  signals[STREAM_ADDED] =
    g_signal_new ("stream-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_SCREEN_CAST_STREAM);
  signals[STREAM_REMOVED] =
    g_signal_new ("stream-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  META_TYPE_SCREEN_CAST_STREAM);
}

static gboolean
meta_screen_cast_session_is_recording (MetaScreenCastSession *session)
{
  GList *l;

  if (!session->streams)
    return FALSE;

  for (l = session->streams; l; l = l->next)
    {
      MetaScreenCastStream *stream = l->data;
      MetaScreenCastFlag flags;

      flags = meta_screen_cast_stream_get_flags (stream);
      if (!(flags & META_SCREEN_CAST_FLAG_IS_RECORDING))
        return FALSE;
    }

  return TRUE;
}

static MetaScreenCastSessionHandle *
meta_screen_cast_session_handle_new (MetaScreenCastSession *session)
{
  MetaScreenCastSessionHandle *handle;
  gboolean is_recording;

  is_recording = meta_screen_cast_session_is_recording (session);
  handle = g_object_new (META_TYPE_SCREEN_CAST_SESSION_HANDLE,
                         "is-recording", is_recording,
                         NULL);
  handle->session = session;

  return handle;
}

static void
meta_screen_cast_session_handle_stop (MetaRemoteAccessHandle *handle)
{
  MetaScreenCastSession *session;

  session = META_SCREEN_CAST_SESSION_HANDLE (handle)->session;
  if (!session)
    return;

  meta_dbus_session_queue_close (META_DBUS_SESSION (session));
}

static void
meta_screen_cast_session_handle_init (MetaScreenCastSessionHandle *handle)
{
}

static void
meta_screen_cast_session_handle_class_init (MetaScreenCastSessionHandleClass *klass)
{
  MetaRemoteAccessHandleClass *remote_access_handle_class =
    META_REMOTE_ACCESS_HANDLE_CLASS (klass);

  remote_access_handle_class->stop = meta_screen_cast_session_handle_stop;
}
