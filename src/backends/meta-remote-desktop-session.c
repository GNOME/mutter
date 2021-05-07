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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/meta-remote-desktop-session.h"

#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixoutputstream.h>
#include <glib-unix.h>
#include <linux/input.h>
#include <stdlib.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include "backends/meta-dbus-session-watcher.h"
#include "backends/meta-screen-cast-session.h"
#include "backends/meta-remote-access-controller-private.h"
#include "backends/x11/meta-backend-x11.h"
#include "cogl/cogl.h"
#include "core/display-private.h"
#include "core/meta-selection-private.h"
#include "core/meta-selection-source-remote.h"
#include "meta/meta-backend.h"

#include "meta-dbus-remote-desktop.h"

#define META_REMOTE_DESKTOP_SESSION_DBUS_PATH "/org/gnome/Mutter/RemoteDesktop/Session"

#define TRANSFER_REQUEST_CLEANUP_TIMEOUT_MS (s2ms (15))

typedef enum _MetaRemoteDesktopNotifyAxisFlags
{
  META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_NONE = 0,
  META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_FINISH = 1 << 0,
  META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_SOURCE_WHEEL = 1 << 1,
  META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_SOURCE_FINGER = 1 << 2,
  META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_SOURCE_CONTINUOUS = 1 << 3,
} MetaRemoteDesktopNotifyAxisFlags;

typedef struct _SelectionReadData
{
  MetaRemoteDesktopSession *session;
  GOutputStream *stream;
  GCancellable *cancellable;
} SelectionReadData;

struct _MetaRemoteDesktopSession
{
  MetaDBusRemoteDesktopSessionSkeleton parent;

  MetaRemoteDesktop *remote_desktop;

  GDBusConnection *connection;
  char *peer_name;

  char *session_id;
  char *object_path;

  MetaScreenCastSession *screen_cast_session;
  gulong screen_cast_session_closed_handler_id;
  guint started : 1;

  ClutterVirtualInputDevice *virtual_pointer;
  ClutterVirtualInputDevice *virtual_keyboard;
  ClutterVirtualInputDevice *virtual_touchscreen;

  MetaRemoteDesktopSessionHandle *handle;

  gboolean is_clipboard_enabled;
  gulong owner_changed_handler_id;
  SelectionReadData *read_data;
  unsigned int transfer_serial;
  MetaSelectionSourceRemote *current_source;
  GHashTable *transfer_requests;
  guint transfer_request_timeout_id;
};

static void
meta_remote_desktop_session_init_iface (MetaDBusRemoteDesktopSessionIface *iface);

static void
meta_dbus_session_init_iface (MetaDbusSessionInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaRemoteDesktopSession,
                         meta_remote_desktop_session,
                         META_DBUS_TYPE_REMOTE_DESKTOP_SESSION_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_REMOTE_DESKTOP_SESSION,
                                                meta_remote_desktop_session_init_iface)
                         G_IMPLEMENT_INTERFACE (META_TYPE_DBUS_SESSION,
                                                meta_dbus_session_init_iface))

struct _MetaRemoteDesktopSessionHandle
{
  MetaRemoteAccessHandle parent;

  MetaRemoteDesktopSession *session;
};

G_DEFINE_TYPE (MetaRemoteDesktopSessionHandle,
               meta_remote_desktop_session_handle,
               META_TYPE_REMOTE_ACCESS_HANDLE)

static MetaRemoteDesktopSessionHandle *
meta_remote_desktop_session_handle_new (MetaRemoteDesktopSession *session);

static gboolean
meta_remote_desktop_session_is_running (MetaRemoteDesktopSession *session)
{
  return !!session->started;
}

static void
init_remote_access_handle (MetaRemoteDesktopSession *session)
{
  MetaBackend *backend = meta_get_backend ();
  MetaRemoteAccessController *remote_access_controller;
  MetaRemoteAccessHandle *remote_access_handle;

  session->handle = meta_remote_desktop_session_handle_new (session);

  remote_access_controller = meta_backend_get_remote_access_controller (backend);
  remote_access_handle = META_REMOTE_ACCESS_HANDLE (session->handle);
  meta_remote_access_controller_notify_new_handle (remote_access_controller,
                                                   remote_access_handle);
}

static void
ensure_virtual_device (MetaRemoteDesktopSession *session,
                       ClutterInputDeviceType    device_type)
{
  MetaRemoteDesktop *remote_desktop = session->remote_desktop;
  MetaBackend *backend = meta_remote_desktop_get_backend (remote_desktop);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  ClutterVirtualInputDevice **virtual_device_ptr = NULL;

  switch (device_type)
    {
    case CLUTTER_POINTER_DEVICE:
      virtual_device_ptr = &session->virtual_pointer;
      break;
    case CLUTTER_KEYBOARD_DEVICE:
      virtual_device_ptr = &session->virtual_keyboard;
      break;
    case CLUTTER_TOUCHSCREEN_DEVICE:
      virtual_device_ptr = &session->virtual_touchscreen;
      break;
    default:
      g_assert_not_reached ();
    }

  g_assert (virtual_device_ptr);

  if (*virtual_device_ptr)
    return;

  *virtual_device_ptr = clutter_seat_create_virtual_device (seat, device_type);
}

static gboolean
meta_remote_desktop_session_start (MetaRemoteDesktopSession *session,
                                   GError                  **error)
{
  g_assert (!session->started);

  if (session->screen_cast_session)
    {
      if (!meta_screen_cast_session_start (session->screen_cast_session, error))
        return FALSE;
    }

  init_remote_access_handle (session);
  session->started = TRUE;

  return TRUE;
}

void
meta_remote_desktop_session_close (MetaRemoteDesktopSession *session)
{
  MetaDBusRemoteDesktopSession *skeleton =
    META_DBUS_REMOTE_DESKTOP_SESSION (session);

  session->started = FALSE;

  if (session->screen_cast_session)
    {
      g_clear_signal_handler (&session->screen_cast_session_closed_handler_id,
                              session->screen_cast_session);
      meta_screen_cast_session_close (session->screen_cast_session);
      session->screen_cast_session = NULL;
    }

  g_clear_object (&session->virtual_pointer);
  g_clear_object (&session->virtual_keyboard);
  g_clear_object (&session->virtual_touchscreen);

  meta_dbus_session_notify_closed (META_DBUS_SESSION (session));
  meta_dbus_remote_desktop_session_emit_closed (skeleton);
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (session));

  if (session->handle)
    {
      MetaRemoteAccessHandle *remote_access_handle =
        META_REMOTE_ACCESS_HANDLE (session->handle);

      meta_remote_access_handle_notify_stopped (remote_access_handle);
    }

  g_object_unref (session);
}

char *
meta_remote_desktop_session_get_object_path (MetaRemoteDesktopSession *session)
{
  return session->object_path;
}

char *
meta_remote_desktop_session_get_session_id (MetaRemoteDesktopSession *session)
{
  return session->session_id;
}

static void
on_screen_cast_session_closed (MetaScreenCastSession    *screen_cast_session,
                               MetaRemoteDesktopSession *session)
{
  session->screen_cast_session = NULL;
  meta_remote_desktop_session_close (session);
}

gboolean
meta_remote_desktop_session_register_screen_cast (MetaRemoteDesktopSession  *session,
                                                  MetaScreenCastSession     *screen_cast_session,
                                                  GError                   **error)
{
  if (session->screen_cast_session)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Remote desktop session already have an associated "
                   "screen cast session");
      return FALSE;
    }

  session->screen_cast_session = screen_cast_session;
  session->screen_cast_session_closed_handler_id =
    g_signal_connect (screen_cast_session, "session-closed",
                      G_CALLBACK (on_screen_cast_session_closed),
                      session);

  return TRUE;
}

MetaRemoteDesktopSession *
meta_remote_desktop_session_new (MetaRemoteDesktop  *remote_desktop,
                                 const char         *peer_name,
                                 GError            **error)
{
  MetaBackend *backend = meta_remote_desktop_get_backend (remote_desktop);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  ClutterKeymap *keymap = clutter_seat_get_keymap (seat);
  GDBusInterfaceSkeleton *interface_skeleton;
  MetaRemoteDesktopSession *session;

  session = g_object_new (META_TYPE_REMOTE_DESKTOP_SESSION, NULL);

  session->remote_desktop = remote_desktop;
  session->peer_name = g_strdup (peer_name);

  interface_skeleton = G_DBUS_INTERFACE_SKELETON (session);
  session->connection = meta_remote_desktop_get_connection (remote_desktop);
  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         session->connection,
                                         session->object_path,
                                         error))
    {
      g_object_unref (session);
      return NULL;
    }

  g_object_bind_property (keymap, "caps-lock-state",
                          session, "caps-lock-state",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
  g_object_bind_property (keymap, "num-lock-state",
                          session, "num-lock-state",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  return session;
}

static gboolean
check_permission (MetaRemoteDesktopSession *session,
                  GDBusMethodInvocation    *invocation)
{
  return g_strcmp0 (session->peer_name,
                    g_dbus_method_invocation_get_sender (invocation)) == 0;
}

static gboolean
meta_remote_desktop_session_check_can_notify (MetaRemoteDesktopSession *session,
                                              GDBusMethodInvocation    *invocation)
{
  if (!session->started)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Session not started");
      return FALSE;
    }

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return FALSE;
    }

  return TRUE;
}

static gboolean
handle_start (MetaDBusRemoteDesktopSession *skeleton,
              GDBusMethodInvocation        *invocation)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  GError *error = NULL;

  if (session->started)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Already started");
      return TRUE;
    }

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  if (!meta_remote_desktop_session_start (session, &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to start remote desktop: %s",
                                             error->message);
      g_error_free (error);

      meta_remote_desktop_session_close (session);

      return TRUE;
    }

  meta_dbus_remote_desktop_session_complete_start (skeleton, invocation);

  return TRUE;
}

static gboolean
handle_stop (MetaDBusRemoteDesktopSession *skeleton,
             GDBusMethodInvocation        *invocation)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);

  if (!session->started)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Session not started");
      return TRUE;
    }

  if (!check_permission (session, invocation))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Permission denied");
      return TRUE;
    }

  meta_remote_desktop_session_close (session);

  meta_dbus_remote_desktop_session_complete_stop (skeleton, invocation);

  return TRUE;
}

static gboolean
handle_notify_keyboard_keycode (MetaDBusRemoteDesktopSession *skeleton,
                                GDBusMethodInvocation        *invocation,
                                unsigned int                  keycode,
                                gboolean                      pressed)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  ClutterKeyState state;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (pressed)
    {
      ensure_virtual_device (session, CLUTTER_KEYBOARD_DEVICE);
      state = CLUTTER_KEY_STATE_PRESSED;
    }
  else
    {
      if (!session->virtual_keyboard)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Invalid key event");
          return TRUE;
        }

      state = CLUTTER_KEY_STATE_RELEASED;
    }

  clutter_virtual_input_device_notify_key (session->virtual_keyboard,
                                           CLUTTER_CURRENT_TIME,
                                           keycode,
                                           state);

  meta_dbus_remote_desktop_session_complete_notify_keyboard_keycode (skeleton,
                                                                     invocation);
  return TRUE;
}

static gboolean
handle_notify_keyboard_keysym (MetaDBusRemoteDesktopSession *skeleton,
                               GDBusMethodInvocation        *invocation,
                               unsigned int                  keysym,
                               gboolean                      pressed)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  ClutterKeyState state;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (pressed)
    {
      ensure_virtual_device (session, CLUTTER_KEYBOARD_DEVICE);
      state = CLUTTER_KEY_STATE_PRESSED;
    }
  else
    {
      if (!session->virtual_keyboard)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Invalid key event");
          return TRUE;
        }

      state = CLUTTER_KEY_STATE_RELEASED;
    }

  clutter_virtual_input_device_notify_keyval (session->virtual_keyboard,
                                              CLUTTER_CURRENT_TIME,
                                              keysym,
                                              state);

  meta_dbus_remote_desktop_session_complete_notify_keyboard_keysym (skeleton,
                                                                    invocation);
  return TRUE;
}

/* Translation taken from the clutter evdev backend. */
static int
translate_to_clutter_button (int button)
{
  switch (button)
    {
    case BTN_LEFT:
      return CLUTTER_BUTTON_PRIMARY;
    case BTN_RIGHT:
      return CLUTTER_BUTTON_SECONDARY;
    case BTN_MIDDLE:
      return CLUTTER_BUTTON_MIDDLE;
    default:
      /*
       * For compatibility reasons, all additional buttons go after the old
       * 4-7 scroll ones.
       */
      return button - (BTN_LEFT - 1) + 4;
    }
}

static gboolean
handle_notify_pointer_button (MetaDBusRemoteDesktopSession *skeleton,
                              GDBusMethodInvocation        *invocation,
                              int                           button_code,
                              gboolean                      pressed)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  uint32_t button;
  ClutterButtonState state;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  button = translate_to_clutter_button (button_code);

  if (pressed)
    {
      ensure_virtual_device (session, CLUTTER_POINTER_DEVICE);
      state = CLUTTER_BUTTON_STATE_PRESSED;
    }
  else
    {
      if (!session->virtual_pointer)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Invalid button event");
          return TRUE;
        }

      state = CLUTTER_BUTTON_STATE_RELEASED;
    }

  clutter_virtual_input_device_notify_button (session->virtual_pointer,
                                              CLUTTER_CURRENT_TIME,
                                              button,
                                              state);

  meta_dbus_remote_desktop_session_complete_notify_pointer_button (skeleton,
                                                                   invocation);

  return TRUE;
}

static gboolean
clutter_scroll_source_from_axis_flags (MetaRemoteDesktopNotifyAxisFlags  axis_flags,
                                       ClutterScrollSource              *scroll_source)
{
  MetaRemoteDesktopNotifyAxisFlags scroll_mask;

  scroll_mask = META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_SOURCE_WHEEL |
                META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_SOURCE_FINGER |
                META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_SOURCE_CONTINUOUS;

  switch (axis_flags & scroll_mask)
    {
    case META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_SOURCE_WHEEL:
      *scroll_source = CLUTTER_SCROLL_SOURCE_WHEEL;
      return TRUE;
    case META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_NONE:
    case META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_SOURCE_FINGER:
      *scroll_source = CLUTTER_SCROLL_SOURCE_FINGER;
      return TRUE;
    case META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_SOURCE_CONTINUOUS:
      *scroll_source = CLUTTER_SCROLL_SOURCE_CONTINUOUS;
      return TRUE;
    }

  return FALSE;
}

static gboolean
handle_notify_pointer_axis (MetaDBusRemoteDesktopSession *skeleton,
                            GDBusMethodInvocation        *invocation,
                            double                        dx,
                            double                        dy,
                            uint32_t                      flags)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  ClutterScrollFinishFlags finish_flags = CLUTTER_SCROLL_FINISHED_NONE;
  ClutterScrollSource scroll_source;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (!clutter_scroll_source_from_axis_flags (flags, &scroll_source))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Invalid scroll source");
      return TRUE;
    }

  if (flags & META_REMOTE_DESKTOP_NOTIFY_AXIS_FLAGS_FINISH)
    {
      finish_flags |= (CLUTTER_SCROLL_FINISHED_HORIZONTAL |
                       CLUTTER_SCROLL_FINISHED_VERTICAL);
    }

  ensure_virtual_device (session, CLUTTER_POINTER_DEVICE);

  clutter_virtual_input_device_notify_scroll_continuous (session->virtual_pointer,
                                                         CLUTTER_CURRENT_TIME,
                                                         dx, dy,
                                                         scroll_source,
                                                         finish_flags);

  meta_dbus_remote_desktop_session_complete_notify_pointer_axis (skeleton,
                                                                 invocation);

  return TRUE;
}

static ClutterScrollDirection
discrete_steps_to_scroll_direction (unsigned int axis,
                                    int          steps)
{
  if (axis == 0 && steps < 0)
    return CLUTTER_SCROLL_UP;
  if (axis == 0 && steps > 0)
    return CLUTTER_SCROLL_DOWN;
  if (axis == 1 && steps < 0)
    return CLUTTER_SCROLL_LEFT;
  if (axis == 1 && steps > 0)
    return CLUTTER_SCROLL_RIGHT;

  g_assert_not_reached ();
  return 0;
}

static gboolean
handle_notify_pointer_axis_discrete (MetaDBusRemoteDesktopSession *skeleton,
                                     GDBusMethodInvocation        *invocation,
                                     unsigned int                  axis,
                                     int                           steps)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  ClutterScrollDirection direction;
  int step_count;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (axis > 1)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Invalid axis value");
      return TRUE;
    }

  if (steps == 0)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Invalid axis steps value");
      return TRUE;
    }

  ensure_virtual_device (session, CLUTTER_POINTER_DEVICE);

  /*
   * We don't have the actual scroll source, but only know they should be
   * considered as discrete steps. The device that produces such scroll events
   * is the scroll wheel, so pretend that is the scroll source.
   */
  direction = discrete_steps_to_scroll_direction (axis, steps);

  for (step_count = 0; step_count < abs (steps); step_count++)
    clutter_virtual_input_device_notify_discrete_scroll (session->virtual_pointer,
                                                         CLUTTER_CURRENT_TIME,
                                                         direction,
                                                         CLUTTER_SCROLL_SOURCE_WHEEL);

  meta_dbus_remote_desktop_session_complete_notify_pointer_axis_discrete (skeleton,
                                                                          invocation);

  return TRUE;
}

static gboolean
handle_notify_pointer_motion_relative (MetaDBusRemoteDesktopSession *skeleton,
                                       GDBusMethodInvocation        *invocation,
                                       double                        dx,
                                       double                        dy)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  ensure_virtual_device (session, CLUTTER_POINTER_DEVICE);

  clutter_virtual_input_device_notify_relative_motion (session->virtual_pointer,
                                                       CLUTTER_CURRENT_TIME,
                                                       dx, dy);

  meta_dbus_remote_desktop_session_complete_notify_pointer_motion_relative (skeleton,
                                                                            invocation);

  return TRUE;
}

static gboolean
handle_notify_pointer_motion_absolute (MetaDBusRemoteDesktopSession *skeleton,
                                       GDBusMethodInvocation        *invocation,
                                       const char                   *stream_path,
                                       double                        x,
                                       double                        y)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  MetaScreenCastStream *stream;
  double abs_x, abs_y;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;


  if (!session->screen_cast_session)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "No screen cast active");
      return TRUE;
    }

  stream = meta_screen_cast_session_get_stream (session->screen_cast_session,
                                                stream_path);
  if (!stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Unknown stream");
      return TRUE;
    }

  ensure_virtual_device (session, CLUTTER_POINTER_DEVICE);

  if (meta_screen_cast_stream_transform_position (stream, x, y, &abs_x, &abs_y))
    {
      clutter_virtual_input_device_notify_absolute_motion (session->virtual_pointer,
                                                           CLUTTER_CURRENT_TIME,
                                                           abs_x, abs_y);
    }
  else
    {
      meta_topic (META_DEBUG_REMOTE_DESKTOP,
                  "Dropping early absolute pointer motion (%f, %f)", x, y);
    }

  meta_dbus_remote_desktop_session_complete_notify_pointer_motion_absolute (skeleton,
                                                                            invocation);

  return TRUE;
}

static gboolean
handle_notify_touch_down (MetaDBusRemoteDesktopSession *skeleton,
                          GDBusMethodInvocation        *invocation,
                          const char                   *stream_path,
                          unsigned int                  slot,
                          double                        x,
                          double                        y)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  MetaScreenCastStream *stream;
  double abs_x, abs_y;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (slot > CLUTTER_VIRTUAL_INPUT_DEVICE_MAX_TOUCH_SLOTS)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Touch slot out of range");
      return TRUE;
    }

  if (!session->screen_cast_session)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "No screen cast active");
      return TRUE;
    }

  stream = meta_screen_cast_session_get_stream (session->screen_cast_session,
                                                stream_path);
  if (!stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Unknown stream");
      return TRUE;
    }

  ensure_virtual_device (session, CLUTTER_TOUCHSCREEN_DEVICE);

  if (meta_screen_cast_stream_transform_position (stream, x, y, &abs_x, &abs_y))
    {
      clutter_virtual_input_device_notify_touch_down (session->virtual_touchscreen,
                                                      CLUTTER_CURRENT_TIME,
                                                      slot,
                                                      abs_x, abs_y);
    }
  else
    {
      meta_topic (META_DEBUG_REMOTE_DESKTOP,
                  "Dropping early touch down (%f, %f)", x, y);
    }

  meta_dbus_remote_desktop_session_complete_notify_touch_down (skeleton,
                                                               invocation);

  return TRUE;
}

static gboolean
handle_notify_touch_motion (MetaDBusRemoteDesktopSession *skeleton,
                            GDBusMethodInvocation        *invocation,
                            const char                   *stream_path,
                            unsigned int                  slot,
                            double                        x,
                            double                        y)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  MetaScreenCastStream *stream;
  double abs_x, abs_y;

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;


  if (slot > CLUTTER_VIRTUAL_INPUT_DEVICE_MAX_TOUCH_SLOTS)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Touch slot out of range");
      return TRUE;
    }

  if (!session->screen_cast_session)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "No screen cast active");
      return TRUE;
    }

  stream = meta_screen_cast_session_get_stream (session->screen_cast_session,
                                                stream_path);
  if (!stream)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Unknown stream");
      return TRUE;
    }

  if (!session->virtual_touchscreen)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Invalid touch point");
      return TRUE;
    }

  if (meta_screen_cast_stream_transform_position (stream, x, y, &abs_x, &abs_y))
    {
      clutter_virtual_input_device_notify_touch_motion (session->virtual_touchscreen,
                                                        CLUTTER_CURRENT_TIME,
                                                        slot,
                                                        abs_x, abs_y);
    }
  else
    {
      meta_topic (META_DEBUG_REMOTE_DESKTOP,
                  "Dropping early touch motion (%f, %f)", x, y);
    }

  meta_dbus_remote_desktop_session_complete_notify_touch_motion (skeleton,
                                                                 invocation);

  return TRUE;
}

static gboolean
handle_notify_touch_up (MetaDBusRemoteDesktopSession *skeleton,
                        GDBusMethodInvocation        *invocation,
                        unsigned int                  slot)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);

  if (!meta_remote_desktop_session_check_can_notify (session, invocation))
    return TRUE;

  if (slot > CLUTTER_VIRTUAL_INPUT_DEVICE_MAX_TOUCH_SLOTS)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Touch slot out of range");
      return TRUE;
    }

  if (!session->virtual_touchscreen)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Invalid touch point");
      return TRUE;
    }

  clutter_virtual_input_device_notify_touch_up (session->virtual_touchscreen,
                                                       CLUTTER_CURRENT_TIME,
                                                       slot);

  meta_dbus_remote_desktop_session_complete_notify_touch_up (skeleton,
                                                             invocation);

  return TRUE;
}

static MetaSelectionSourceRemote *
create_remote_desktop_source (MetaRemoteDesktopSession  *session,
                              GVariant                  *mime_types_variant,
                              GError                   **error)
{
  GVariantIter iter;
  char *mime_type;
  GList *mime_types = NULL;

  g_variant_iter_init (&iter, mime_types_variant);
  if (g_variant_iter_n_children (&iter) == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "No mime types in mime types list");
      return NULL;
    }

  while (g_variant_iter_next (&iter, "s", &mime_type))
    mime_types = g_list_prepend (mime_types, mime_type);

  mime_types = g_list_reverse (mime_types);

  return meta_selection_source_remote_new (session, mime_types);
}

static const char *
mime_types_to_string (char **formats,
                      char  *buf,
                      int    buf_len)
{
  g_autofree char *mime_types_string = NULL;
  int len;

  if (!formats)
    return "N\\A";

  mime_types_string = g_strjoinv (",", formats);
  len = strlen (mime_types_string);
  strncpy (buf, mime_types_string, buf_len - 1);
  if (len >= buf_len - 1)
    buf[buf_len - 2] = '*';
  buf[buf_len - 1] = '\0';

  return buf;
}

static gboolean
is_own_source (MetaRemoteDesktopSession *session,
               MetaSelectionSource      *source)
{
  return source && source == META_SELECTION_SOURCE (session->current_source);
}

static GVariant *
generate_owner_changed_variant (char     **mime_types_array,
                                gboolean   is_own_source)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  if (mime_types_array)
    {
      g_variant_builder_add (&builder, "{sv}", "mime-types",
                             g_variant_new ("(^as)", mime_types_array));
      g_variant_builder_add (&builder, "{sv}", "session-is-owner",
                             g_variant_new_boolean (is_own_source));
    }

  return g_variant_builder_end (&builder);
}

static void
emit_owner_changed (MetaRemoteDesktopSession *session,
                    MetaSelectionSource      *owner)
{
  char log_buf[255];
  g_autofree char **mime_types_array = NULL;
  GList *l;
  int i;
  GVariant *options_variant;
  const char *object_path;

  if (owner)
    {
      GList *mime_types;

      mime_types = meta_selection_source_get_mimetypes (owner);
      mime_types_array = g_new0 (char *, g_list_length (mime_types) + 1);
      for (l = meta_selection_source_get_mimetypes (owner), i = 0;
           l;
           l = l->next, i++)
        mime_types_array[i] = l->data;
    }

  meta_topic (META_DEBUG_REMOTE_DESKTOP,
              "Clipboard owner changed, owner: %p (%s, is own? %s), mime types: [%s], "
              "notifying %s",
              owner,
              owner ? g_type_name_from_instance ((GTypeInstance *) owner)
                    : "NULL",
              is_own_source (session, owner) ? "yes" : "no",
              mime_types_to_string (mime_types_array, log_buf,
                                    G_N_ELEMENTS (log_buf)),
              session->peer_name);

  options_variant =
    generate_owner_changed_variant (mime_types_array,
                                    is_own_source (session, owner));

  object_path = g_dbus_interface_skeleton_get_object_path (
    G_DBUS_INTERFACE_SKELETON (session));
  g_dbus_connection_emit_signal (session->connection,
                                 NULL,
                                 object_path,
                                 "org.gnome.Mutter.RemoteDesktop.Session",
                                 "SelectionOwnerChanged",
                                 g_variant_new ("(@a{sv})", options_variant),
                                 NULL);
}

static void
on_selection_owner_changed (MetaSelection            *selection,
                            MetaSelectionType         selection_type,
                            MetaSelectionSource      *owner,
                            MetaRemoteDesktopSession *session)
{
  if (selection_type != META_SELECTION_CLIPBOARD)
    return;

  emit_owner_changed (session, owner);
}

static gboolean
handle_enable_clipboard (MetaDBusRemoteDesktopSession *skeleton,
                         GDBusMethodInvocation        *invocation,
                         GVariant                     *arg_options)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  GVariant *mime_types_variant;
  g_autoptr (GError) error = NULL;
  MetaDisplay *display = meta_get_display ();
  MetaSelection *selection = meta_display_get_selection (display);
  g_autoptr (MetaSelectionSourceRemote) source_remote = NULL;

  meta_topic (META_DEBUG_REMOTE_DESKTOP,
              "Enable clipboard for %s",
              g_dbus_method_invocation_get_sender (invocation));

  if (session->is_clipboard_enabled)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Already enabled");
      return TRUE;
    }

  mime_types_variant = g_variant_lookup_value (arg_options,
                                               "mime-types",
                                               G_VARIANT_TYPE_STRING_ARRAY);
  if (mime_types_variant)
    {
      source_remote = create_remote_desktop_source (session,
                                                    mime_types_variant,
                                                    &error);
      if (!source_remote)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Invalid mime type list: %s",
                                                 error->message);
          return TRUE;
        }
    }

  if (source_remote)
    {
      meta_topic (META_DEBUG_REMOTE_DESKTOP,
                  "Setting remote desktop clipboard source: %p from %s",
                  source_remote, session->peer_name);

      g_set_object (&session->current_source, source_remote);
      meta_selection_set_owner (selection,
                                META_SELECTION_CLIPBOARD,
                                META_SELECTION_SOURCE (source_remote));
    }
  else
    {
      MetaSelectionSource *owner;

      owner = meta_selection_get_current_owner (selection,
                                                META_SELECTION_CLIPBOARD);

      if (owner)
        emit_owner_changed (session, owner);
    }

  session->is_clipboard_enabled = TRUE;
  session->owner_changed_handler_id =
    g_signal_connect (selection, "owner-changed",
                      G_CALLBACK (on_selection_owner_changed),
                      session);

  meta_dbus_remote_desktop_session_complete_enable_clipboard (skeleton,
                                                              invocation);

  return TRUE;
}

static gboolean
cancel_transfer_request (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  GTask *task = G_TASK (value);
  MetaRemoteDesktopSession *session = user_data;

  meta_selection_source_remote_cancel_transfer (session->current_source,
                                                task);

  return TRUE;
}

static void
meta_remote_desktop_session_cancel_transfer_requests (MetaRemoteDesktopSession *session)
{
  g_return_if_fail (session->current_source);

  g_hash_table_foreach_remove (session->transfer_requests,
                               cancel_transfer_request,
                               session);
}

static gboolean
transfer_request_cleanup_timout (gpointer user_data)
{
  MetaRemoteDesktopSession *session = user_data;

  meta_topic (META_DEBUG_REMOTE_DESKTOP,
              "Cancel unanswered SelectionTransfer requests for %s, "
              "waited for %.02f seconds already",
              session->peer_name,
              TRANSFER_REQUEST_CLEANUP_TIMEOUT_MS / 1000.0);

  meta_remote_desktop_session_cancel_transfer_requests (session);

  session->transfer_request_timeout_id = 0;
  return G_SOURCE_REMOVE;
}

static void
reset_current_selection_source (MetaRemoteDesktopSession *session)
{
  MetaDisplay *display = meta_get_display ();
  MetaSelection *selection = meta_display_get_selection (display);

  if (!session->current_source)
    return;

  meta_selection_unset_owner (selection,
                              META_SELECTION_CLIPBOARD,
                              META_SELECTION_SOURCE (session->current_source));
  meta_remote_desktop_session_cancel_transfer_requests (session);
  g_clear_handle_id (&session->transfer_request_timeout_id, g_source_remove);
  g_clear_object (&session->current_source);
}

static void
cancel_selection_read (MetaRemoteDesktopSession *session)
{
  if (!session->read_data)
    return;

  g_cancellable_cancel (session->read_data->cancellable);
  session->read_data->session = NULL;
  session->read_data = NULL;
}

static gboolean
handle_disable_clipboard (MetaDBusRemoteDesktopSession *skeleton,
                          GDBusMethodInvocation        *invocation)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  MetaDisplay *display = meta_get_display ();
  MetaSelection *selection = meta_display_get_selection (display);

  meta_topic (META_DEBUG_REMOTE_DESKTOP,
              "Disable clipboard for %s",
              g_dbus_method_invocation_get_sender (invocation));

  if (!session->is_clipboard_enabled)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Was not enabled");
      return TRUE;
    }

  g_clear_signal_handler (&session->owner_changed_handler_id, selection);
  reset_current_selection_source (session);
  cancel_selection_read (session);

  meta_dbus_remote_desktop_session_complete_disable_clipboard (skeleton,
                                                               invocation);

  return TRUE;
}

static gboolean
handle_set_selection (MetaDBusRemoteDesktopSession *skeleton,
                      GDBusMethodInvocation        *invocation,
                      GVariant                     *arg_options)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  g_autoptr (GVariant) mime_types_variant = NULL;
  g_autoptr (GError) error = NULL;

  if (!session->is_clipboard_enabled)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Clipboard not enabled");
      return TRUE;
    }

  if (session->current_source)
    {
      meta_remote_desktop_session_cancel_transfer_requests (session);
      g_clear_handle_id (&session->transfer_request_timeout_id,
                         g_source_remove);
    }

  mime_types_variant = g_variant_lookup_value (arg_options,
                                               "mime-types",
                                               G_VARIANT_TYPE_STRING_ARRAY);
  if (mime_types_variant)
    {
      g_autoptr (MetaSelectionSourceRemote) source_remote = NULL;
      MetaDisplay *display = meta_get_display ();

      source_remote = create_remote_desktop_source (session,
                                                    mime_types_variant,
                                                    &error);
      if (!source_remote)
        {
          g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                 G_DBUS_ERROR_FAILED,
                                                 "Invalid format list: %s",
                                                 error->message);
          return TRUE;
        }

      meta_topic (META_DEBUG_REMOTE_DESKTOP,
                  "Set selection for %s to %p",
                  g_dbus_method_invocation_get_sender (invocation),
                  source_remote);

      g_set_object (&session->current_source, source_remote);
      meta_selection_set_owner (meta_display_get_selection (display),
                                META_SELECTION_CLIPBOARD,
                                META_SELECTION_SOURCE (source_remote));
    }
  else
    {
      meta_topic (META_DEBUG_REMOTE_DESKTOP,
                  "Unset selection for %s",
                  g_dbus_method_invocation_get_sender (invocation));

      reset_current_selection_source (session);
    }

  meta_dbus_remote_desktop_session_complete_set_selection (skeleton,
                                                           invocation);

  return TRUE;
}

static void
reset_transfer_cleanup_timeout (MetaRemoteDesktopSession *session)
{
  g_clear_handle_id (&session->transfer_request_timeout_id, g_source_remove);
  session->transfer_request_timeout_id =
    g_timeout_add (TRANSFER_REQUEST_CLEANUP_TIMEOUT_MS,
                   transfer_request_cleanup_timout,
                   session);
}

void
meta_remote_desktop_session_request_transfer (MetaRemoteDesktopSession *session,
                                              const char               *mime_type,
                                              GTask                    *task)
{
  const char *object_path;

  session->transfer_serial++;

  meta_topic (META_DEBUG_REMOTE_DESKTOP,
              "Emit SelectionTransfer ('%s', %u) for %s",
              mime_type,
              session->transfer_serial,
              session->peer_name);

  g_hash_table_insert (session->transfer_requests,
                       GUINT_TO_POINTER (session->transfer_serial),
                       task);
  reset_transfer_cleanup_timeout (session);

  object_path = g_dbus_interface_skeleton_get_object_path (
    G_DBUS_INTERFACE_SKELETON (session));
  g_dbus_connection_emit_signal (session->connection,
                                 NULL,
                                 object_path,
                                 "org.gnome.Mutter.RemoteDesktop.Session",
                                 "SelectionTransfer",
                                 g_variant_new ("(su)",
                                                mime_type,
                                                session->transfer_serial),
                                 NULL);
}

static gboolean
handle_selection_write (MetaDBusRemoteDesktopSession *skeleton,
                        GDBusMethodInvocation        *invocation,
                        GUnixFDList                  *fd_list_in,
                        unsigned int                  serial)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  g_autoptr (GError) error = NULL;
  int pipe_fds[2];
  g_autoptr (GUnixFDList) fd_list = NULL;
  int fd_idx;
  GVariant *fd_variant;
  GTask *task;

  meta_topic (META_DEBUG_REMOTE_DESKTOP,
              "Write selection for %s",
              g_dbus_method_invocation_get_sender (invocation));

  if (!session->is_clipboard_enabled)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Clipboard not enabled");
      return TRUE;
    }

  if (!session->current_source)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "No current selection owned");
      return TRUE;
    }

  if (!g_hash_table_steal_extended (session->transfer_requests,
                                    GUINT_TO_POINTER (serial),
                                    NULL,
                                    (gpointer *) &task))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Transfer serial %u doesn't match "
                                             "any transfer request",
                                             serial);
      return TRUE;
    }

  if (!g_unix_open_pipe (pipe_fds, FD_CLOEXEC, &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed open pipe: %s",
                                             error->message);
      return TRUE;
    }

  if (!g_unix_set_fd_nonblocking (pipe_fds[0], TRUE, &error))
    {
      close (pipe_fds[0]);
      close (pipe_fds[1]);

      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to make pipe non-blocking: %s",
                                             error->message);
      return TRUE;
    }

  fd_list = g_unix_fd_list_new ();

  fd_idx = g_unix_fd_list_append (fd_list, pipe_fds[1], NULL);
  close (pipe_fds[1]);
  fd_variant = g_variant_new_handle (fd_idx);

  meta_selection_source_remote_complete_transfer (session->current_source,
                                                  pipe_fds[0],
                                                  task);

  meta_dbus_remote_desktop_session_complete_selection_write (skeleton,
                                                             invocation,
                                                             fd_list,
                                                             fd_variant);

  return TRUE;
}

static gboolean
handle_selection_write_done (MetaDBusRemoteDesktopSession *skeleton,
                             GDBusMethodInvocation        *invocation,
                             unsigned int                  arg_serial,
                             gboolean                      arg_success)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);

  meta_topic (META_DEBUG_REMOTE_DESKTOP,
              "Write selection done for %s",
              g_dbus_method_invocation_get_sender (invocation));

  if (!session->is_clipboard_enabled)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Clipboard not enabled");
      return TRUE;
    }

  meta_dbus_remote_desktop_session_complete_selection_write_done (skeleton,
                                                                  invocation);

  return TRUE;
}

static void
transfer_cb (MetaSelection     *selection,
             GAsyncResult      *res,
             SelectionReadData *read_data)
{
  g_autoptr (GError) error = NULL;

  if (!meta_selection_transfer_finish (selection, res, &error))
    {
      g_warning ("Could not fetch selection data "
                 "for remote desktop session: %s",
                 error->message);
    }

  if (read_data->session)
    {
      meta_topic (META_DEBUG_REMOTE_DESKTOP, "Finished selection transfer for %s",
                  read_data->session->peer_name);
    }

  g_output_stream_close (read_data->stream, NULL, NULL);
  g_clear_object (&read_data->stream);
  g_clear_object (&read_data->cancellable);

  if (read_data->session)
    read_data->session->read_data = NULL;

  g_free (read_data);
}

static gboolean
handle_selection_read (MetaDBusRemoteDesktopSession *skeleton,
                       GDBusMethodInvocation        *invocation,
                       GUnixFDList                  *fd_list_in,
                       const char                   *mime_type)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (skeleton);
  MetaDisplay *display = meta_get_display ();
  MetaSelection *selection = meta_display_get_selection (display);
  MetaSelectionSource *source;
  g_autoptr (GError) error = NULL;
  int pipe_fds[2];
  g_autoptr (GUnixFDList) fd_list = NULL;
  int fd_idx;
  GVariant *fd_variant;
  SelectionReadData *read_data;

  meta_topic (META_DEBUG_REMOTE_DESKTOP,
              "Read selection for %s",
              g_dbus_method_invocation_get_sender (invocation));

  if (!session->is_clipboard_enabled)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Clipboard not enabled");
      return TRUE;
    }

  source = meta_selection_get_current_owner (selection,
                                             META_SELECTION_CLIPBOARD);
  if (!source)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FILE_NOT_FOUND,
                                             "No selection owner available");
      return TRUE;
    }

  if (is_own_source (session, source))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Tried to read own selection");
      return TRUE;
    }

  if (session->read_data)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_LIMITS_EXCEEDED,
                                             "Tried to read in parallel");
      return TRUE;
    }

  if (!g_unix_open_pipe (pipe_fds, FD_CLOEXEC, &error))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed open pipe: %s",
                                             error->message);
      return TRUE;
    }

  if (!g_unix_set_fd_nonblocking (pipe_fds[0], TRUE, &error))
    {
      close (pipe_fds[0]);
      close (pipe_fds[1]);

      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Failed to make pipe non-blocking: %s",
                                             error->message);
      return TRUE;
    }

  fd_list = g_unix_fd_list_new ();

  fd_idx = g_unix_fd_list_append (fd_list, pipe_fds[0], NULL);
  close (pipe_fds[0]);
  fd_variant = g_variant_new_handle (fd_idx);

  session->read_data = read_data = g_new0 (SelectionReadData, 1);
  read_data->session = session;
  read_data->stream = g_unix_output_stream_new (pipe_fds[1], TRUE);
  read_data->cancellable = g_cancellable_new ();
  meta_selection_transfer_async (selection,
                                 META_SELECTION_CLIPBOARD,
                                 mime_type,
                                 -1,
                                 read_data->stream,
                                 read_data->cancellable,
                                 (GAsyncReadyCallback) transfer_cb,
                                 read_data);

  meta_dbus_remote_desktop_session_complete_selection_read (skeleton,
                                                            invocation,
                                                            fd_list,
                                                            fd_variant);

  return TRUE;
}

static void
meta_remote_desktop_session_init_iface (MetaDBusRemoteDesktopSessionIface *iface)
{
  iface->handle_start = handle_start;
  iface->handle_stop = handle_stop;
  iface->handle_notify_keyboard_keycode = handle_notify_keyboard_keycode;
  iface->handle_notify_keyboard_keysym = handle_notify_keyboard_keysym;
  iface->handle_notify_pointer_button = handle_notify_pointer_button;
  iface->handle_notify_pointer_axis = handle_notify_pointer_axis;
  iface->handle_notify_pointer_axis_discrete = handle_notify_pointer_axis_discrete;
  iface->handle_notify_pointer_motion_relative = handle_notify_pointer_motion_relative;
  iface->handle_notify_pointer_motion_absolute = handle_notify_pointer_motion_absolute;
  iface->handle_notify_touch_down = handle_notify_touch_down;
  iface->handle_notify_touch_motion = handle_notify_touch_motion;
  iface->handle_notify_touch_up = handle_notify_touch_up;
  iface->handle_enable_clipboard = handle_enable_clipboard;
  iface->handle_disable_clipboard = handle_disable_clipboard;
  iface->handle_set_selection = handle_set_selection;
  iface->handle_selection_write = handle_selection_write;
  iface->handle_selection_write_done = handle_selection_write_done;
  iface->handle_selection_read = handle_selection_read;
}

static void
meta_remote_desktop_session_client_vanished (MetaDbusSession *dbus_session)
{
  meta_remote_desktop_session_close (META_REMOTE_DESKTOP_SESSION (dbus_session));
}

static void
meta_dbus_session_init_iface (MetaDbusSessionInterface *iface)
{
  iface->client_vanished = meta_remote_desktop_session_client_vanished;
}

static void
meta_remote_desktop_session_finalize (GObject *object)
{
  MetaRemoteDesktopSession *session = META_REMOTE_DESKTOP_SESSION (object);
  MetaDisplay *display = meta_get_display ();
  MetaSelection *selection = meta_display_get_selection (display);

  g_assert (!meta_remote_desktop_session_is_running (session));

  g_clear_signal_handler (&session->owner_changed_handler_id, selection);
  reset_current_selection_source (session);
  cancel_selection_read (session);
  g_hash_table_unref (session->transfer_requests);

  g_clear_object (&session->handle);
  g_free (session->peer_name);
  g_free (session->session_id);
  g_free (session->object_path);

  G_OBJECT_CLASS (meta_remote_desktop_session_parent_class)->finalize (object);
}

static void
meta_remote_desktop_session_init (MetaRemoteDesktopSession *session)
{
  MetaDBusRemoteDesktopSession *skeleton =
    META_DBUS_REMOTE_DESKTOP_SESSION  (session);
  GRand *rand;
  static unsigned int global_session_number = 0;

  rand = g_rand_new ();
  session->session_id = meta_generate_random_id (rand, 32);
  g_rand_free (rand);

  meta_dbus_remote_desktop_session_set_session_id (skeleton, session->session_id);

  session->object_path =
    g_strdup_printf (META_REMOTE_DESKTOP_SESSION_DBUS_PATH "/u%u",
                     ++global_session_number);

  session->transfer_requests = g_hash_table_new (NULL, NULL);
}

static void
meta_remote_desktop_session_class_init (MetaRemoteDesktopSessionClass *klass)
{

  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_remote_desktop_session_finalize;
}

static MetaRemoteDesktopSessionHandle *
meta_remote_desktop_session_handle_new (MetaRemoteDesktopSession *session)
{
  MetaRemoteDesktopSessionHandle *handle;

  handle = g_object_new (META_TYPE_REMOTE_DESKTOP_SESSION_HANDLE, NULL);
  handle->session = session;

  return handle;
}

static void
meta_remote_desktop_session_handle_stop (MetaRemoteAccessHandle *handle)
{
  MetaRemoteDesktopSession *session;

  session = META_REMOTE_DESKTOP_SESSION_HANDLE (handle)->session;
  if (!session)
    return;

  meta_remote_desktop_session_close (session);
}

static void
meta_remote_desktop_session_handle_init (MetaRemoteDesktopSessionHandle *handle)
{
}

static void
meta_remote_desktop_session_handle_class_init (MetaRemoteDesktopSessionHandleClass *klass)
{
  MetaRemoteAccessHandleClass *remote_access_handle_class =
    META_REMOTE_ACCESS_HANDLE_CLASS (klass);

  remote_access_handle_class->stop = meta_remote_desktop_session_handle_stop;
}
