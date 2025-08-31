/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2013-2021 Red Hat, Inc.
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
 * Adapted from gnome-session/gnome-session/gs-idle-monitor.c and
 *         from gnome-desktop/libgnome-desktop/gnome-idle-monitor.c
 */

#include "config.h"

#include "backends/meta-idle-manager.h"

#include "backends/meta-idle-monitor-private.h"
#include "clutter/clutter-mutter.h"
#include "clutter/clutter.h"
#include "meta/main.h"
#include "meta/meta-context.h"
#include "meta/meta-idle-monitor.h"
#include "meta/util.h"

#include "meta-dbus-idle-monitor.h"

typedef struct _MetaIdleManager
{
  MetaBackend *backend;
  MetaIdleMonitor *core_monitor;
  guint dbus_name_id;
} MetaIdleManager;

static gboolean
handle_get_idletime (MetaDBusIdleMonitor   *skeleton,
                     GDBusMethodInvocation *invocation,
                     MetaIdleMonitor       *monitor)
{
  guint64 idletime;

  idletime = meta_idle_monitor_get_idletime (monitor);
  meta_dbus_idle_monitor_complete_get_idletime (skeleton, invocation, idletime);

  return TRUE;
}

static gboolean
handle_reset_idletime (MetaDBusIdleMonitor   *skeleton,
                       GDBusMethodInvocation *invocation,
                       MetaIdleMonitor       *monitor)
{
  if (!g_getenv ("MUTTER_DEBUG_RESET_IDLETIME"))
    {
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     G_DBUS_ERROR,
                                                     G_DBUS_ERROR_UNKNOWN_METHOD,
                                                     "This method is for testing purposes only. MUTTER_DEBUG_RESET_IDLETIME must be set to use it");
      return TRUE;
    }

  meta_idle_manager_reset_idle_time (meta_idle_monitor_get_manager (monitor));
  meta_dbus_idle_monitor_complete_reset_idletime (skeleton, invocation);

  return TRUE;
}

typedef struct {
  MetaDBusIdleMonitor *dbus_monitor;
  MetaIdleMonitor *monitor;
  char *dbus_name;
  guint watch_id;
  guint name_watcher_id;
} DBusWatch;

static void
destroy_dbus_watch (gpointer data)
{
  DBusWatch *watch = data;

  g_object_unref (watch->dbus_monitor);
  g_object_unref (watch->monitor);
  g_free (watch->dbus_name);
  g_bus_unwatch_name (watch->name_watcher_id);

  g_free (watch);
}

static void
dbus_idle_callback (MetaIdleMonitor *monitor,
                    guint            watch_id,
                    gpointer         user_data)
{
  DBusWatch *watch = user_data;
  GDBusInterfaceSkeleton *skeleton = G_DBUS_INTERFACE_SKELETON (watch->dbus_monitor);

  g_dbus_connection_emit_signal (g_dbus_interface_skeleton_get_connection (skeleton),
                                 watch->dbus_name,
                                 g_dbus_interface_skeleton_get_object_path (skeleton),
                                 "org.gnome.Mutter.IdleMonitor",
                                 "WatchFired",
                                 g_variant_new ("(u)", watch_id),
                                 NULL);
}

static void
name_vanished_callback (GDBusConnection *connection,
                        const char      *name,
                        gpointer         user_data)
{
  DBusWatch *watch = user_data;

  meta_idle_monitor_remove_watch (watch->monitor, watch->watch_id);
}

static DBusWatch *
make_dbus_watch (MetaDBusIdleMonitor   *skeleton,
                 GDBusMethodInvocation *invocation,
                 MetaIdleMonitor       *monitor)
{
  DBusWatch *watch;

  watch = g_new0 (DBusWatch, 1);
  watch->dbus_monitor = g_object_ref (skeleton);
  watch->monitor = g_object_ref (monitor);
  watch->dbus_name = g_strdup (g_dbus_method_invocation_get_sender (invocation));
  watch->name_watcher_id = g_bus_watch_name_on_connection (g_dbus_method_invocation_get_connection (invocation),
                                                           watch->dbus_name,
                                                           G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                           NULL, /* appeared */
                                                           name_vanished_callback,
                                                           watch, NULL);

  return watch;
}

static gboolean
handle_add_idle_watch (MetaDBusIdleMonitor   *skeleton,
                       GDBusMethodInvocation *invocation,
                       guint64                interval,
                       MetaIdleMonitor       *monitor)
{
  DBusWatch *watch;

  watch = make_dbus_watch (skeleton, invocation, monitor);
  watch->watch_id = meta_idle_monitor_add_idle_watch (monitor, interval,
                                                      dbus_idle_callback, watch, destroy_dbus_watch);

  meta_dbus_idle_monitor_complete_add_idle_watch (skeleton, invocation, watch->watch_id);

  return TRUE;
}

static gboolean
handle_add_user_active_watch (MetaDBusIdleMonitor   *skeleton,
                              GDBusMethodInvocation *invocation,
                              MetaIdleMonitor       *monitor)
{
  DBusWatch *watch;

  watch = make_dbus_watch (skeleton, invocation, monitor);
  watch->watch_id = meta_idle_monitor_add_user_active_watch (monitor,
                                                             dbus_idle_callback, watch,
                                                             destroy_dbus_watch);

  meta_dbus_idle_monitor_complete_add_user_active_watch (skeleton, invocation, watch->watch_id);

  return TRUE;
}

static gboolean
handle_remove_watch (MetaDBusIdleMonitor   *skeleton,
                     GDBusMethodInvocation *invocation,
                     guint                  id,
                     MetaIdleMonitor       *monitor)
{
  meta_idle_monitor_remove_watch (monitor, id);
  meta_dbus_idle_monitor_complete_remove_watch (skeleton, invocation);

  return TRUE;
}

static void
create_monitor_skeleton (GDBusObjectManagerServer *manager,
                         MetaIdleMonitor          *monitor,
                         const char               *path)
{
  MetaDBusIdleMonitor *skeleton;
  MetaDBusObjectSkeleton *object;

  skeleton = meta_dbus_idle_monitor_skeleton_new ();
  g_signal_connect (skeleton, "handle-add-idle-watch",
                    G_CALLBACK (handle_add_idle_watch), monitor);
  g_signal_connect (skeleton, "handle-add-user-active-watch",
                    G_CALLBACK (handle_add_user_active_watch), monitor);
  g_signal_connect (skeleton, "handle-remove-watch",
                    G_CALLBACK (handle_remove_watch), monitor);
  g_signal_connect (skeleton, "handle-reset-idletime",
                    G_CALLBACK (handle_reset_idletime), monitor);
  g_signal_connect (skeleton, "handle-get-idletime",
                    G_CALLBACK (handle_get_idletime), monitor);

  object = meta_dbus_object_skeleton_new (path);
  meta_dbus_object_skeleton_set_idle_monitor (object, skeleton);

  g_dbus_object_manager_server_export (manager, G_DBUS_OBJECT_SKELETON (object));

  g_object_unref (skeleton);
  g_object_unref (object);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaIdleManager *manager = user_data;
  GDBusObjectManagerServer *object_manager;
  MetaIdleMonitor *monitor;

  object_manager = g_dbus_object_manager_server_new ("/org/gnome/Mutter/IdleMonitor");

  /* We never clear the core monitor, as that's supposed to cumulate idle times from
     all devices */
  monitor = meta_idle_manager_get_core_monitor (manager);
  create_monitor_skeleton (object_manager, monitor,
                           "/org/gnome/Mutter/IdleMonitor/Core");

  g_dbus_object_manager_server_set_connection (object_manager, connection);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  meta_topic (META_DEBUG_DBUS, "Acquired name %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  meta_topic (META_DEBUG_DBUS, "Lost or failed to acquire name %s", name);
}

MetaIdleMonitor *
meta_idle_manager_get_core_monitor (MetaIdleManager *idle_manager)
{
  if (!idle_manager->core_monitor)
    idle_manager->core_monitor = meta_idle_monitor_new (idle_manager);

  return idle_manager->core_monitor;
}

void
meta_idle_manager_reset_idle_time (MetaIdleManager *idle_manager)
{
  MetaIdleMonitor *core_monitor;

  core_monitor = meta_idle_manager_get_core_monitor (idle_manager);
  meta_idle_monitor_reset_idletime (core_monitor);
}

MetaIdleManager *
meta_idle_manager_new (MetaBackend *backend)
{
  MetaIdleManager *idle_manager;

  idle_manager = g_new0 (MetaIdleManager, 1);
  idle_manager->backend = backend;

  idle_manager->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    "org.gnome.Mutter.IdleMonitor",
                    G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                     G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    idle_manager,
                    NULL);

  return idle_manager;
}

void
meta_idle_manager_free (MetaIdleManager *idle_manager)
{
  g_bus_unown_name (idle_manager->dbus_name_id);
  g_free (idle_manager);
}
