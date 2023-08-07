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

#include "backends/meta-input-capture-private.h"

#include "backends/meta-input-capture-session.h"
#include "backends/meta-backend-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "clutter/clutter.h"

#include "meta-dbus-input-capture.h"

#define META_INPUT_CAPTURE_DBUS_SERVICE "org.gnome.Mutter.InputCapture"
#define META_INPUT_CAPTURE_DBUS_PATH "/org/gnome/Mutter/InputCapture"

enum
{
  CANCELLED,
};

typedef enum _MetaInputCaptureCapabilities
{
  META_INPUT_CAPTURE_CAPABILITY_NONE = 1 << 0,
  META_INPUT_CAPTURE_CAPABILITY_KEYBOARD = 1 << 1,
  META_INPUT_CAPTURE_CAPABILITY_POINTER = 1 << 2,
  META_INPUT_CAPTURE_CAPABILITY_TOUCH = 1 << 3,
} MetaInputCaptureCapabilities;

struct _MetaInputCapture
{
  MetaDbusSessionManager parent;

  struct {
    MetaInputCaptureEnable enable;
    MetaInputCaptureDisable disable;
    gpointer user_data;
  } event_router;

  MetaInputCaptureSession *active_session;
};

G_DEFINE_TYPE (MetaInputCapture, meta_input_capture,
               META_TYPE_DBUS_SESSION_MANAGER)

static gboolean
handle_create_session (MetaDBusInputCapture  *skeleton,
                       GDBusMethodInvocation *invocation,
                       uint32_t               capabilities,
                       MetaInputCapture      *input_capture)
{
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (input_capture);
  MetaDbusSession *dbus_session;
  MetaInputCaptureSession *session;
  g_autoptr (GError) error = NULL;
  char *session_path;

  dbus_session =
    meta_dbus_session_manager_create_session (session_manager,
                                              invocation,
                                              &error,
                                              NULL);
  if (!dbus_session)
    {
      g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR,
                                                     G_DBUS_ERROR_FAILED,
                                                     error->message);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }
  session = META_INPUT_CAPTURE_SESSION (dbus_session);

  session_path = meta_input_capture_session_get_object_path (session);
  meta_dbus_input_capture_complete_create_session (skeleton,
                                                   invocation,
                                                   session_path);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static void
meta_input_capture_constructed (GObject *object)
{
  MetaInputCapture *input_capture = META_INPUT_CAPTURE (object);
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (input_capture);
  GDBusInterfaceSkeleton *interface_skeleton =
    meta_dbus_session_manager_get_interface_skeleton (session_manager);

  g_signal_connect (interface_skeleton, "handle-create-session",
                    G_CALLBACK (handle_create_session), input_capture);

  G_OBJECT_CLASS (meta_input_capture_parent_class)->constructed (object);
}

static void
meta_input_capture_class_init (MetaInputCaptureClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_input_capture_constructed;
}

static void
meta_input_capture_init (MetaInputCapture *input_capture)
{
}

static MetaInputCaptureCapabilities
calculate_supported_capabilities (MetaInputCapture *input_capture)
{
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (input_capture);
  MetaBackend *backend =
    meta_dbus_session_manager_get_backend (session_manager);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  ClutterVirtualDeviceType device_types;
  MetaInputCaptureCapabilities supported_capabilities =
    META_INPUT_CAPTURE_CAPABILITY_NONE;

  device_types =
    clutter_seat_get_supported_virtual_device_types (seat);

  if (device_types & CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD)
    supported_capabilities |= META_INPUT_CAPTURE_CAPABILITY_KEYBOARD;
  if (device_types & CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER)
    supported_capabilities |= META_INPUT_CAPTURE_CAPABILITY_POINTER;
  if (device_types & CLUTTER_VIRTUAL_DEVICE_TYPE_TOUCHSCREEN)
    supported_capabilities |= META_INPUT_CAPTURE_CAPABILITY_TOUCH;

  return supported_capabilities;
}

MetaInputCapture *
meta_input_capture_new (MetaBackend *backend)
{
  MetaInputCapture *input_capture;
  g_autoptr (MetaDBusInputCapture) skeleton = NULL;

  skeleton = meta_dbus_input_capture_skeleton_new ();
  input_capture = g_object_new (META_TYPE_INPUT_CAPTURE,
                                "backend", backend,
                                "service-name", META_INPUT_CAPTURE_DBUS_SERVICE,
                                "service-path", META_INPUT_CAPTURE_DBUS_PATH,
                                "session-gtype", META_TYPE_INPUT_CAPTURE_SESSION,
                                "interface-skeleton", skeleton,
                                NULL);

  meta_dbus_input_capture_set_supported_capabilities (
    META_DBUS_INPUT_CAPTURE (skeleton),
    calculate_supported_capabilities (input_capture));

  return input_capture;
}

void
meta_input_capture_set_event_router (MetaInputCapture        *input_capture,
                                     MetaInputCaptureEnable   enable,
                                     MetaInputCaptureDisable  disable,
                                     gpointer                 user_data)
{
  g_warn_if_fail (!input_capture->event_router.enable &&
                  !input_capture->event_router.disable &&
                  !input_capture->event_router.user_data);

  input_capture->event_router.enable = enable;
  input_capture->event_router.disable = disable;
  input_capture->event_router.user_data = user_data;
}

void
meta_input_capture_activate (MetaInputCapture        *input_capture,
                             MetaInputCaptureSession *session)
{
  g_return_if_fail (input_capture->event_router.enable);

  meta_topic (META_DEBUG_INPUT, "Activating input capturing");
  input_capture->active_session = session;
  input_capture->event_router.enable (input_capture,
                                      input_capture->event_router.user_data);
}

void
meta_input_capture_deactivate (MetaInputCapture        *input_capture,
                               MetaInputCaptureSession *session)
{
  g_return_if_fail (input_capture->event_router.disable);

  meta_topic (META_DEBUG_INPUT, "Deactivating input capturing");
  input_capture->event_router.disable (input_capture,
                                       input_capture->event_router.user_data);
  input_capture->active_session = NULL;
}

void
meta_input_capture_notify_cancelled (MetaInputCapture *input_capture)
{
  g_return_if_fail (input_capture->active_session);

  meta_input_capture_session_notify_cancelled (input_capture->active_session);
}

gboolean
meta_input_capture_process_event (MetaInputCapture   *input_capture,
                                  const ClutterEvent *event)
{
  g_return_val_if_fail (input_capture->active_session, FALSE);

  return meta_input_capture_session_process_event (input_capture->active_session,
                                                   event);
}
