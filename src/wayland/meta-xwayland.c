/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * X Wayland Support
 *
 * Copyright (C) 2013 Intel Corporation
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

#include "config.h"

#include "wayland/meta-xwayland.h"
#include "wayland/meta-xwayland-private.h"

#include <errno.h>
#include <glib-unix.h>
#include <glib.h>
#include <sys/socket.h>
#include <sys/un.h>
#if defined(HAVE_SYS_RANDOM)
#include <sys/random.h>
#elif defined(HAVE_LINUX_RANDOM)
#include <linux/random.h>
#endif
#include <unistd.h>
#include <X11/Xauth.h>

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-private.h"
#include "core/main-private.h"
#include "meta/main.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "x11/meta-x11-display-private.h"

enum
{
  XWAYLAND_SURFACE_WINDOW_ASSOCIATED,

  XWAYLAND_SURFACE_LAST_SIGNAL
};

guint xwayland_surface_signals[XWAYLAND_SURFACE_LAST_SIGNAL];

#define META_TYPE_WAYLAND_SURFACE_ROLE_XWAYLAND (meta_wayland_surface_role_xwayland_get_type ())
G_DECLARE_FINAL_TYPE (MetaWaylandSurfaceRoleXWayland,
                      meta_wayland_surface_role_xwayland,
                      META, WAYLAND_SURFACE_ROLE_XWAYLAND,
                      MetaWaylandActorSurface)

struct _MetaWaylandSurfaceRoleXWayland
{
  MetaWaylandActorSurface parent;
};

G_DEFINE_TYPE (MetaWaylandSurfaceRoleXWayland,
               meta_wayland_surface_role_xwayland,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

static int display_number_override = -1;

static void meta_xwayland_stop_xserver (MetaXWaylandManager *manager);

void
meta_xwayland_associate_window_with_surface (MetaWindow          *window,
                                             MetaWaylandSurface  *surface)
{
  MetaDisplay *display = window->display;
  MetaWindowActor *window_actor;

  /* If the window has an existing surface, like if we're
   * undecorating or decorating the window, then we need
   * to detach the window from its old surface.
   */
  if (window->surface)
    {
      meta_wayland_surface_set_window (window->surface, NULL);
      window->surface = NULL;
    }

  if (!meta_wayland_surface_assign_role (surface,
                                         META_TYPE_WAYLAND_SURFACE_ROLE_XWAYLAND,
                                         NULL))
    {
      wl_resource_post_error (surface->resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  window->surface = surface;
  meta_wayland_surface_set_window (surface, window);
  g_signal_emit (surface->role,
                 xwayland_surface_signals[XWAYLAND_SURFACE_WINDOW_ASSOCIATED],
                 0);

  window_actor = meta_window_actor_from_window (window);
  if (window_actor)
    {
      MetaSurfaceActor *surface_actor;

      surface_actor = meta_wayland_surface_get_actor (surface);
      meta_window_actor_assign_surface_actor (window_actor, surface_actor);
    }

  /* Now that we have a surface check if it should have focus. */
  meta_display_sync_wayland_input_focus (display);
}

static gboolean
associate_window_with_surface_id (MetaXWaylandManager *manager,
                                  MetaWindow          *window,
                                  guint32              surface_id)
{
  struct wl_resource *resource;

  resource = wl_client_get_object (manager->client, surface_id);
  if (resource)
    {
      MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
      meta_xwayland_associate_window_with_surface (window, surface);
      return TRUE;
    }
  else
    return FALSE;
}

void
meta_xwayland_handle_wl_surface_id (MetaWindow *window,
                                    guint32     surface_id)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;

  if (!associate_window_with_surface_id (manager, window, surface_id))
    {
      /* No surface ID yet, schedule this association for whenever the
       * surface is made known.
       */
      meta_wayland_compositor_schedule_surface_association (compositor,
                                                            surface_id, window);
    }
}

gboolean
meta_xwayland_is_xwayland_surface (MetaWaylandSurface *surface)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;

  return wl_resource_get_client (surface->resource) == manager->client;
}

static gboolean
try_display (int    display,
             char **filename_out,
             int   *fd_out)
{
  gboolean ret = FALSE;
  char *filename;
  int fd;

  filename = g_strdup_printf ("/tmp/.X%d-lock", display);

 again:
  fd = open (filename, O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0444);

  if (fd < 0 && errno == EEXIST)
    {
      char pid[11];
      char *end;
      pid_t other;

      fd = open (filename, O_CLOEXEC, O_RDONLY);
      if (fd < 0 || read (fd, pid, 11) != 11)
        {
          g_warning ("can't read lock file %s: %m", filename);
          goto out;
        }
      close (fd);
      fd = -1;

      pid[10] = '\0';
      other = strtol (pid, &end, 0);
      if (end != pid + 10)
        {
          g_warning ("can't parse lock file %s", filename);
          goto out;
        }

      if (kill (other, 0) < 0 && errno == ESRCH)
        {
          /* Process is dead. Try unlinking the lock file and trying again. */
          if (unlink (filename) < 0)
            {
              g_warning ("failed to unlink stale lock file %s: %m", filename);
              goto out;
            }

          goto again;
        }

      goto out;
    }
  else if (fd < 0)
    {
      g_warning ("failed to create lock file %s: %m", filename);
      goto out;
    }

  ret = TRUE;

 out:
  if (!ret)
    {
      g_free (filename);
      filename = NULL;

      if (fd >= 0)
        {
          close (fd);
          fd = -1;
        }
    }

  *filename_out = filename;
  *fd_out = fd;
  return ret;
}

static char *
create_lock_file (int display, int *display_out)
{
  char *filename;
  int fd;

  char pid[12];
  int size;
  int number_of_tries = 0;

  while (!try_display (display, &filename, &fd))
    {
      display++;
      number_of_tries++;

      /* If we can't get a display after 50 times, then something's wrong. Just
       * abort in this case. */
      if (number_of_tries >= 50)
        return NULL;
    }

  /* Subtle detail: we use the pid of the wayland compositor, not the xserver
   * in the lock file. Another subtlety: snprintf returns the number of bytes
   * it _would've_ written without either the NUL or the size clamping, hence
   * the disparity in size. */
  size = snprintf (pid, 12, "%10d\n", getpid ());
  if (size != 11 || write (fd, pid, 11) != 11)
    {
      unlink (filename);
      close (fd);
      g_warning ("failed to write pid to lock file %s", filename);
      g_free (filename);
      return NULL;
    }

  close (fd);

  *display_out = display;
  return filename;
}

static int
bind_to_abstract_socket (int       display,
                         gboolean *fatal)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    {
      *fatal = TRUE;
      g_warning ("Failed to create socket: %m");
      return -1;
    }

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "%c/tmp/.X11-unix/X%d", 0, display);
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      *fatal = errno != EADDRINUSE;
      g_warning ("failed to bind to @%s: %m", addr.sun_path + 1);
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0)
    {
      *fatal = errno != EADDRINUSE;
      g_warning ("Failed to listen on abstract socket @%s: %m",
                 addr.sun_path + 1);
      close (fd);
      return -1;
    }

  return fd;
}

static int
bind_to_unix_socket (int display)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "/tmp/.X11-unix/X%d", display) + 1;
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  unlink (addr.sun_path);
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      g_warning ("failed to bind to %s: %m\n", addr.sun_path);
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0)
    {
      unlink (addr.sun_path);
      close (fd);
      return -1;
    }

  return fd;
}

static void
xserver_died (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  GSubprocess *proc = G_SUBPROCESS (source);
  g_autoptr (GError) error = NULL;

  if (!g_subprocess_wait_finish (proc, result, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to finish waiting for Xwayland: %s", error->message);
    }
  else if (!g_subprocess_get_successful (proc))
    {
      if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_MANDATORY)
        g_warning ("X Wayland crashed; exiting");
      else
        g_warning ("X Wayland crashed; attempting to recover");
    }

  if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_MANDATORY)
    {
      meta_exit (META_EXIT_ERROR);
    }
  else if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_ON_DEMAND)
    {
      MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
      MetaDisplay *display = meta_get_display ();

      if (display->x11_display)
        meta_display_shutdown_x11 (display);

      if (!meta_xwayland_init (&compositor->xwayland_manager,
                               compositor->wayland_display))
        g_warning ("Failed to init X sockets");
    }
}

static gboolean
shutdown_xwayland_cb (gpointer data)
{
  MetaXWaylandManager *manager = data;

  meta_verbose ("Shutting down Xwayland");
  manager->xserver_grace_period_id = 0;
  meta_display_shutdown_x11 (meta_get_display ());
  meta_xwayland_stop_xserver (manager);
  return G_SOURCE_REMOVE;
}

static int
x_io_error (Display *display)
{
  g_warning ("Connection to xwayland lost");

  if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_MANDATORY)
    meta_exit (META_EXIT_ERROR);

  return 0;
}

void
meta_xwayland_override_display_number (int number)
{
  display_number_override = number;
}

static gboolean
open_display_sockets (MetaXWaylandManager *manager,
                      int                  display_index,
                      gboolean            *fatal)
{
  int abstract_fd, unix_fd;

  abstract_fd = bind_to_abstract_socket (display_index,
                                         fatal);
  if (abstract_fd < 0)
    return FALSE;

  unix_fd = bind_to_unix_socket (display_index);
  if (unix_fd < 0)
    {
      *fatal = FALSE;
      close (abstract_fd);
      return FALSE;
    }

  manager->abstract_fd = abstract_fd;
  manager->unix_fd = unix_fd;

  return TRUE;
}

static gboolean
choose_xdisplay (MetaXWaylandManager *manager)
{
  int display = 0;
  char *lock_file = NULL;
  gboolean fatal = FALSE;

  if (display_number_override != -1)
    display = display_number_override;
  else if (g_getenv ("RUNNING_UNDER_GDM"))
    display = 1024;

  do
    {
      lock_file = create_lock_file (display, &display);
      if (!lock_file)
        {
          g_warning ("Failed to create an X lock file");
          return FALSE;
        }

      if (!open_display_sockets (manager, display, &fatal))
        {
          unlink (lock_file);

          if (!fatal)
            {
              display++;
              continue;
            }
          else
            {
              g_warning ("Failed to bind X11 socket");
              return FALSE;
            }
        }

      break;
    }
  while (1);

  manager->display_index = display;
  manager->display_name = g_strdup_printf (":%d", manager->display_index);
  manager->lock_file = lock_file;

  return TRUE;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FILE, fclose)

static gboolean
prepare_auth_file (MetaXWaylandManager *manager)
{
  Xauth auth_entry = { 0 };
  g_autoptr (FILE) fp = NULL;
  char auth_data[16];
  int fd;

  manager->auth_file = g_build_filename (g_get_user_runtime_dir (),
                                         ".mutter-Xwaylandauth.XXXXXX",
                                         NULL);

  if (getrandom (auth_data, sizeof (auth_data), 0) != sizeof (auth_data))
    {
      g_warning ("Failed to get random data: %s", g_strerror (errno));
      return FALSE;
    }

  auth_entry.family = FamilyLocal;
  auth_entry.address = (char *) g_get_host_name ();
  auth_entry.address_length = strlen (auth_entry.address);
  auth_entry.name = (char *) "MIT-MAGIC-COOKIE-1";
  auth_entry.name_length = strlen (auth_entry.name);
  auth_entry.data = auth_data;
  auth_entry.data_length = sizeof (auth_data);

  fd = g_mkstemp (manager->auth_file);
  if (fd < 0)
    {
      g_warning ("Failed to open Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  fp = fdopen (fd, "w+");
  if (!fp)
    {
      g_warning ("Failed to open Xauthority stream: %s", g_strerror (errno));
      close (fd);
      return FALSE;
    }

  if (!XauWriteAuth (fp, &auth_entry))
    {
      g_warning ("Error writing to Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  auth_entry.family = FamilyWild;
  if (!XauWriteAuth (fp, &auth_entry))
    {
      g_warning ("Error writing to Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  if (fflush (fp) == EOF)
    {
      g_warning ("Error writing to Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

static void
add_local_user_to_xhost (Display *xdisplay)
{
  XHostAddress host_entry;
  XServerInterpretedAddress siaddr;

  siaddr.type = (char *) "localuser";
  siaddr.typelength = strlen (siaddr.type);
  siaddr.value = (char *) g_get_user_name();
  siaddr.valuelength = strlen (siaddr.value);

  host_entry.family = FamilyServerInterpreted;
  host_entry.address = (char *) &siaddr;

  XAddHost (xdisplay, &host_entry);
}

static void
xserver_finished_init (MetaXWaylandManager *manager)
{
  /* At this point xwayland is all setup to start accepting
   * connections so we can quit the transient initialization mainloop
   * and unblock meta_wayland_init() to continue initializing mutter.
   * */
  g_main_loop_quit (manager->init_loop);
  g_clear_pointer (&manager->init_loop, g_main_loop_unref);
}

static gboolean
on_displayfd_ready (int          fd,
                    GIOCondition condition,
                    gpointer     user_data)
{
  MetaXWaylandManager *manager = user_data;
  MetaDisplay *display = meta_get_display ();

  /* The server writes its display name to the displayfd
   * socket when it's ready. We don't care about the data
   * in the socket, just that it wrote something, since
   * that means it's ready. */
  xserver_finished_init (manager);

  if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_ON_DEMAND)
    meta_display_init_x11 (display, NULL);

  return G_SOURCE_REMOVE;
}

static gboolean
meta_xwayland_start_xserver (MetaXWaylandManager *manager)
{
  int xwayland_client_fd[2];
  int displayfd[2];
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  GSubprocessFlags flags;
  GError *error = NULL;

  /* We want xwayland to be a wayland client so we make a socketpair to setup a
   * wayland protocol connection. */
  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xwayland_client_fd) < 0)
    {
      g_warning ("xwayland_client_fd socketpair failed\n");
      return FALSE;
    }

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, displayfd) < 0)
    {
      g_warning ("displayfd socketpair failed\n");
      return FALSE;
    }

  /* xwayland, please. */
  flags = G_SUBPROCESS_FLAGS_NONE;

  if (getenv ("XWAYLAND_STFU"))
    {
      flags |= G_SUBPROCESS_FLAGS_STDOUT_SILENCE;
      flags |= G_SUBPROCESS_FLAGS_STDERR_SILENCE;
    }

  launcher = g_subprocess_launcher_new (flags);

  g_subprocess_launcher_take_fd (launcher, xwayland_client_fd[1], 3);
  g_subprocess_launcher_take_fd (launcher, manager->abstract_fd, 4);
  g_subprocess_launcher_take_fd (launcher, manager->unix_fd, 5);
  g_subprocess_launcher_take_fd (launcher, displayfd[1], 6);

  g_subprocess_launcher_setenv (launcher, "WAYLAND_SOCKET", "3", TRUE);

  manager->proc = g_subprocess_launcher_spawn (launcher, &error,
                                               XWAYLAND_PATH, manager->display_name,
                                               "-rootless",
                                               "-noreset",
                                               "-accessx",
                                               "-core",
                                               "-auth", manager->auth_file,
                                               "-listen", "4",
                                               "-listen", "5",
                                               "-displayfd", "6",
                                               NULL);
  if (!manager->proc)
    {
      g_error ("Failed to spawn Xwayland: %s", error->message);
      return FALSE;
    }

  manager->xserver_died_cancellable = g_cancellable_new ();
  g_subprocess_wait_async (manager->proc, manager->xserver_died_cancellable,
                           xserver_died, NULL);
  g_unix_fd_add (displayfd[0], G_IO_IN, on_displayfd_ready, manager);
  manager->client = wl_client_create (manager->wayland_display,
                                      xwayland_client_fd[0]);

  /* We need to run a mainloop until we know xwayland has a binding
   * for our xserver interface at which point we can assume it's
   * ready to start accepting connections. */
  manager->init_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (manager->init_loop);

  return TRUE;
}

static gboolean
xdisplay_connection_activity_cb (gint         fd,
                                 GIOCondition cond,
                                 gpointer     user_data)
{
  MetaXWaylandManager *manager = user_data;

  if (!meta_xwayland_start_xserver (manager))
    g_critical ("Could not start Xserver");

  return G_SOURCE_REMOVE;
}

static void
meta_xwayland_stop_xserver_timeout (MetaXWaylandManager *manager)
{
  if (manager->xserver_grace_period_id)
    return;

  manager->xserver_grace_period_id =
    g_timeout_add_seconds (10, shutdown_xwayland_cb, manager);
}

static void
window_unmanaged_cb (MetaWindow          *window,
                     MetaXWaylandManager *manager)
{
  manager->x11_windows = g_list_remove (manager->x11_windows, window);
  g_signal_handlers_disconnect_by_func (window,
                                        window_unmanaged_cb,
                                        manager);
  if (!manager->x11_windows)
    {
      meta_verbose ("All X11 windows gone, setting shutdown timeout");
      meta_xwayland_stop_xserver_timeout (manager);
    }
}

static void
window_created_cb (MetaDisplay         *display,
                   MetaWindow          *window,
                   MetaXWaylandManager *manager)
{
  /* Ignore all internal windows */
  if (!window->xwindow ||
      meta_window_get_client_pid (window) == getpid ())
    return;

  manager->x11_windows = g_list_prepend (manager->x11_windows, window);
  g_signal_connect (window, "unmanaged",
                    G_CALLBACK (window_unmanaged_cb), manager);

  if (manager->xserver_grace_period_id)
    {
      g_source_remove (manager->xserver_grace_period_id);
      manager->xserver_grace_period_id = 0;
    }
}

static void
meta_xwayland_stop_xserver (MetaXWaylandManager *manager)
{
  if (manager->proc)
    g_subprocess_send_signal (manager->proc, SIGTERM);
  g_signal_handlers_disconnect_by_func (meta_get_display (),
                                        window_created_cb,
                                        manager);
  g_clear_object (&manager->xserver_died_cancellable);
  g_clear_object (&manager->proc);
}

gboolean
meta_xwayland_init (MetaXWaylandManager *manager,
                    struct wl_display   *wl_display)
{
  MetaDisplayPolicy policy;
  gboolean fatal;

  if (!manager->display_name)
    {
      if (!choose_xdisplay (manager))
        return FALSE;

      if (!prepare_auth_file (manager))
        return FALSE;
    }
  else
    {
      if (!open_display_sockets (manager, manager->display_index, &fatal))
        return FALSE;
    }

  manager->wayland_display = wl_display;
  policy = meta_get_x11_display_policy ();

  if (policy == META_DISPLAY_POLICY_MANDATORY)
    {
      return meta_xwayland_start_xserver (manager);
    }
  else if (policy == META_DISPLAY_POLICY_ON_DEMAND)
    {
      g_unix_fd_add (manager->abstract_fd, G_IO_IN,
                     xdisplay_connection_activity_cb, manager);
      return TRUE;
    }

  return FALSE;
}

static void
on_x11_display_closing (MetaDisplay *display)
{
  Display *xdisplay = meta_x11_display_get_xdisplay (display->x11_display);

  meta_xwayland_shutdown_dnd (xdisplay);
  g_signal_handlers_disconnect_by_func (display,
                                        on_x11_display_closing,
                                        NULL);
}

/* To be called right after connecting */
void
meta_xwayland_complete_init (MetaDisplay *display,
                             Display     *xdisplay)
{
  MetaWaylandCompositor *compositor = meta_wayland_compositor_get_default ();
  MetaXWaylandManager *manager = &compositor->xwayland_manager;

  /* We install an X IO error handler in addition to the child watch,
     because after Xlib connects our child watch may not be called soon
     enough, and therefore we won't crash when X exits (and most important
     we won't reset the tty).
  */
  XSetIOErrorHandler (x_io_error);

  g_signal_connect (display, "x11-display-closing",
                    G_CALLBACK (on_x11_display_closing), NULL);
  meta_xwayland_init_dnd (xdisplay);
  add_local_user_to_xhost (xdisplay);

  if (meta_get_x11_display_policy () == META_DISPLAY_POLICY_ON_DEMAND)
    {
      meta_xwayland_stop_xserver_timeout (manager);
      g_signal_connect (meta_get_display (), "window-created",
                        G_CALLBACK (window_created_cb), manager);
    }
}

void
meta_xwayland_shutdown (MetaXWaylandManager *manager)
{
  char path[256];

  g_cancellable_cancel (manager->xserver_died_cancellable);

  snprintf (path, sizeof path, "/tmp/.X11-unix/X%d", manager->display_index);
  unlink (path);

  g_clear_pointer (&manager->display_name, g_free);
  if (manager->auth_file)
    {
      unlink (manager->auth_file);
      g_clear_pointer (&manager->auth_file, g_free);
    }
  if (manager->lock_file)
    {
      unlink (manager->lock_file);
      g_clear_pointer (&manager->lock_file, g_free);
    }
}

static void
xwayland_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_surface_role_xwayland_parent_class);

  /* See comment in xwayland_surface_commit for why we reply even though the
   * surface may not be drawn the next frame.
   */
  wl_list_insert_list (&surface->compositor->frame_callbacks,
                       &surface->pending_frame_callback_list);
  wl_list_init (&surface->pending_frame_callback_list);

  surface_role_class->assigned (surface_role);
}

static void
xwayland_surface_commit (MetaWaylandSurfaceRole  *surface_role,
                         MetaWaylandPendingState *pending)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_surface_role_xwayland_parent_class);

  /* For Xwayland windows, throttling frames when the window isn't actually
   * drawn is less useful, because Xwayland still has to do the drawing sent
   * from the application - the throttling would only be of sending us damage
   * messages, so we simplify and send frame callbacks after the next paint of
   * the screen, whether the window was drawn or not.
   *
   * Currently it may take a few frames before we draw the window, for not
   * completely understood reasons, and in that case, not thottling frame
   * callbacks to drawing has the happy side effect that we avoid showing the
   * user the initial black frame from when the window is mapped empty.
   */
  meta_wayland_surface_queue_pending_state_frame_callbacks (surface, pending);

  surface_role_class->commit (surface_role, pending);
}

static MetaWaylandSurface *
xwayland_surface_get_toplevel (MetaWaylandSurfaceRole *surface_role)
{
  return meta_wayland_surface_role_get_surface (surface_role);
}

static void
xwayland_surface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_wayland_surface_role_xwayland_parent_class);

  if (surface->window)
    actor_surface_class->sync_actor_state (actor_surface);
}

static void
meta_wayland_surface_role_xwayland_init (MetaWaylandSurfaceRoleXWayland *role)
{
}

static void
meta_wayland_surface_role_xwayland_class_init (MetaWaylandSurfaceRoleXWaylandClass *klass)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  surface_role_class->assigned = xwayland_surface_assigned;
  surface_role_class->commit = xwayland_surface_commit;
  surface_role_class->get_toplevel = xwayland_surface_get_toplevel;

  actor_surface_class->sync_actor_state = xwayland_surface_sync_actor_state;

  xwayland_surface_signals[XWAYLAND_SURFACE_WINDOW_ASSOCIATED] =
    g_signal_new ("window-associated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}
