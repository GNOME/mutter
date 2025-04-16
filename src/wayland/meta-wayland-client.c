/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2019 Sergio Costas (rastersoft@gmail.com)
 * Copyright 2023 Red Hat
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
 */

/**
 * MetaWaylandClient:
 * 
 * A class that allows to launch a trusted client and detect if an specific
 * Wayland window belongs to it.
 */

#include "config.h"

#include "wayland/meta-wayland-client-private.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <wayland-server.h>

#include "core/window-private.h"
#include "meta/util.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-types.h"
#include "wayland/meta-window-wayland.h"

enum
{
  CLIENT_DESTROYED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _MetaWaylandClient
{
  GObject parent_instance;

  MetaContext *context;

  struct {
    int fd;
  } indirect;

  struct wl_client *wayland_client;
  struct wl_listener client_destroy_listener;
  MetaServiceClientType service_client_type;
};

G_DEFINE_TYPE (MetaWaylandClient, meta_wayland_client, G_TYPE_OBJECT)

static void
meta_wayland_client_dispose (GObject *object)
{
  MetaWaylandClient *client = META_WAYLAND_CLIENT (object);

  g_clear_pointer (&client->wayland_client, wl_client_destroy);

  G_OBJECT_CLASS (meta_wayland_client_parent_class)->dispose (object);
}

static void
meta_wayland_client_class_init (MetaWaylandClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_wayland_client_dispose;

  signals[CLIENT_DESTROYED] = g_signal_new ("client-destroyed",
                                            G_TYPE_FROM_CLASS (klass),
                                            G_SIGNAL_RUN_LAST,
                                            0, NULL, NULL,
                                            NULL,
                                            G_TYPE_NONE, 0);
}

static void
meta_wayland_client_init (MetaWaylandClient *client)
{
  client->service_client_type = META_SERVICE_CLIENT_TYPE_NONE;
}

/**
 * meta_wayland_client_new_indirect: (skip)
 */
MetaWaylandClient *
meta_wayland_client_new_indirect (MetaContext  *context,
                                  GError      **error)
{
  MetaWaylandClient *client;

  if (!meta_is_wayland_compositor ())
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "MetaWaylandClient can be used only with Wayland.");
      return NULL;
    }

  client = g_object_new (META_TYPE_WAYLAND_CLIENT, NULL);
  client->context = context;

  return client;
}

static gboolean
init_wayland_client (MetaWaylandClient  *client,
                     struct wl_client  **wayland_client,
                     int                *fd,
                     GError            **error)
{
  MetaWaylandCompositor *compositor;
  int client_fd[2];

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, client_fd) < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create a socket pair for the wayland client.");
      return FALSE;
    }

  compositor = meta_context_get_wayland_compositor (client->context);

  *wayland_client = wl_client_create (compositor->wayland_display, client_fd[0]);
  *fd = client_fd[1];

  return TRUE;
}

static void
client_destroyed_cb (struct wl_listener *listener,
                     void               *user_data)
{
  MetaWaylandClient *client = wl_container_of (listener, client,
                                               client_destroy_listener);

  client->wayland_client = NULL;
  g_signal_emit (client, signals[CLIENT_DESTROYED], 0);
}

static void
set_wayland_client (MetaWaylandClient *client,
                    struct wl_client  *wayland_client)
{
  client->wayland_client = wayland_client;

  client->client_destroy_listener.notify = client_destroyed_cb;
  wl_client_add_destroy_listener (wayland_client,
                                  &client->client_destroy_listener);
}

/**
 * meta_wayland_client_setup_fd: (skip)
 * @client: a #MetaWaylandClient
 *
 * Initialize a wl_client that can be connected to via the returned file
 * descriptor. May only be used with a #MetaWaylandClient created with
 * meta_wayland_client_new_indirect().
 *
 * Returns: (transfer full): A new file descriptor
 */
int
meta_wayland_client_setup_fd (MetaWaylandClient  *client,
                              GError            **error)
{
  struct wl_client *wayland_client;
  int fd;

  g_return_val_if_fail (!client->wayland_client, -1);

  if (!init_wayland_client (client, &wayland_client, &fd, error))
    return -1;

  set_wayland_client (client, wayland_client);

  return fd;
}

gboolean
meta_wayland_client_matches (MetaWaylandClient      *client,
                             const struct wl_client *wayland_client)
{
  g_return_val_if_fail (wayland_client, FALSE);
  g_return_val_if_fail (client->wayland_client, FALSE);

  return client->wayland_client == wayland_client;
}

void
meta_wayland_client_assign_service_client_type (MetaWaylandClient     *client,
                                                MetaServiceClientType  service_client_type)
{
  g_return_if_fail (client->service_client_type ==
                    META_SERVICE_CLIENT_TYPE_NONE);
  client->service_client_type = service_client_type;
}

MetaServiceClientType
meta_wayland_client_get_service_client_type (MetaWaylandClient *client)
{
  return client->service_client_type;
}
