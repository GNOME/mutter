/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright 2019 Sergio Costas (rastersoft@gmail.com)
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
 */

/**
 * SECTION: meta-wayland-client
 * @title MetaWaylandClient
 * @include: gio/gsubprocess.h
 * A class that allows to launch a trusted client and detect if an specific
 * Wayland window belongs to it.
 */

#include "config.h"

#include "meta/meta-wayland-client.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <wayland-server.h>

#include "core/window-private.h"
#include "meta/util.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-types.h"

struct _MetaWaylandClient
{
  GObject parent_instance;

  GSubprocessLauncher *launcher;
  GSubprocess *subprocess;
  GCancellable *died_cancellable;
  gboolean process_running;
  struct wl_client *wayland_client;
};

G_DEFINE_TYPE (MetaWaylandClient, meta_wayland_client, G_TYPE_OBJECT)

static void
meta_wayland_client_dispose (GObject *object)
{
  MetaWaylandClient *client = META_WAYLAND_CLIENT (object);

  g_cancellable_cancel (client->died_cancellable);
  g_clear_object (&client->died_cancellable);
  g_clear_object (&client->launcher);
  g_clear_object (&client->subprocess);

  G_OBJECT_CLASS (meta_wayland_client_parent_class)->dispose (object);
}

static void
meta_wayland_client_class_init (MetaWaylandClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_wayland_client_dispose;
}

static void
meta_wayland_client_init (MetaWaylandClient *client)
{
}

static void
process_died (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  MetaWaylandClient *client = META_WAYLAND_CLIENT (user_data);

  client->process_running = FALSE;
}

/**
 * meta_wayland_client_new:
 * @launcher: (not nullable): a GSubprocessLauncher to use to launch the subprocess
 * @error: (nullable): Error
 *
 * Creates a new #MetaWaylandClient. The GSubprocesslauncher passed is
 * stored internally and will be used to launch the subprocess.
 *
 * Returns: A #MetaWaylandClient or %NULL if %error is set. Free with
 * g_object_unref().
 */
MetaWaylandClient *
meta_wayland_client_new (GSubprocessLauncher  *launcher,
                         GError              **error)
{
  MetaWaylandClient *client;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!meta_is_wayland_compositor ())
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "MetaWaylandClient can be used only with Wayland.");
      return NULL;
    }

  if (launcher == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Invalid launcher.");
      return NULL;
    }

  client = g_object_new (META_TYPE_WAYLAND_CLIENT, NULL);
  client->launcher = launcher;
  return client;
}

/**
 * meta_wayland_client_spawnv:
 * @client: a #MetaWaylandClient
 * @display: (not nullable): the current MetaDisplay
 * @argv: (array zero-terminated=1) (element-type filename): Command line arguments
 * @error: (nullable): Error
 *
 * Creates a #GSubprocess given a provided array of arguments, launching a new
 * process with the binary specified in the first element of argv, and with the
 * rest of elements as parameters. It also sets up a new Wayland socket and sets
 * the environment variable WAYLAND_SOCKET to make the new process to use it.
 *
 * Returns: (transfer full): A new #GSubprocess, or %NULL on error (and @error
 * will be set)
 **/
GSubprocess *
meta_wayland_client_spawnv (MetaWaylandClient   *client,
                            MetaDisplay         *display,
                            const char * const  *argv,
                            GError             **error)
{
  int client_fd[2];
  GSubprocess *subprocess;
  struct wl_client *wayland_client;
  MetaWaylandCompositor *compositor;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail (argv != NULL &&
                        argv[0] != NULL &&
                        argv[0][0] != '\0',
                        NULL);

  if (client->launcher == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_INITIALIZED,
                   "MetaWaylandClient must be created using meta_wayland_client_new().");
      return NULL;
    }

  if (client->subprocess != NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "This object already has a process running.");
      return NULL;
    }

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, client_fd) < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to create a socket pair for the wayland client.");
      return NULL;
    }

  compositor = meta_wayland_compositor_get_default ();
  g_subprocess_launcher_take_fd (client->launcher, client_fd[1], 3);
  g_subprocess_launcher_setenv (client->launcher, "WAYLAND_SOCKET", "3", TRUE);
  wayland_client = wl_client_create (compositor->wayland_display, client_fd[0]);
  subprocess = g_subprocess_launcher_spawnv (client->launcher, argv, error);

  if (subprocess == NULL)
    return NULL;

  client->subprocess = subprocess;
  client->wayland_client = wayland_client;
  client->process_running = TRUE;
  client->died_cancellable = g_cancellable_new ();
  g_subprocess_wait_async (client->subprocess,
                           client->died_cancellable,
                           process_died,
                           client);

  return g_object_ref (client->subprocess);
}

/**
 * meta_wayland_client_spawn:
 * @client: a #MetaWaylandClient
 * @display: (not nullable): the current MetaDisplay
 * @error: (nullable): Error
 * @argv0: Command line arguments
 * @...: Continued arguments, %NULL terminated
 *
 * Creates a #GSubprocess given a provided varargs list of arguments. It also
 * sets up a new Wayland socket and sets the environment variable WAYLAND_SOCKET
 * to make the new process to use it.
 *
 * Returns: (transfer full): A new #GSubprocess, or %NULL on error (and @error
 * will be set)
 **/
GSubprocess *
meta_wayland_client_spawn (MetaWaylandClient  *client,
                           MetaDisplay        *display,
                           GError            **error,
                           const char         *argv0,
                           ...)
{
  g_autoptr (GPtrArray) args = NULL;
  GSubprocess *result;
  const char *arg;
  va_list ap;

  g_return_val_if_fail (argv0 != NULL && argv0[0] != '\0', NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  args = g_ptr_array_new_with_free_func (g_free);

  va_start (ap, argv0);
  g_ptr_array_add (args, (char *) argv0);
  while ((arg = va_arg (ap, const char *)))
    g_ptr_array_add (args, (char *) arg);

  g_ptr_array_add (args, NULL);
  va_end (ap);

  result = meta_wayland_client_spawnv (client,
                                       display,
                                       (const char * const *) args->pdata,
                                       error);

  return result;
}

/**
 * meta_wayland_client_owns_wayland_window
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
  MetaWaylandSurface *surface;

  g_return_val_if_fail (meta_is_wayland_compositor (), FALSE);
  g_return_val_if_fail (client->subprocess != NULL, FALSE);
  g_return_val_if_fail (client->process_running, FALSE);

  surface = window->surface;
  if (surface == NULL)
    return FALSE;

  return wl_resource_get_client (surface->resource) == client->wayland_client;
}

/**
 * meta_wayland_client_skip_from_window_list
 * @client: a #MetaWaylandClient
 * @window: (not nullable): a MetaWindow
 *
 * Hides this window from any window list, like taskbars, pagers...
 */
void
meta_wayland_client_hide_from_window_list (MetaWaylandClient *client,
                                           MetaWindow        *window)
{
  if (!meta_wayland_client_owns_window (client, window))
    return;

  if (!window->skip_from_window_list)
    {
      window->skip_from_window_list = TRUE;
      meta_window_recalc_features (window);
    }
}

/**
 * meta_wayland_client_show_in_window_list
 * @client: a #MetaWaylandClient
 * @window: (not nullable): a MetaWindow
 *
 * Shows again this window in window lists, like taskbars, pagers...
 */
void
meta_wayland_client_show_in_window_list (MetaWaylandClient *client,
                                         MetaWindow        *window)
{
  if (!meta_wayland_client_owns_window (client, window))
    return;

  if (window->skip_from_window_list)
    {
      window->skip_from_window_list = FALSE;
      meta_window_recalc_features (window);
    }
}
