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

#include "mdk-touch.h"

#include <linux/input-event-codes.h>

#include "mdk-monitor.h"
#include "mdk-stream.h"

#include "mdk-dbus-remote-desktop.h"

#define MAX_SLOTS 32

typedef struct _MdkSessionTouch
{
  grefcount ref_count;

  gboolean slots[MAX_SLOTS];
} MdkSessionTouch;

struct _MdkTouch
{
  GObject parent;

  MdkSession *session;
  MdkDBusRemoteDesktopSession *session_proxy;

  MdkSessionTouch *session_touch;
  int session_slots[MAX_SLOTS];

  MdkMonitor *monitor;
};

static GQuark quark_session_touch = 0;

G_DEFINE_FINAL_TYPE (MdkTouch, mdk_touch, G_TYPE_OBJECT)

static MdkSessionTouch *
mdk_touch_ensure_session_touch (MdkTouch *touch)
{
  MdkSessionTouch *session_touch;

  if (touch->session_touch)
    return touch->session_touch;

  session_touch = g_object_get_qdata (G_OBJECT (touch->session),
                                      quark_session_touch);

  if (session_touch)
    {
      touch->session_touch = session_touch;
      g_ref_count_inc (&session_touch->ref_count);
      return session_touch;
    }

  session_touch = g_new0 (MdkSessionTouch, 1);
  g_ref_count_init (&session_touch->ref_count);
  touch->session_touch = session_touch;
  g_object_set_qdata (G_OBJECT (touch->session),
                      quark_session_touch,
                      session_touch);
  return session_touch;
}

static void
mdk_touch_finalize (GObject *object)
{
  MdkTouch *touch = MDK_TOUCH (object);

  if (touch->session_touch)
    {
      if (g_ref_count_dec (&touch->session_touch->ref_count))
        {
          g_object_set_qdata (G_OBJECT (touch->session),
                              quark_session_touch,
                              NULL);
          g_clear_pointer (&touch->session_touch, g_free);
        }
    }

  G_OBJECT_CLASS (mdk_touch_parent_class)->finalize (object);
}

static void
mdk_touch_class_init (MdkTouchClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = mdk_touch_finalize;

  quark_session_touch = g_quark_from_static_string ("-mdk-session-touch");
}

static void
mdk_touch_init (MdkTouch *touch)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (touch->session_slots); i++)
    touch->session_slots[i] = -1;
}

MdkTouch *
mdk_touch_new (MdkSession                  *session,
               MdkDBusRemoteDesktopSession *session_proxy,
               MdkMonitor                  *monitor)
{
  MdkTouch *touch;

  touch = g_object_new (MDK_TYPE_TOUCH, NULL);
  touch->session = session;
  touch->session_proxy = session_proxy;
  touch->monitor = monitor;

  return touch;
}

void
mdk_touch_release_all (MdkTouch *touch)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (touch->session_slots); i++)
    {
      if (touch->session_slots[i] == -1)
        continue;

      mdk_touch_notify_up (touch, i);
    }
}

void
mdk_touch_notify_down (MdkTouch *touch,
                       int       slot,
                       double    x,
                       double    y)
{
  MdkStream *stream = mdk_monitor_get_stream (touch->monitor);
  MdkSessionTouch *session_touch;
  int session_slot = -1;
  int i;

  g_return_if_fail (slot < G_N_ELEMENTS (touch->session_slots));

  session_touch = mdk_touch_ensure_session_touch (touch);
  for (i = 0; i < G_N_ELEMENTS (session_touch->slots); i++)
    {
      if (session_touch->slots[i])
        continue;

      session_touch->slots[i] = TRUE;
      session_slot = i;
      touch->session_slots[slot] = session_slot;
      break;
    }

  if (session_slot == -1)
    {
      g_warning ("Ran out of touch session slots");
      return;
    }

  mdk_dbus_remote_desktop_session_call_notify_touch_down (
    touch->session_proxy,
    mdk_stream_get_path (stream),
    session_slot, x, y,
    NULL, NULL, NULL);
}

void
mdk_touch_notify_motion (MdkTouch *touch,
                         int       slot,
                         double    x,
                         double    y)
{
  MdkStream *stream = mdk_monitor_get_stream (touch->monitor);
  int session_slot;

  session_slot = touch->session_slots[slot];
  if (session_slot == -1)
    return;

  mdk_dbus_remote_desktop_session_call_notify_touch_motion (
    touch->session_proxy,
    mdk_stream_get_path (stream),
    session_slot, x, y,
    NULL, NULL, NULL);
}

void
mdk_touch_notify_up (MdkTouch *touch,
                     int       slot)
{
  MdkSessionTouch *session_touch;
  int session_slot;

  session_touch = mdk_touch_ensure_session_touch (touch);

  session_slot = touch->session_slots[slot];
  touch->session_slots[slot] = -1;
  session_touch->slots[session_slot] = FALSE;

  mdk_dbus_remote_desktop_session_call_notify_touch_up (
    touch->session_proxy,
    session_slot,
    NULL, NULL, NULL);
}
