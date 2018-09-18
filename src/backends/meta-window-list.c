/*
 * Copyright (C) 2018 Red Hat Inc.
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

#define _GNU_SOURCE

#include "config.h"

#include "backends/meta-window-list.h"

#include <errno.h>
#include <glib.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "meta-dbus-window-list.h"
#include "backends/meta-backend-private.h"
#include "meta/meta-backend.h"
#include "core/window-private.h"

#define META_WINDOW_LIST_DBUS_SERVICE "org.gnome.Mutter.WindowList"
#define META_WINDOW_LIST_DBUS_PATH "/org/gnome/Mutter/WindowList"
#define META_WINDOW_LIST_API_VERSION 1

struct _MetaWindowList
{
  MetaDBusWindowListSkeleton parent;

  int dbus_name_id;
};

static void
meta_window_list_init_iface (MetaDBusWindowListIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaWindowList,
                         meta_window_list,
                         META_DBUS_TYPE_WINDOW_LIST_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_WINDOW_LIST,
                                                meta_window_list_init_iface));

GDBusConnection *
meta_window_list_get_connection (MetaWindowList *window_list)
{
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (window_list);

  return g_dbus_interface_skeleton_get_connection (interface_skeleton);
}

static gboolean
handle_get_list (MetaDBusWindowList    *skeleton,
                 GDBusMethodInvocation *invocation)
{
  GVariantBuilder res_builder;
  GSList *windows;
  GSList *tmp;
  MetaWindow *focus_window;

  windows = meta_display_list_windows (meta_get_display (), META_LIST_SORTED);
  focus_window = meta_display_get_focus_window (meta_get_display());

  g_variant_builder_init (&res_builder,
                          G_VARIANT_TYPE ("a(ta{sv})"));

  tmp = windows;
  while (tmp != NULL)
    {
      GVariantBuilder properties_builder;
      MetaWindow *window = tmp->data;
      char *class;
      uint64_t client_pid;

      g_variant_builder_init (&properties_builder,
                              G_VARIANT_TYPE ("a{sv}"));

      g_variant_builder_add (&properties_builder, "{sv}",
                             "title",
                             g_variant_new_string (window->title));

      class = window->res_class ? window->res_class : "";
      g_variant_builder_add (&properties_builder, "{sv}",
                             "class",
                             g_variant_new_string (class));

      g_variant_builder_add (&properties_builder, "{sv}",
                             "type",
                             g_variant_new_uint32 (window->client_type));

      g_variant_builder_add (&properties_builder, "{sv}",
                             "is_visible",
                             g_variant_new_boolean (window->visible_to_compositor));

      g_variant_builder_add (&properties_builder, "{sv}",
                             "has_focus",
                             g_variant_new_boolean (window == focus_window));

      g_variant_builder_add (&properties_builder, "{sv}",
                             "width",
                             g_variant_new_int32 (window->rect.width));

      g_variant_builder_add (&properties_builder, "{sv}",
                             "height",
                             g_variant_new_int32 (window->rect.height));

      client_pid = meta_window_get_client_pid (window);
      if (client_pid)
        g_variant_builder_add (&properties_builder, "{sv}",
                               "pid",
                               g_variant_new_uint64 (client_pid));

      g_variant_builder_add (&res_builder, "(ta{sv})",
                             (uint64_t) window->win_id,
                             &properties_builder);

      tmp = tmp->next;
    }

  g_slist_free (windows);

  meta_dbus_window_list_complete_get_list (skeleton,
                                           invocation,
                                           g_variant_builder_end (&res_builder));

  return TRUE;
}

static void
meta_window_list_init_iface (MetaDBusWindowListIface *iface)
{
  iface->handle_get_list = handle_get_list;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaWindowList *window_list = user_data;
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (window_list);
  GError *error = NULL;

  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         META_WINDOW_LIST_DBUS_PATH,
                                         &error))
    g_warning ("Failed to export remote desktop object: %s\n", error->message);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  g_info ("Acquired name %s\n", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_warning ("Lost or failed to acquire name %s\n", name);
}

static void
meta_window_list_constructed (GObject *object)
{
  MetaWindowList *window_list = META_WINDOW_LIST (object);

  window_list->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    META_WINDOW_LIST_DBUS_SERVICE,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    window_list,
                    NULL);
}

static void
meta_window_list_finalize (GObject *object)
{
  MetaWindowList *window_list = META_WINDOW_LIST (object);

  if (window_list->dbus_name_id != 0)
    g_bus_unown_name (window_list->dbus_name_id);

  G_OBJECT_CLASS (meta_window_list_parent_class)->finalize (object);
}

MetaWindowList *
meta_window_list_new (MetaDbusSessionWatcher *session_watcher)
{
  MetaWindowList *window_list;

  window_list = g_object_new (META_TYPE_WINDOW_LIST, NULL);

  return window_list;
}

static void
meta_window_list_init (MetaWindowList *window_list)
{
}

static void
meta_window_list_class_init (MetaWindowListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_window_list_constructed;
  object_class->finalize = meta_window_list_finalize;
}
