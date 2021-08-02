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

#include <linux/input-event-codes.h>

#include "mdk-monitor.h"
#include "mdk-stream.h"

#include "mdk-dbus-remote-desktop.h"

typedef struct _MdkSessionPointer
{
  grefcount ref_count;
  int button_count[KEY_CNT];
} MdkSessionPointer;

struct _MdkPointer
{
  GObject parent;

  MdkSession *session;
  MdkDBusRemoteDesktopSession *session_proxy;

  MdkSessionPointer *session_pointer;

  gboolean button_pressed[KEY_CNT];

  MdkMonitor *monitor;
};

static GQuark quark_session_pointer = 0;

G_DEFINE_FINAL_TYPE (MdkPointer, mdk_pointer, G_TYPE_OBJECT)

static MdkSessionPointer *
mdk_pointer_ensure_session_pointer (MdkPointer *pointer)
{
  MdkSessionPointer *session_pointer;

  if (pointer->session_pointer)
    return pointer->session_pointer;

  session_pointer = g_object_get_qdata (G_OBJECT (pointer->session),
                                        quark_session_pointer);

  if (session_pointer)
    {
      pointer->session_pointer = session_pointer;
      g_ref_count_inc (&session_pointer->ref_count);
      return session_pointer;
    }

  session_pointer = g_new0 (MdkSessionPointer, 1);
  g_ref_count_init (&session_pointer->ref_count);
  pointer->session_pointer = session_pointer;
  g_object_set_qdata (G_OBJECT (pointer->session),
                      quark_session_pointer,
                      session_pointer);
  return session_pointer;
}

static void
mdk_pointer_finalize (GObject *object)
{
  MdkPointer *pointer = MDK_POINTER (object);

  if (pointer->session_pointer)
    {
      if (g_ref_count_dec (&pointer->session_pointer->ref_count))
        {
          g_object_set_qdata (G_OBJECT (pointer->session),
                              quark_session_pointer,
                              NULL);
          g_clear_pointer (&pointer->session_pointer, g_free);
        }
    }

  G_OBJECT_CLASS (mdk_pointer_parent_class)->finalize (object);
}

static void
mdk_pointer_class_init (MdkPointerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mdk_pointer_finalize;

  quark_session_pointer = g_quark_from_static_string ("-mdk-session-pointer");
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
mdk_pointer_release_all (MdkPointer *pointer)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (pointer->button_pressed); i++)
    {
      if (!pointer->button_pressed[i])
        continue;

      mdk_pointer_notify_button (pointer, i, 0);
    }
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
  MdkSessionPointer *session_pointer;

  if (button > G_N_ELEMENTS (pointer->button_pressed))
    {
      g_warning ("Unknown button key code 0x%x, ignoring", button);
      return;
    }

  if (state)
    {
      g_return_if_fail (!pointer->button_pressed[button]);
      pointer->button_pressed[button] = TRUE;
    }
  else
    {
      if (!pointer->button_pressed[button])
        return;

      pointer->button_pressed[button] = FALSE;
    }

  session_pointer = mdk_pointer_ensure_session_pointer (pointer);

  if (state)
    {
      session_pointer->button_count[button]++;
    }
  else
    {
      g_return_if_fail (session_pointer->button_count[button] > 0);
      session_pointer->button_count[button]--;
    }

  if (session_pointer->button_count[button] > 1)
    return;

  g_debug ("Emit pointer button 0x%x %s",
           button,
           state ? "pressed" : "released");

  mdk_dbus_remote_desktop_session_call_notify_pointer_button (
    pointer->session_proxy,
    button, state,
    NULL, NULL, NULL);
}

void
mdk_pointer_notify_scroll (MdkPointer *pointer,
                           double      dx,
                           double      dy)
{
  mdk_dbus_remote_desktop_session_call_notify_pointer_axis (
    pointer->session_proxy,
    dx, dy,
    MDK_SCROLL_FLAG_SOURCE_FINGER,
    NULL, NULL, NULL);
}

void
mdk_pointer_notify_scroll_end (MdkPointer *pointer)
{
  mdk_dbus_remote_desktop_session_call_notify_pointer_axis (
    pointer->session_proxy,
    0, 0,
    MDK_SCROLL_FLAG_FINISH | MDK_SCROLL_FLAG_SOURCE_FINGER,
    NULL, NULL, NULL);
}

void
mdk_pointer_notify_scroll_discrete (MdkPointer         *pointer,
                                    GdkScrollDirection  direction)
{
  unsigned int axis;
  int steps;

  switch (direction)
    {
    case GDK_SCROLL_UP:
      axis = 0;
      steps = -1;
      break;
    case GDK_SCROLL_DOWN:
      axis = 0;
      steps = 1;
      break;
    case GDK_SCROLL_LEFT:
      axis = 1;
      steps = -1;
      break;
    case GDK_SCROLL_RIGHT:
      axis = 1;
      steps = 1;
      break;
    case GDK_SCROLL_SMOOTH:
    default:
      g_assert_not_reached ();
    }

  mdk_dbus_remote_desktop_session_call_notify_pointer_axis_discrete (
    pointer->session_proxy,
    axis,
    steps,
    NULL, NULL, NULL);
}
