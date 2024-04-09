/*
 * Copyright (C) 2023 Red Hat Inc.
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

#include "core/meta-service-channel.h"

#include "wayland/meta-wayland-client-private.h"

#define META_SERVICE_CHANNEL_DBUS_SERVICE "org.gnome.Mutter.ServiceChannel"
#define META_SERVICE_CHANNEL_DBUS_PATH "/org/gnome/Mutter/ServiceChannel"

struct _MetaServiceChannel
{
  MetaDBusServiceChannelSkeleton parent;

  guint dbus_name_id;

  MetaContext *context;

  GHashTable *service_clients;
};

typedef struct _MetaServiceClient
{
  MetaWaylandClient *wayland_client;
  gulong destroyed_handler_id;
  MetaServiceChannel *service_channel;
} MetaServiceClient;

static void meta_service_channel_init_iface (MetaDBusServiceChannelIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaServiceChannel, meta_service_channel,
                         META_DBUS_TYPE_SERVICE_CHANNEL_SKELETON,
                         G_IMPLEMENT_INTERFACE (META_DBUS_TYPE_SERVICE_CHANNEL,
                                                meta_service_channel_init_iface))

static void
meta_service_client_free (MetaServiceClient *service_client)
{
  g_signal_handler_disconnect (service_client->wayland_client,
                               service_client->destroyed_handler_id);
  g_object_unref (service_client->wayland_client);
  g_free (service_client);
}

static void
on_service_client_destroyed (MetaWaylandClient *wayland_client,
                             MetaServiceClient *service_client)
{
  MetaServiceClientType service_client_type;

  service_client_type =
    meta_wayland_client_get_service_client_type (wayland_client);
  g_return_if_fail (service_client_type != META_SERVICE_CLIENT_TYPE_NONE);

  g_hash_table_remove (service_client->service_channel->service_clients,
                       GINT_TO_POINTER (service_client_type));
}

static MetaServiceClient *
meta_service_client_new (MetaServiceChannel *service_channel,
                         MetaWaylandClient  *wayland_client)
{
  MetaServiceClient *service_client;

  service_client = g_new0 (MetaServiceClient, 1);
  service_client->service_channel = service_channel;
  service_client->wayland_client = g_object_ref (wayland_client);
  service_client->destroyed_handler_id =
    g_signal_connect (wayland_client, "client-destroyed",
                      G_CALLBACK (on_service_client_destroyed),
                      service_client);

  return service_client;
}

static gboolean
verify_service_client_type (uint32_t service_client_type)
{
  switch ((MetaServiceClientType) service_client_type)
    {
    case META_SERVICE_CLIENT_TYPE_NONE:
      return FALSE;
    case META_SERVICE_CLIENT_TYPE_PORTAL_BACKEND:
    case META_SERVICE_CLIENT_TYPE_FILECHOOSER_PORTAL_BACKEND:
      return TRUE;
    }

  return FALSE;
}

static gboolean
handle_open_wayland_service_connection (MetaDBusServiceChannel *object,
                                        GDBusMethodInvocation  *invocation,
                                        GUnixFDList            *in_fd_list,
                                        uint32_t                service_client_type)
{
#ifdef HAVE_WAYLAND
  MetaServiceChannel *service_channel = META_SERVICE_CHANNEL (object);
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaWaylandClient) wayland_client = NULL;
  g_autoptr (GUnixFDList) out_fd_list = NULL;
  int fd;
  int fd_id;

  if (meta_context_get_compositor_type (service_channel->context) !=
      META_COMPOSITOR_TYPE_WAYLAND)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_NOT_SUPPORTED,
                                             "Not a Wayland compositor");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if (!verify_service_client_type (service_client_type))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "Invalid service client type");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  wayland_client = meta_wayland_client_new_indirect (service_channel->context,
                                                     &error);
  if (!wayland_client)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_NOT_SUPPORTED,
                                             "Failed to create Wayland client: %s",
                                             error->message);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  meta_wayland_client_assign_service_client_type (wayland_client,
                                                  service_client_type);

  fd = meta_wayland_client_setup_fd (wayland_client, &error);
  if (fd < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_NOT_SUPPORTED,
                                             "Failed to setup Wayland client socket: %s",
                                             error->message);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  out_fd_list = g_unix_fd_list_new ();
  fd_id = g_unix_fd_list_append (out_fd_list, fd, &error);
  close (fd);

  if (fd_id == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             G_DBUS_ERROR,
                                             G_DBUS_ERROR_ACCESS_DENIED,
                                             "Failed to append fd: %s",
                                             error->message);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  g_hash_table_replace (service_channel->service_clients,
                        GUINT_TO_POINTER (service_client_type),
                        meta_service_client_new (service_channel,
                                                 wayland_client));

  meta_dbus_service_channel_complete_open_wayland_service_connection (
    object, invocation, out_fd_list, g_variant_new_handle (fd_id));
  return G_DBUS_METHOD_INVOCATION_HANDLED;
#else /* HAVE_WAYLAND */
  g_dbus_method_invocation_return_error (invocation,
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_NOT_SUPPORTED,
                                         "Wayland not supported",
                                         error->message);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
#endif /* HAVE_WAYLAND */
}

static void
meta_service_channel_init_iface (MetaDBusServiceChannelIface *iface)
{
  iface->handle_open_wayland_service_connection =
    handle_open_wayland_service_connection;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaServiceChannel *service_channel = user_data;
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (service_channel);
  g_autoptr (GError) error = NULL;

  if (!g_dbus_interface_skeleton_export (interface_skeleton,
                                         connection,
                                         META_SERVICE_CHANNEL_DBUS_PATH,
                                         &error))
    g_warning ("Failed to export service channel object: %s", error->message);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  g_info ("Acquired name %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_warning ("Lost or failed to acquire name %s", name);
}

static void
meta_service_channel_constructed (GObject *object)
{
  MetaServiceChannel *service_channel = META_SERVICE_CHANNEL (object);

  service_channel->service_clients =
    g_hash_table_new_full (NULL, NULL,
                           NULL, (GDestroyNotify) meta_service_client_free);

  service_channel->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    META_SERVICE_CHANNEL_DBUS_SERVICE,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    service_channel,
                    NULL);
}

static void
meta_service_channel_finalize (GObject *object)
{
  MetaServiceChannel *service_channel = META_SERVICE_CHANNEL (object);

  g_clear_pointer (&service_channel->service_clients, g_hash_table_unref);
  g_clear_handle_id (&service_channel->dbus_name_id, g_bus_unown_name);

  G_OBJECT_CLASS (meta_service_channel_parent_class)->finalize (object);
}

static void
meta_service_channel_class_init (MetaServiceChannelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = meta_service_channel_constructed;
  object_class->finalize = meta_service_channel_finalize;
}

static void
meta_service_channel_init (MetaServiceChannel *service_channel)
{
}

MetaServiceChannel *
meta_service_channel_new (MetaContext *context)
{
  MetaServiceChannel *service_channel;

  service_channel = g_object_new (META_TYPE_SERVICE_CHANNEL, NULL);
  service_channel->context = context;

  return service_channel;
}

MetaWaylandClient *
meta_service_channel_get_service_client (MetaServiceChannel    *service_channel,
                                         MetaServiceClientType  service_client_type)
{
  MetaServiceClient *service_client;

  service_client = g_hash_table_lookup (service_channel->service_clients,
                                        GINT_TO_POINTER (service_client_type));
  if (!service_client)
    return NULL;

  return service_client->wayland_client;
}
