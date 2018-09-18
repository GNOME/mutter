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

#include "config.h"

#include "core/meta-introspect.h"

#include <errno.h>
#include <glib.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "core/window-private.h"

#include "meta-dbus-introspect.h"

#define META_INTROSPECT_DBUS_SERVICE "org.gnome.Mutter.Introspect"
#define META_INTROSPECT_DBUS_PATH "/org/gnome/Mutter/Introspect"
#define META_INTROSPECT_API_VERSION 1

struct _MetaIntrospect
{
  MetaDBusIntrospectSkeleton parent;

  int dbus_name_id;
};

static void
meta_introspect_init_iface (MetaDBusIntrospectIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaIntrospect,
                         meta_introspect,
                         META_DBUS_TYPE_INTROSPECT_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_INTROSPECT,
                                                meta_introspect_init_iface));

GDBusConnection *
meta_introspect_get_connection (MetaIntrospect *introspect)
{
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (introspect);

  return g_dbus_interface_skeleton_get_connection (interface_skeleton);
}

static gboolean
handle_get_windows (MetaDBusIntrospect    *skeleton,
                    GDBusMethodInvocation *invocation)
{
  GVariantBuilder windows_builder;
  MetaWindow *focus_window;
  GSList *windows;
  GSList *l;

  windows = meta_display_list_windows (meta_get_display (), META_LIST_SORTED);
  focus_window = meta_display_get_focus_window (meta_get_display());

  g_variant_builder_init (&windows_builder,
                          G_VARIANT_TYPE ("a(ta{sv})"));

  for (l = windows; l; l = l->next)
    {
      GVariantBuilder properties_builder;
      MetaWindow *window = l->data;
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
                             "is-visible",
                             g_variant_new_boolean (window->visible_to_compositor));

      g_variant_builder_add (&properties_builder, "{sv}",
                             "has-focus",
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

      g_variant_builder_add (&windows_builder, "(ta{sv})",
                             (uint64_t) window->id,
                             &properties_builder);
    }

  g_slist_free (windows);

  meta_dbus_introspect_complete_get_windows (skeleton,
                                             invocation,
                                             g_variant_builder_end (&windows_builder));

  return TRUE;
}

static void
meta_introspect_init_iface (MetaDBusIntrospectIface *iface)
{
  iface->handle_get_windows = handle_get_windows;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaIntrospect *introspect = user_data;
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (introspect);
  GError *error = NULL;

  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         META_INTROSPECT_DBUS_PATH,
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
  g_info ("Lost or failed to acquire name %s\n", name);
}

static void
meta_introspect_constructed (GObject *object)
{
  MetaIntrospect *introspect = META_INTROSPECT (object);

  introspect->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    META_INTROSPECT_DBUS_SERVICE,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    introspect,
                    NULL);

  G_OBJECT_CLASS (meta_introspect_parent_class)->constructed (object);
}

static void
meta_introspect_finalize (GObject *object)
{
  MetaIntrospect *introspect = META_INTROSPECT (object);

  if (introspect->dbus_name_id != 0)
    g_bus_unown_name (introspect->dbus_name_id);

  G_OBJECT_CLASS (meta_introspect_parent_class)->finalize (object);
}

MetaIntrospect *
meta_introspect_new (void)
{
  MetaIntrospect *introspect;

  introspect = g_object_new (META_TYPE_INTROSPECT, NULL);

  return introspect;
}

static void
meta_introspect_init (MetaIntrospect *introspect)
{
}

static void
meta_introspect_class_init (MetaIntrospectClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_introspect_constructed;
  object_class->finalize = meta_introspect_finalize;
}
