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

  struct {
    GSubprocess *subprocess;
  } subprocess;

  char *window_tag;

  pid_t pid;
};

G_DEFINE_TYPE (MetaWaylandClient, meta_wayland_client, G_TYPE_OBJECT)

static void
meta_wayland_client_finalize (GObject *object)
{
  MetaWaylandClient *client = META_WAYLAND_CLIENT (object);

  g_clear_fd (&client->created.client_fd, NULL);
  g_clear_object (&client->subprocess.subprocess);
  g_clear_pointer (&client->window_tag, g_free);

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
  wl_client_get_credentials (wayland_client, &client->pid, NULL, NULL);

  set_wayland_client (client, wayland_client);

  return client;
}

MetaWaylandClient *
meta_wayland_client_new_create (MetaContext  *context,
                                pid_t         pid,
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
  client->pid = pid;

  wayland_client = wl_client_create (compositor->wayland_display, client_fd[0]);
  set_wayland_client (client, wayland_client);

  return client;
}

static void
child_setup (gpointer user_data)
{
  MetaContext *context = META_CONTEXT (user_data);

  meta_context_restore_rlimit_nofile (context, NULL);
}

/**
 * meta_wayland_client_new_subprocess:
 * @context: (not nullable): a #MetaContext
 * @launcher: (not nullable): a GSubprocessLauncher to use to launch the subprocess
 * @argv: (array zero-terminated=1) (element-type filename): Command line arguments
 * @error: (nullable): Error
 *
 * Creates a new #MetaWaylandClient. The #GSubprocesslauncher and array of
 * arguments are used to launch a new process with the binary specified in the
 * first element of argv, and with the rest of elements as parameters.
 * It also sets up a new Wayland socket and sets the environment variable
 * WAYLAND_SOCKET to make the new process to use it.
 *
 * Returns: A #MetaWaylandClient or %NULL if %error is set. Free with
 * g_object_unref().
 */
MetaWaylandClient *
meta_wayland_client_new_subprocess (MetaContext          *context,
                                    GSubprocessLauncher  *launcher,
                                    const char * const   *argv,
                                    GError              **error)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);
  struct wl_client *wayland_client;
  int client_fd[2];
  MetaWaylandClient *client;
  g_autoptr (GSubprocess) subprocess = NULL;

  g_return_val_if_fail (META_IS_CONTEXT (context), NULL);
  g_return_val_if_fail (G_IS_SUBPROCESS_LAUNCHER (launcher), NULL);
  g_return_val_if_fail (argv != NULL &&
                        argv[0] != NULL &&
                        argv[0][0] != '\0',
                        NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail (meta_is_wayland_compositor (), NULL);

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, client_fd) < 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create a socket pair for the wayland client.");
      return NULL;
    }

  g_subprocess_launcher_take_fd (launcher, client_fd[1], 3);
  g_subprocess_launcher_setenv (launcher, "WAYLAND_SOCKET", "3", TRUE);
  g_subprocess_launcher_set_child_setup (launcher, child_setup, context, NULL);

  subprocess = g_subprocess_launcher_spawnv (launcher, argv, error);
  if (!subprocess)
    return NULL;

  client = g_object_new (META_TYPE_WAYLAND_CLIENT, NULL);
  client->context = context;
  client->kind = META_WAYLAND_CLIENT_KIND_SUBPROCESS;
  client->pid =
    g_ascii_strtoll (g_subprocess_get_identifier (subprocess), NULL, 0);
  client->subprocess.subprocess = g_steal_pointer (&subprocess);

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

/**
 * meta_wayland_client_get_subprocess:
 * @client: a #MetaWaylandClient
 *
 * Get the #GSubprocess which was created by meta_wayland_client_new_subprocess.
 *
 * Returns: (transfer none): The #GSubprocess
 **/
GSubprocess *
meta_wayland_client_get_subprocess (MetaWaylandClient *client)
{
  g_return_val_if_fail (client->kind == META_WAYLAND_CLIENT_KIND_SUBPROCESS,
                        NULL);

  return client->subprocess.subprocess;
}

/**
 * meta_wayland_client_owns_window
 * @client: a #MetaWaylandClient
 * @window: (not nullable): a MetaWindow
 *
 * Checks whether @window belongs to the process launched from @client or not.
 * This only works under Wayland. If the window is an X11 window, an exception
 * will be triggered.
 *
 * Returns: TRUE if the window was created by this process; FALSE if not.
 */
gboolean
meta_wayland_client_owns_window (MetaWaylandClient *client,
                                 MetaWindow        *window)
{
  MetaWindowWayland *wl_window;
  MetaWaylandClient *window_client;

  g_return_val_if_fail (meta_is_wayland_compositor (), FALSE);

  if (!META_IS_WINDOW_WAYLAND (window))
    return FALSE;

  wl_window = META_WINDOW_WAYLAND (window);
  window_client = meta_window_wayland_get_client (wl_window);

  return client == window_client;
}

MetaWaylandClient *
meta_get_wayland_client (const struct wl_client *wl_client)
{
  return wl_client_get_user_data ((struct wl_client *) wl_client);
}

void
meta_wayland_client_set_window_tag (MetaWaylandClient *client,
                                    const char *window_tag)
{
  g_set_str (&client->window_tag, window_tag);
}

const char *
meta_wayland_client_get_window_tag (MetaWaylandClient *client)
{
  return client->window_tag;
}

pid_t
meta_wayland_client_get_pid (MetaWaylandClient *client)
{
  return client->pid;
}
