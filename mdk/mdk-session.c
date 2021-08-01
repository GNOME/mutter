/*
 * Copyright (C) 2021-2024 Red Hat Inc.
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

#include "mdk-session.h"

#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include "mdk-context.h"
#include "mdk-keyboard.h"
#include "mdk-pointer.h"
#include "mdk-stream.h"

#include "mdk-dbus-remote-desktop.h"
#include "mdk-dbus-screen-cast.h"

typedef enum _MdkScreenCastCursorMode
{
  MDK_SCREEN_CAST_CURSOR_MODE_HIDDEN = 0,
  MDK_SCREEN_CAST_CURSOR_MODE_EMBEDDED = 1,
  MDK_SCREEN_CAST_CURSOR_MODE_METADATA = 2,
} MdkScreenCastCursorMode;

enum
{
  PROP_0,

  PROP_CONTEXT,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

enum
{
  CLOSED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MdkSession
{
  GObject parent;

  MdkContext *context;

  GCancellable *init_cancellable;
  GTask *init_task;

  MdkDBusRemoteDesktop *remote_desktop_proxy;
  MdkDBusScreenCast *screen_cast_proxy;
  MdkDBusRemoteDesktopSession *remote_desktop_session_proxy;
  MdkDBusScreenCastSession *screen_cast_session_proxy;
};

static void
async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (MdkSession, mdk_session, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                                      async_initable_iface_init))

static void
notify_init_failed (MdkSession   *session,
                    const GError *error)
{
  g_task_return_new_error (g_steal_pointer (&session->init_task),
                           error->domain,
                           error->code,
                           "Failed to initialize MDK session: %s",
                           error->message);
}

static void
on_session_closed (MdkDBusRemoteDesktopSession *remote_desktop_session_proxy,
                   MdkSession                  *session)
{
  g_signal_emit (session, signals[CLOSED], 0);
}

static gboolean
init_session_in_thread (MdkSession    *session,
                        GCancellable  *cancellable,
                        GError       **error)
{
  g_autofree char *session_path = NULL;
  GVariantBuilder builder;
  const char *remote_desktop_session_id;
  GVariant *properties;

  g_debug ("Opening remote desktop and screen cast session");

  if (!mdk_dbus_remote_desktop_call_create_session_sync (
        session->remote_desktop_proxy,
        &session_path,
        cancellable,
        error))
    return FALSE;

  session->remote_desktop_session_proxy =
    mdk_dbus_remote_desktop_session_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.RemoteDesktop",
      session_path,
      cancellable,
      error);
  if (!session->remote_desktop_session_proxy)
    return FALSE;

  g_clear_pointer (&session_path, g_free);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  remote_desktop_session_id =
    mdk_dbus_remote_desktop_session_get_session_id (
      session->remote_desktop_session_proxy);
  g_variant_builder_add (&builder, "{sv}",
                         "remote-desktop-session-id",
                         g_variant_new_string (remote_desktop_session_id));
  properties = g_variant_builder_end (&builder);

  if (!mdk_dbus_screen_cast_call_create_session_sync (
        session->screen_cast_proxy,
        properties,
        &session_path,
        cancellable,
        error))
    return FALSE;

  session->screen_cast_session_proxy =
    mdk_dbus_screen_cast_session_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.ScreenCast",
      session_path,
      cancellable,
      error);
  if (!session->screen_cast_session_proxy)
    return FALSE;

  g_signal_connect (session->remote_desktop_session_proxy,
                    "closed",
                    G_CALLBACK (on_session_closed),
                    session);

  return TRUE;
}

static void
init_in_thread (GTask        *task,
                gpointer      source_object,
                gpointer      task_data,
                GCancellable *cancellable)
{
  MdkSession *session = g_task_get_task_data (task);
  g_autoptr (GError) error = NULL;

  g_debug ("Initializing session");

  g_assert (session->init_task == task);

  session->remote_desktop_proxy =
    mdk_dbus_remote_desktop_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.RemoteDesktop",
      "/org/gnome/Mutter/RemoteDesktop",
      cancellable,
      &error);
  if (!session->remote_desktop_proxy)
    {
      notify_init_failed (session, error);
      return;
    }

  session->screen_cast_proxy =
    mdk_dbus_screen_cast_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
      "org.gnome.Mutter.ScreenCast",
      "/org/gnome/Mutter/ScreenCast",
      cancellable,
      &error);
  if (!session->screen_cast_proxy)
    {
      notify_init_failed (session, error);
      return;
    }

  if (!init_session_in_thread (session, cancellable, &error))
    {
      notify_init_failed (session, error);
      return;
    }

  if (!mdk_dbus_remote_desktop_session_call_start_sync (
        session->remote_desktop_session_proxy,
        cancellable,
        &error))
    {
      notify_init_failed (session, error);
      return;
    }

  g_task_return_boolean (g_steal_pointer (&session->init_task), TRUE);
}

static void
mdk_session_init_async (GAsyncInitable      *initable,
                        int                  io_priority,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  MdkSession *session = MDK_SESSION (initable);
  GTask *task;

  task = g_task_new (initable, cancellable, callback, user_data);
  g_task_set_name (task, G_STRFUNC);
  g_task_set_task_data (task, session, NULL);

  session->init_task = task;
  session->init_cancellable = g_cancellable_new ();

  g_task_run_in_thread (task, init_in_thread);
}

static gboolean
mdk_session_init_finish (GAsyncInitable  *initable,
                         GAsyncResult    *result,
                         GError         **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = mdk_session_init_async;
  iface->init_finish = mdk_session_init_finish;
}

static void
mdk_session_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  MdkSession *session = MDK_SESSION (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      session->context = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_session_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  MdkSession *session = MDK_SESSION (object);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, session->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
mdk_session_finalize (GObject *object)
{
  MdkSession *session = MDK_SESSION (object);

  if (session->remote_desktop_session_proxy)
    {
      mdk_dbus_remote_desktop_session_call_stop_sync (session->remote_desktop_session_proxy,
                                                      NULL, NULL);
      g_clear_object (&session->remote_desktop_session_proxy);
    }
  g_clear_object (&session->screen_cast_session_proxy);

  g_clear_object (&session->screen_cast_proxy);
  g_clear_object (&session->remote_desktop_proxy);

  G_OBJECT_CLASS (mdk_session_parent_class)->finalize (object);
}

static void
mdk_session_class_init (MdkSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = mdk_session_set_property;
  object_class->get_property = mdk_session_get_property;
  object_class->finalize = mdk_session_finalize;

  obj_props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         MDK_TYPE_CONTEXT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST,
                                  0,
                                  NULL, NULL, NULL,
                                  G_TYPE_NONE, 0);
}

static void
mdk_session_init (MdkSession *session)
{
}

static void
record_virtual_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  g_autoptr (GTask) task = G_TASK (user_data);
  MdkSession *session = g_task_get_task_data (task);
  g_autoptr (GError) error = NULL;
  g_autofree char *stream_path = NULL;

  if (!mdk_dbus_screen_cast_session_call_record_virtual_finish (
        session->screen_cast_session_proxy,
        &stream_path,
        res,
        &error))
    {
      g_task_return_new_error (task, error->domain, error->code,
                               "Failed to record virtual monitor: %s",
                               error->message);
      return;
    }

  g_task_return_pointer (task, g_steal_pointer (&stream_path), g_free);
}

void
mdk_session_create_monitor_async (MdkSession          *session,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  MdkDBusScreenCastSession *proxy = session->screen_cast_session_proxy;
  GVariantBuilder properties_builder;
  GTask *task;

  g_debug ("Creating virtual monitor");

  task = g_task_new (session, cancellable, callback, user_data);
  g_task_set_task_data (task, session, NULL);

  g_variant_builder_init (&properties_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&properties_builder, "{sv}",
                         "cursor-mode",
                         g_variant_new_uint32 (MDK_SCREEN_CAST_CURSOR_MODE_HIDDEN));
  g_variant_builder_add (&properties_builder, "{sv}",
                         "is-platform",
                         g_variant_new_boolean (TRUE));

  mdk_dbus_screen_cast_session_call_record_virtual (
    proxy,
    g_variant_builder_end (&properties_builder),
    cancellable,
    record_virtual_cb,
    task);
}

const char *
mdk_session_create_monitor_finish (MdkSession    *session,
                                   GAsyncResult  *res,
                                   GError       **error)
{
  return g_task_propagate_pointer (G_TASK (res), error);
}

MdkContext *
mdk_session_get_context (MdkSession *session)
{
  return session->context;
}

MdkPointer *
mdk_session_create_pointer (MdkSession *session,
                            MdkMonitor *monitor)
{
  return mdk_pointer_new (session,
                          session->remote_desktop_session_proxy,
                          monitor);
}

MdkKeyboard *
mdk_session_create_keyboard (MdkSession *session)
{
  return mdk_keyboard_new (session,
                           session->remote_desktop_session_proxy);
}
