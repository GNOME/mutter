/*
 * Copyright (C) 2021 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "mdk-pointer.h"

#include "mdk-monitor.h"
#include "mdk-stream.h"

#include "mdk-dbus-remote-desktop.h"

struct _MdkPointer
{
  GObject parent;

  MdkSession *session;
  MdkDBusRemoteDesktopSession *session_proxy;

  MdkMonitor *monitor;
};

G_DEFINE_FINAL_TYPE (MdkPointer, mdk_pointer, G_TYPE_OBJECT)

static void
mdk_pointer_class_init (MdkPointerClass *klass)
{
}

static void
mdk_pointer_init (MdkPointer *pointer)
{
}

MdkPointer *
mdk_pointer_new (MdkSession                  *session,
                 MdkDBusRemoteDesktopSession *session_proxy,
                 MdkMonitor                  *monitor)
{
  MdkPointer *pointer;

  pointer = g_object_new (MDK_TYPE_POINTER, NULL);
  pointer->session = session;
  pointer->session_proxy = session_proxy;
  pointer->monitor = monitor;

  return pointer;
}

void
mdk_pointer_notify_motion (MdkPointer *pointer,
                           double      x,
                           double      y)
{
  MdkStream *stream = mdk_monitor_get_stream (pointer->monitor);

  mdk_dbus_remote_desktop_session_call_notify_pointer_motion_absolute (
    pointer->session_proxy,
    mdk_stream_get_path (stream),
    x, y,
    NULL, NULL, NULL);
}

void
mdk_pointer_notify_button (MdkPointer *pointer,
                           int32_t     button,
                           int         state)
{
  mdk_dbus_remote_desktop_session_call_notify_pointer_button (
    pointer->session_proxy,
    button, state,
    NULL, NULL, NULL);
}
