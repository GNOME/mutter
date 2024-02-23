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
    GSubprocessLauncher *launcher;
    GSubprocess *subprocess;
    GCancellable *died_cancellable;
    gboolean process_running;
    gboolean process_launched;
  } subprocess;

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
  g_cancellable_cancel (client->subprocess.died_cancellable);
  g_clear_object (&client->subprocess.died_cancellable);
  g_clear_object (&client->subprocess.launcher);
  g_clear_object (&client->subprocess.subprocess);

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

static void
process_died (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  MetaWaylandClient *client = META_WAYLAND_CLIENT (user_data);

  client->subprocess.process_running = FALSE;
}

static void
child_setup (gpointer user_data)
{
  MetaDisplay *display = user_data;
  MetaContext *context = meta_display_get_context (display);

  meta_context_restore_rlimit_nofile (context, NULL);
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

/**
 * meta_wayland_client_new:
 * @context: (not nullable): a #MetaContext
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
meta_wayland_client_new (MetaContext          *context,
                         GSubprocessLauncher  *launcher,
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
  client->context = context;
  client->subprocess.launcher = g_object_ref (launcher);
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
  g_return_val_if_fail (!client->subprocess.launcher, -1);

  if (!init_wayland_client (client, &wayland_client, &fd, error))
    return -1;

  set_wayland_client (client, wayland_client);

  return fd;
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
  GSubprocess *subprocess;
  struct wl_client *wayland_client;
  int fd;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail (argv != NULL &&
                        argv[0] != NULL &&
                        argv[0][0] != '\0',
                        NULL);

  if (!client->subprocess.launcher)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "This client can not be launched");
      return NULL;
    }

  if (client->subprocess.process_launched)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "This object already has spawned a subprocess.");
      return NULL;
    }

  if (!client->subprocess.launcher)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_INITIALIZED,
                   "MetaWaylandClient must be created using meta_wayland_client_new().");
      return NULL;
    }

  if (!init_wayland_client (client, &wayland_client, &fd, error))
    return NULL;

  g_subprocess_launcher_take_fd (client->subprocess.launcher, fd, 3);
  g_subprocess_launcher_setenv (client->subprocess.launcher,
                                "WAYLAND_SOCKET", "3", TRUE);
  g_subprocess_launcher_set_child_setup (client->subprocess.launcher,
                                         child_setup, display, NULL);
  subprocess = g_subprocess_launcher_spawnv (client->subprocess.launcher, argv,
                                             error);
  g_clear_object (&client->subprocess.launcher);
  client->subprocess.process_launched = TRUE;

  if (subprocess == NULL)
    return NULL;

  set_wayland_client (client, wayland_client);

  client->subprocess.subprocess = subprocess;
  client->subprocess.process_running = TRUE;
  client->subprocess.died_cancellable = g_cancellable_new ();
  g_subprocess_wait_async (client->subprocess.subprocess,
                           client->subprocess.died_cancellable,
                           process_died,
                           client);

  return g_object_ref (client->subprocess.subprocess);
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
  g_return_val_if_fail (client->subprocess.subprocess != NULL, FALSE);
  g_return_val_if_fail (client->subprocess.process_running, FALSE);

  surface = meta_window_get_wayland_surface (window);
  if (surface == NULL || surface->resource == NULL)
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

/**
 * meta_wayland_client_make_desktop
 * @client: a #MetaWaylandClient
 * @window: (not nullable): a MetaWindow
 *
 * Mark window as DESKTOP window
 */
void
meta_wayland_client_make_desktop (MetaWaylandClient *client,
                                  MetaWindow        *window)
{
  g_return_if_fail (META_IS_WAYLAND_CLIENT (client));
  g_return_if_fail (META_IS_WINDOW (window));
  g_return_if_fail (window->type == META_WINDOW_NORMAL);

  if (!meta_wayland_client_owns_window (client, window))
    return;

  meta_window_set_type (window, META_WINDOW_DESKTOP);
}

/**
 * meta_wayland_client_make_dock:
 * @client: a #MetaWaylandClient
 * @window: a MetaWindow
 *
 * Mark window as DOCK window
 */
void
meta_wayland_client_make_dock (MetaWaylandClient *client,
                               MetaWindow        *window)
{
  g_return_if_fail (META_IS_WAYLAND_CLIENT (client));
  g_return_if_fail (META_IS_WINDOW (window));
  g_return_if_fail (window->type == META_WINDOW_NORMAL);

  if (!meta_wayland_client_owns_window (client, window))
    return;

  meta_window_set_type (window, META_WINDOW_DOCK);
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
