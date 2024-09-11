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

#include "backends/meta-remote-desktop.h"

#include <errno.h>
#include <glib.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-cursor-renderer.h"
#include "backends/meta-remote-desktop-session.h"
#include "backends/native/meta-cursor-renderer-native.h"
#include "meta/meta-backend.h"

#include "meta-dbus-remote-desktop.h"

#define META_REMOTE_DESKTOP_DBUS_SERVICE "org.gnome.Mutter.RemoteDesktop"
#define META_REMOTE_DESKTOP_DBUS_PATH "/org/gnome/Mutter/RemoteDesktop"
#define META_REMOTE_DESKTOP_API_VERSION 1

struct _MetaRemoteDesktop
{
  MetaDbusSessionManager parent;
};

G_DEFINE_TYPE (MetaRemoteDesktop, meta_remote_desktop,
               META_TYPE_DBUS_SESSION_MANAGER)

static gboolean
handle_create_session (MetaDBusRemoteDesktop *skeleton,
                       GDBusMethodInvocation *invocation,
                       MetaRemoteDesktop     *remote_desktop)
{
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (remote_desktop);
  MetaDbusSession *dbus_session;
  MetaRemoteDesktopSession *session;
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

  session = META_REMOTE_DESKTOP_SESSION (dbus_session);
  session_path = meta_remote_desktop_session_get_object_path (session);
  meta_dbus_remote_desktop_complete_create_session (skeleton,
                                                    invocation,
                                                    session_path);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static MetaRemoteDesktopDeviceTypes
calculate_supported_device_types (MetaBackend *backend)
{
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  ClutterVirtualDeviceType device_types;
  MetaRemoteDesktopDeviceTypes supported_devices =
    META_REMOTE_DESKTOP_DEVICE_TYPE_NONE;

  device_types =
    clutter_seat_get_supported_virtual_device_types (seat);

  if (device_types & CLUTTER_VIRTUAL_DEVICE_TYPE_KEYBOARD)
    supported_devices |= META_REMOTE_DESKTOP_DEVICE_TYPE_KEYBOARD;
  if (device_types & CLUTTER_VIRTUAL_DEVICE_TYPE_POINTER)
    supported_devices |= META_REMOTE_DESKTOP_DEVICE_TYPE_POINTER;
  if (device_types & CLUTTER_VIRTUAL_DEVICE_TYPE_TOUCHSCREEN)
    supported_devices |= META_REMOTE_DESKTOP_DEVICE_TYPE_TOUCHSCREEN;

  return supported_devices;
}

static void
meta_remote_desktop_constructed (GObject *object)
{
  MetaRemoteDesktop *remote_desktop = META_REMOTE_DESKTOP (object);
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (remote_desktop);
  GDBusInterfaceSkeleton *interface_skeleton =
    meta_dbus_session_manager_get_interface_skeleton (session_manager);
  MetaDBusRemoteDesktop *interface =
    META_DBUS_REMOTE_DESKTOP (interface_skeleton);
  MetaBackend *backend = meta_dbus_session_manager_get_backend (session_manager);

  g_signal_connect (interface, "handle-create-session",
                    G_CALLBACK (handle_create_session), remote_desktop);

  meta_dbus_remote_desktop_set_supported_device_types (
    interface,
    calculate_supported_device_types (backend));
  meta_dbus_remote_desktop_set_version (
    interface,
    META_REMOTE_DESKTOP_API_VERSION);

  G_OBJECT_CLASS (meta_remote_desktop_parent_class)->constructed (object);
}

MetaRemoteDesktop *
meta_remote_desktop_new (MetaBackend *backend)
{
  MetaRemoteDesktop *remote_desktop;
  g_autoptr (MetaDBusRemoteDesktop) skeleton = NULL;

  skeleton = meta_dbus_remote_desktop_skeleton_new ();
  remote_desktop =
    g_object_new (META_TYPE_REMOTE_DESKTOP,
                  "backend", backend,
                  "service-name", META_REMOTE_DESKTOP_DBUS_SERVICE,
                  "service-path", META_REMOTE_DESKTOP_DBUS_PATH,
                  "session-gtype", META_TYPE_REMOTE_DESKTOP_SESSION,
                  "interface-skeleton", skeleton,
                  NULL);

  return remote_desktop;
}

static void
meta_remote_desktop_init (MetaRemoteDesktop *remote_desktop)
{
}

static void
meta_remote_desktop_class_init (MetaRemoteDesktopClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_remote_desktop_constructed;
}

gboolean
meta_remote_desktop_is_enabled (MetaRemoteDesktop *remote_desktop)
{
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (remote_desktop);

  return meta_dbus_session_manager_is_enabled (session_manager);
}
