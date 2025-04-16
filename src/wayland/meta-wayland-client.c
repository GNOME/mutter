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
#include <glib/gstdio.h>

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

  struct wl_client *wayland_client;
  struct wl_listener client_destroy_listener;

  MetaWaylandClientCaps caps;

  MetaWaylandClientKind kind;

  struct {
    int client_fd;
  } created;
};

G_DEFINE_TYPE (MetaWaylandClient, meta_wayland_client, G_TYPE_OBJECT)

static void
meta_wayland_client_finalize (GObject *object)
{
  MetaWaylandClient *client = META_WAYLAND_CLIENT (object);

  g_clear_fd (&client->created.client_fd, NULL);

  G_OBJECT_CLASS (meta_wayland_client_parent_class)->finalize (object);
}

static void
meta_wayland_client_class_init (MetaWaylandClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_wayland_client_finalize;

  signals[CLIENT_DESTROYED] =
    g_signal_new ("client-destroyed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_wayland_client_init (MetaWaylandClient *client)
{
  client->created.client_fd = -1;
}

static void
on_client_destroyed (struct wl_listener *listener,
                     void               *user_data)
{
  MetaWaylandClient *client = wl_container_of (listener,
                                               client,
                                               client_destroy_listener);

  client->wayland_client = NULL;
  g_signal_emit (client, signals[CLIENT_DESTROYED], 0);
}

static void
set_wayland_client (MetaWaylandClient *client,
                    struct wl_client  *wayland_client)
{
  client->wayland_client = wayland_client;

  client->client_destroy_listener.notify = on_client_destroyed;
  wl_client_add_destroy_listener (wayland_client,
                                  &client->client_destroy_listener);

  wl_client_set_user_data (wayland_client,
                           g_object_ref (client),
                           g_object_unref);
}

MetaWaylandClient *
meta_wayland_client_new_from_wl (MetaContext      *context,
                                 struct wl_client *wayland_client)
{
  MetaWaylandClient *client;

  client = g_object_new (META_TYPE_WAYLAND_CLIENT, NULL);
  client->context = context;
  client->kind = META_WAYLAND_CLIENT_KIND_PUBLIC;

  set_wayland_client (client, wayland_client);

  return client;
}

MetaWaylandClient *
meta_wayland_client_new_create (MetaContext  *context,
                                GError      **error)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);
  struct wl_client *wayland_client;
  int client_fd[2];
  MetaWaylandClient *client;

  g_return_val_if_fail (meta_is_wayland_compositor (), NULL);

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, client_fd) < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create a socket pair for the wayland client.");
      return FALSE;
    }

  client = g_object_new (META_TYPE_WAYLAND_CLIENT, NULL);
  client->context = context;
  client->kind = META_WAYLAND_CLIENT_KIND_CREATED;
  client->created.client_fd = client_fd[1];

  wayland_client = wl_client_create (compositor->wayland_display, client_fd[0]);
  set_wayland_client (client, wayland_client);

  return client;
}

void
meta_wayland_client_destroy (MetaWaylandClient *client)
{
  g_clear_pointer (&client->wayland_client, wl_client_destroy);
}


MetaContext *
meta_wayland_client_get_context (MetaWaylandClient *client)
{
  return client->context;
}

struct wl_client *
meta_wayland_client_get_wl_client (MetaWaylandClient *client)
{
  return client->wayland_client;
}

gboolean
meta_wayland_client_matches (MetaWaylandClient      *client,
                             const struct wl_client *wl_client)
{
  return meta_wayland_client_get_wl_client (client) == wl_client;
}

MetaWaylandClientKind
meta_wayland_client_get_kind (MetaWaylandClient *client)
{
  return client->kind;
}

void
meta_wayland_client_set_caps (MetaWaylandClient     *client,
                              MetaWaylandClientCaps  caps)
{
  client->caps = caps;
}

MetaWaylandClientCaps
meta_wayland_client_get_caps (MetaWaylandClient *client)
{
  return client->caps;
}

gboolean
meta_wayland_client_has_caps (MetaWaylandClient     *client,
                              MetaWaylandClientCaps  caps)
{
  return (client->caps & caps) == caps;
}

int
meta_wayland_client_take_client_fd (MetaWaylandClient *client)
{
  g_return_val_if_fail (client->kind == META_WAYLAND_CLIENT_KIND_CREATED, -1);

  return g_steal_fd (&client->created.client_fd);
}

MetaWaylandClient *
meta_get_wayland_client (const struct wl_client *wl_client)
{
  return wl_client_get_user_data ((struct wl_client *) wl_client);
}
