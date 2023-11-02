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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "wayland/meta-xwayland.h"
#include "wayland/meta-xwayland-private.h"

#include <errno.h>
#include <glib-unix.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#if defined(HAVE_SYS_RANDOM)
#include <sys/random.h>
#elif defined(HAVE_LINUX_RANDOM)
#include <linux/random.h>
#endif
#include <unistd.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xauth.h>
#include <X11/Xlib-xcb.h>

#include <xcb/res.h>

#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-settings-private.h"
#include "meta/main.h"
#include "meta/meta-backend.h"
#include "mtk/mtk-x11.h"
#include "wayland/meta-xwayland-grab-keyboard.h"
#include "wayland/meta-xwayland-surface.h"
#include "x11/meta-x11-display-private.h"

#ifdef HAVE_XWAYLAND_LISTENFD
#define XWAYLAND_LISTENFD "-listenfd"
#else
#define XWAYLAND_LISTENFD "-listen"
#endif

#define TMP_UNIX_DIR         "/tmp"
#define X11_TMP_UNIX_DIR     "/tmp/.X11-unix"
#define X11_TMP_UNIX_PATH    "/tmp/.X11-unix/X"

static int display_number_override = -1;

static void meta_xwayland_stop_xserver (MetaXWaylandManager *manager);

static void
meta_xwayland_set_primary_output (MetaX11Display *x11_display);

static MetaMonitorManager *
monitor_manager_from_x11_display (MetaX11Display *x11_display)
{
  MetaDisplay *display = meta_x11_display_get_display (x11_display);
  MetaContext *context = meta_display_get_context (display);
  MetaBackend *backend = meta_context_get_backend (context);

  return meta_backend_get_monitor_manager (backend);
}

void
meta_xwayland_associate_window_with_surface (MetaWindow          *window,
                                             MetaWaylandSurface  *surface)
{
  MetaDisplay *display = window->display;
  MetaXwaylandSurface *xwayland_surface;
  MetaContext *context = meta_display_get_context (display);
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (context);

  if (!meta_wayland_surface_assign_role (surface,
                                         META_TYPE_XWAYLAND_SURFACE,
                                         NULL))
    {
      wl_resource_post_error (surface->resource,
                              WL_DISPLAY_ERROR_INVALID_OBJECT,
                              "wl_surface@%d already has a different role",
                              wl_resource_get_id (surface->resource));
      return;
    }

  xwayland_surface = META_XWAYLAND_SURFACE (surface->role);
  meta_xwayland_surface_associate_with_window (xwayland_surface, window);

  /* Now that we have a surface check if it should have focus. */
  meta_wayland_compositor_sync_focus (wayland_compositor);
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
  MetaDisplay *display = meta_window_get_display (window);
  MetaContext *context = meta_display_get_context (display);
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);
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

static gboolean
try_display (int      display,
             char   **filename_out,
             int     *fd_out,
             GError **error)
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
      int read_bytes;

      fd = open (filename, O_CLOEXEC, O_RDONLY);
      if (fd < 0)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to open lock file %s: %s",
                       filename, g_strerror (errno));
          goto out;
        }

      read_bytes = read (fd, pid, 11);
      if (read_bytes != 11)
        {
          if (read_bytes < 0)
            {
              g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Failed to read from lock file %s: %s",
                           filename, g_strerror (errno));
            }
          else
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT,
                           "Only read %d bytes (needed 11) from lock file: %s",
                           read_bytes, filename);
            }
          goto out;
        }
      close (fd);
      fd = -1;

      pid[10] = '\0';
      other = strtol (pid, &end, 0);
      if (end != pid + 10)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                       "Can't parse lock file %s", filename);
          goto out;
        }

      if (kill (other, 0) < 0 && errno == ESRCH)
        {
          /* Process is dead. Try unlinking the lock file and trying again. */
          if (unlink (filename) < 0)
            {
              g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "Failed to unlink stale lock file %s: %s",
                           filename, g_strerror (errno));
              goto out;
            }

          goto again;
        }

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Lock file %s is already occupied", filename);
      goto out;
    }
  else if (fd < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                  "Failed to create lock file %s: %s",
                  filename, g_strerror (errno));
      goto out;
    }

  ret = TRUE;

 out:
  if (!ret)
    {
      g_free (filename);
      filename = NULL;

      g_clear_fd (&fd, NULL);
    }

  *filename_out = filename;
  *fd_out = fd;
  return ret;
}

static char *
create_lock_file (int      display,
                  int     *display_out,
                  GError **error)
{
  char *filename;
  int fd;
  char pid[12];
  int size;
  int number_of_tries = 0;
  g_autoptr (GError) local_error = NULL;

  while (!try_display (display, &filename, &fd, &local_error))
    {
      meta_topic (META_DEBUG_WAYLAND,
                  "Failed to lock X11 display: %s", local_error->message);
      g_clear_error (&local_error);
      display++;
      number_of_tries++;

      /* If we can't get a display after 50 times, then something's wrong. Just
       * abort in this case. */
      if (number_of_tries >= 50)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Gave up after trying to lock different "
                       "X11 display lock file 50 times");
          return NULL;
        }
    }

  /* Subtle detail: we use the pid of the wayland compositor, not the xserver
   * in the lock file. Another subtlety: snprintf returns the number of bytes
   * it _would've_ written without either the NUL or the size clamping, hence
   * the disparity in size. */
  size = snprintf (pid, 12, "%10d\n", getpid ());
  errno = 0;
  if (size != 11 || write (fd, pid, 11) != 11)
    {
      if (errno != 0)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to write pid to lock file %s: %s",
                       filename, g_strerror (errno));
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Failed to write pid to lock file %s", filename);
        }

      unlink (filename);
      close (fd);
      g_free (filename);
      return NULL;
    }

  close (fd);

  *display_out = display;
  return filename;
}

static int
bind_to_abstract_socket (int      display,
                         GError **error)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to create socket: %s", g_strerror (errno));
      return -1;
    }

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "%c%s%d", 0, X11_TMP_UNIX_PATH, display);
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to bind to %s: %s",
                   addr.sun_path + 1, g_strerror (errno));
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to listen to %s: %s",
                   addr.sun_path + 1, g_strerror (errno));
      close (fd);
      return -1;
    }

  return fd;
}

static int
bind_to_unix_socket (int      display,
                     GError **error)
{
  struct sockaddr_un addr;
  socklen_t size, name_size;
  int fd;

  fd = socket (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to create socket: %s", g_strerror (errno));
      return -1;
    }

  addr.sun_family = AF_LOCAL;
  name_size = snprintf (addr.sun_path, sizeof addr.sun_path,
                        "%s%d", X11_TMP_UNIX_PATH, display) + 1;
  size = offsetof (struct sockaddr_un, sun_path) + name_size;
  unlink (addr.sun_path);
  if (bind (fd, (struct sockaddr *) &addr, size) < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to bind to %s: %s",
                   addr.sun_path, g_strerror (errno));
      close (fd);
      return -1;
    }

  if (listen (fd, 1) < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to listen to %s: %s",
                   addr.sun_path, g_strerror (errno));
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
  MetaXWaylandManager *manager = user_data;
  MetaWaylandCompositor *compositor = manager->compositor;
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);
  GSubprocess *proc = G_SUBPROCESS (source);
  g_autoptr (GError) error = NULL;
  MetaX11DisplayPolicy x11_display_policy;

  if (!g_subprocess_wait_finish (proc, result, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to finish waiting for Xwayland: %s", error->message);
    }

  x11_display_policy =
    meta_context_get_x11_display_policy (compositor->context);
  if (!g_subprocess_get_successful (proc))
    {
      if (x11_display_policy == META_X11_DISPLAY_POLICY_MANDATORY)
        g_warning ("X Wayland crashed; exiting");
      else
        g_warning ("X Wayland crashed; attempting to recover");
    }

  if (x11_display_policy == META_X11_DISPLAY_POLICY_MANDATORY)
    {
      meta_exit (META_EXIT_ERROR);
    }
  else if (x11_display_policy == META_X11_DISPLAY_POLICY_ON_DEMAND)
    {
      g_autoptr (GError) error = NULL;

      if (display->x11_display)
        meta_display_shutdown_x11 (display);

      if (!meta_xwayland_init (&compositor->xwayland_manager,
                               compositor,
                               compositor->wayland_display,
                               &error))
        g_warning ("Failed to init X sockets: %s", error->message);
    }
}

static void
meta_xwayland_terminate (MetaXWaylandManager *manager)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (manager->compositor);
  MetaDisplay *display = meta_context_get_display (context);

  meta_display_shutdown_x11 (display);
  meta_xwayland_stop_xserver (manager);
  g_clear_signal_handler (&manager->prepare_shutdown_id, manager->compositor);
}

static int
x_io_error (Display *display)
{
  g_warning ("Connection to xwayland lost");

  return 0;
}

static int
x_io_error_noop (Display *display)
{
  return 0;
}

static void
x_io_error_exit (Display *display,
                 void    *data)
{
  MetaXWaylandManager *manager = data;
  MetaContext *context = manager->compositor->context;
  MetaX11DisplayPolicy x11_display_policy;

  x11_display_policy =
    meta_context_get_x11_display_policy (context);

  if (x11_display_policy == META_X11_DISPLAY_POLICY_MANDATORY)
    {
      GError *error;

      g_warning ("Xwayland terminated, exiting since it was mandatory");
      error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Xwayland exited unexpectedly");
      meta_context_terminate_with_error (context, error);
    }
  else
    {
      meta_topic (META_DEBUG_WAYLAND, "Xwayland disappeared");
    }
}

static void
x_io_error_exit_noop (Display *display,
                      void    *data)
{
}

void
meta_xwayland_override_display_number (int number)
{
  display_number_override = number;
}

static gboolean
ensure_x11_unix_perms (GError **error)
{
  /* Try to detect systems on which /tmp/.X11-unix is owned by neither root nor
   * ourselves because in that case the owner can take over the socket we create
   * (symlink races are fixed in linux 800179c9b8a1). This should not be
   * possible in the first place and systems should come with some way to ensure
   * that's the case (systemd-tmpfiles, polyinstantiation …).
   *
   * That check however only works if we see the root user namespace which might
   * not be the case when running in e.g. toolbx (root and other user are all
   * mapped to overflowuid). */
  struct stat x11_tmp, tmp;

  if (lstat (X11_TMP_UNIX_DIR, &x11_tmp) != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to check permissions on directory \"%s\": %s",
                   X11_TMP_UNIX_DIR, g_strerror (errno));
      return FALSE;
    }

  if (lstat (TMP_UNIX_DIR, &tmp) != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to check permissions on directory \"%s\": %s",
                   TMP_UNIX_DIR, g_strerror (errno));
      return FALSE;
    }

  /* If the directory already exists, it should belong to the same
   * user as /tmp or belong to ourselves ...
   * (if /tmp is not owned by root or ourselves we're in deep trouble) */
  if (x11_tmp.st_uid != tmp.st_uid && x11_tmp.st_uid != getuid ())
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Wrong ownership for directory \"%s\"",
                   X11_TMP_UNIX_DIR);
      return FALSE;
    }

  /* ... be writable ... */
  if ((x11_tmp.st_mode & 0022) != 0022)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Directory \"%s\" is not writable",
                   X11_TMP_UNIX_DIR);
      return FALSE;
    }

  /* ... and have the sticky bit set */
  if ((x11_tmp.st_mode & 01000) != 01000)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Directory \"%s\" is missing the sticky bit",
                   X11_TMP_UNIX_DIR);
      return FALSE;
    }

  return TRUE;
}

static gboolean
ensure_x11_unix_dir (GError **error)
{
  if (mkdir (X11_TMP_UNIX_DIR, 01777) != 0)
    {
      if (errno == EEXIST)
        return ensure_x11_unix_perms (error);

      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to create directory \"%s\": %s",
                   X11_TMP_UNIX_DIR, g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

static gboolean
open_display_sockets (MetaXWaylandManager  *manager,
                      int                   display_index,
                      int                  *abstract_fd_out,
                      int                  *unix_fd_out,
                      GError              **error)
{
  int abstract_fd, unix_fd;

  abstract_fd = bind_to_abstract_socket (display_index, error);
  if (abstract_fd < 0)
    return FALSE;

  unix_fd = bind_to_unix_socket (display_index, error);
  if (unix_fd < 0)
    {
      close (abstract_fd);
      return FALSE;
    }

  *abstract_fd_out = abstract_fd;
  *unix_fd_out = unix_fd;

  return TRUE;
}

static gboolean
choose_xdisplay (MetaXWaylandManager     *manager,
                 MetaXWaylandConnection  *connection,
                 int                     *display,
                 GError                 **error)
{
  int number_of_tries = 0;
  char *lock_file = NULL;

  if (!ensure_x11_unix_dir (error))
    return FALSE;

  do
    {
      g_autoptr (GError) local_error = NULL;

      lock_file = create_lock_file (*display, display, &local_error);
      if (!lock_file)
        {
          g_prefix_error (&local_error, "Failed to create an X lock file: ");
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      if (!open_display_sockets (manager, *display,
                                 &connection->abstract_fd,
                                 &connection->unix_fd,
                                 &local_error))
        {
          unlink (lock_file);

          if (++number_of_tries >= 50)
            {
              g_prefix_error (&local_error, "Failed to bind X11 socket: ");
              g_propagate_error (error, g_steal_pointer (&local_error));
              g_free (lock_file);
              return FALSE;
            }

          (*display)++;
          continue;
        }

      break;
    }
  while (1);

  connection->display_index = *display;
  connection->name = g_strdup_printf (":%d", connection->display_index);
  connection->lock_file = lock_file;

  return TRUE;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FILE, fclose)

static gboolean
prepare_auth_file (MetaXWaylandManager  *manager,
                   GError              **error)
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
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to get random data: %s", g_strerror (errno));
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
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to open Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  fp = fdopen (fd, "w+");
  if (!fp)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Failed to open Xauthority stream: %s", g_strerror (errno));
      close (fd);
      return FALSE;
    }

  if (!XauWriteAuth (fp, &auth_entry))
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Error writing to Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  auth_entry.family = FamilyWild;
  if (!XauWriteAuth (fp, &auth_entry))
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Error writing to Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  if (fflush (fp) == EOF)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "Error writing to Xauthority file: %s", g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

static void
on_init_x11_cb (MetaDisplay  *display,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr (GError) error = NULL;

  if (!meta_display_init_x11_finish (display, result, &error))
    g_warning ("Failed to initialize X11 display: %s", error->message);
}

static gboolean
on_displayfd_ready (int          fd,
                    GIOCondition condition,
                    gpointer     user_data)
{
  GTask *task = user_data;

  /* The server writes its display name to the displayfd
   * socket when it's ready. We don't care about the data
   * in the socket, just that it wrote something, since
   * that means it's ready. */
  g_task_return_boolean (task, !!(condition & G_IO_IN));
  g_object_unref (task);

  return G_SOURCE_REMOVE;
}

static int
steal_fd (int *fd_ptr)
{
  int fd = *fd_ptr;
  *fd_ptr = -1;
  return fd;
}

void
meta_xwayland_start_xserver (MetaXWaylandManager *manager,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  MetaWaylandCompositor *compositor = manager->compositor;
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaBackend *backend = meta_context_get_backend (context);
  int xwayland_client_fd[2];
  int displayfd[2];
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  GSubprocessFlags flags;
  GError *error = NULL;
  g_autoptr (GTask) task = NULL;
  MetaSettings *settings;
  const char *args[32];
  int xwayland_disable_extensions;
  int i, j;
#ifdef HAVE_XWAYLAND_TERMINATE_DELAY
  MetaX11DisplayPolicy x11_display_policy =
    meta_context_get_x11_display_policy (compositor->context);
#endif
  struct {
    const char *extension_name;
    MetaXwaylandExtension disable_extension;
  } x11_extension_names[] = {
    { "SECURITY", META_XWAYLAND_EXTENSION_SECURITY },
    { "XTEST", META_XWAYLAND_EXTENSION_XTEST },
  };

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_xwayland_start_xserver);
  g_task_set_task_data (task, manager, NULL);

  /* We want xwayland to be a wayland client so we make a socketpair to setup a
   * wayland protocol connection. */
  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xwayland_client_fd) < 0)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               g_io_error_from_errno (errno),
                               "xwayland_client_fd socketpair failed");
      return;
    }

  if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, displayfd) < 0)
    {
      close (xwayland_client_fd[0]);
      close (xwayland_client_fd[1]);

      g_task_return_new_error (task,
                               G_IO_ERROR,
                               g_io_error_from_errno (errno),
                               "displayfd socketpair failed");
      return;
    }

  /* xwayland, please. */
  flags = G_SUBPROCESS_FLAGS_NONE;

  if (getenv ("XWAYLAND_STFU"))
    {
      flags |= G_SUBPROCESS_FLAGS_STDOUT_SILENCE;
      flags |= G_SUBPROCESS_FLAGS_STDERR_SILENCE;
    }

  settings = meta_backend_get_settings (backend);
  xwayland_disable_extensions =
    meta_settings_get_xwayland_disable_extensions (settings);

  launcher = g_subprocess_launcher_new (flags);

  g_subprocess_launcher_take_fd (launcher,
                                 steal_fd (&xwayland_client_fd[1]), 3);
  g_subprocess_launcher_take_fd (launcher,
                                 steal_fd (&manager->public_connection.abstract_fd), 4);
  g_subprocess_launcher_take_fd (launcher,
                                 steal_fd (&manager->public_connection.unix_fd), 5);
  g_subprocess_launcher_take_fd (launcher,
                                 steal_fd (&displayfd[1]), 6);
  g_subprocess_launcher_take_fd (launcher,
                                 steal_fd (&manager->private_connection.abstract_fd), 7);

  g_subprocess_launcher_setenv (launcher, "WAYLAND_SOCKET", "3", TRUE);

  i = 0;
  args[i++] = XWAYLAND_PATH;
  args[i++] = manager->public_connection.name;
  args[i++] = "-rootless";
  args[i++] = "-noreset";
  args[i++] = "-accessx";
  args[i++] = "-core";
  args[i++] = "-auth";
  args[i++] = manager->auth_file;
  args[i++] = XWAYLAND_LISTENFD;
  args[i++] = "4";
  args[i++] = XWAYLAND_LISTENFD;
  args[i++] = "5";
  args[i++] = "-displayfd";
  args[i++] = "6";
#ifdef HAVE_XWAYLAND_INITFD
  args[i++] = "-initfd";
  args[i++] = "7";
#else
  args[i++] = XWAYLAND_LISTENFD;
  args[i++] = "7";
#endif

#ifdef HAVE_XWAYLAND_BYTE_SWAPPED_CLIENTS
  if (meta_settings_are_xwayland_byte_swapped_clients_allowed (settings))
    args[i++] = "+byteswappedclients";
  else
    args[i++] = "-byteswappedclients";
#endif

  if (meta_settings_is_experimental_feature_enabled (settings,
                                                     META_EXPERIMENTAL_FEATURE_AUTOCLOSE_XWAYLAND))
#ifdef HAVE_XWAYLAND_TERMINATE_DELAY
    {
      if (x11_display_policy == META_X11_DISPLAY_POLICY_ON_DEMAND)
        {
          /* Terminate after a 10 seconds delay */
          args[i++] = "-terminate";
          args[i++] = "10";
        }
      else
        {
          g_warning ("autoclose-xwayland disabled, requires Xwayland on demand");
        }
    }
#else
    {
      g_warning ("autoclose-xwayland disabled, not supported");
    }
#endif
#ifdef HAVE_XWAYLAND_ENABLE_EI_PORTAL
    if (manager->should_enable_ei_portal)
      {
        /* Enable portal support */
        args[i++] = "-enable-ei-portal";
      }
#endif
  for (j = 0; j <  G_N_ELEMENTS (x11_extension_names); j++)
    {
      /* Make sure we don't go past the array size - We need room for
       * 2 arguments, plus the last NULL terminator.
       */
      if (i + 3 > G_N_ELEMENTS (args))
        break;

      if (xwayland_disable_extensions & x11_extension_names[j].disable_extension)
        {
          args[i++] = "-extension";
          args[i++] = x11_extension_names[j].extension_name;
        }
  }
  /* Terminator */
  args[i++] = NULL;

  manager->proc = g_subprocess_launcher_spawnv (launcher, args, &error);

  if (!manager->proc)
    {
      close (displayfd[0]);
      close (xwayland_client_fd[0]);

      g_task_return_error (task, error);
      return;
    }

  manager->xserver_died_cancellable = g_cancellable_new ();
  g_subprocess_wait_async (manager->proc, manager->xserver_died_cancellable,
                           xserver_died, manager);
  g_unix_fd_add (displayfd[0], G_IO_IN, on_displayfd_ready,
                 g_steal_pointer (&task));
  manager->client = wl_client_create (manager->wayland_display,
                                      xwayland_client_fd[0]);
}

gboolean
meta_xwayland_start_xserver_finish (MetaXWaylandManager  *manager,
                                    GAsyncResult         *result,
                                    GError              **error)
{
  g_assert (g_task_get_source_tag (G_TASK (result)) ==
            meta_xwayland_start_xserver);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
xdisplay_connection_activity_cb (gint         fd,
                                 GIOCondition cond,
                                 gpointer     user_data)
{
  MetaXWaylandManager *manager = user_data;
  MetaContext *context =
    meta_wayland_compositor_get_context (manager->compositor);
  MetaDisplay *display = meta_context_get_display (context);

  meta_display_init_x11 (display, NULL,
                         (GAsyncReadyCallback) on_init_x11_cb, NULL);

  /* Stop watching both file descriptors */
  g_clear_handle_id (&manager->abstract_fd_watch_id, g_source_remove);
  g_clear_handle_id (&manager->unix_fd_watch_id, g_source_remove);

  return G_SOURCE_REMOVE;
}

static void
meta_xwayland_stop_xserver (MetaXWaylandManager *manager)
{
  if (manager->proc)
    g_subprocess_send_signal (manager->proc, SIGTERM);
  g_clear_object (&manager->xserver_died_cancellable);
  g_clear_object (&manager->proc);
}

static void
meta_xwayland_connection_release (MetaXWaylandConnection *connection)
{
  unlink (connection->lock_file);
  g_clear_pointer (&connection->lock_file, g_free);
}

static void
meta_xwayland_shutdown (MetaWaylandCompositor *compositor)
{
  MetaXWaylandManager *manager = &compositor->xwayland_manager;
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);
  MetaX11Display *x11_display;
  char path[256];

  g_cancellable_cancel (manager->xserver_died_cancellable);

  XSetIOErrorHandler (x_io_error_noop);
  x11_display = display->x11_display;
  if (x11_display)
    {
      XSetIOErrorExitHandler (meta_x11_display_get_xdisplay (x11_display),
                              x_io_error_exit_noop, NULL);
    }

  meta_xwayland_terminate (manager);

  if (manager->public_connection.name)
    {
      snprintf (path, sizeof path, "%s%d", X11_TMP_UNIX_PATH,
                manager->public_connection.display_index);
      unlink (path);
      g_clear_pointer (&manager->public_connection.name, g_free);
    }

  if (manager->private_connection.name)
    {
      snprintf (path, sizeof path, "%s%d", X11_TMP_UNIX_PATH,
                manager->private_connection.display_index);
      unlink (path);
      g_clear_pointer (&manager->private_connection.name, g_free);
    }

  meta_xwayland_connection_release (&manager->public_connection);
  meta_xwayland_connection_release (&manager->private_connection);

  if (manager->auth_file)
    {
      unlink (manager->auth_file);
      g_clear_pointer (&manager->auth_file, g_free);
    }
}

gboolean
meta_xwayland_init (MetaXWaylandManager    *manager,
                    MetaWaylandCompositor  *compositor,
                    struct wl_display      *wl_display,
                    GError                **error)
{
  MetaContext *context = compositor->context;
  MetaX11DisplayPolicy policy;
  int display = 0;

  if (display_number_override != -1)
    display = display_number_override;
  else if (g_getenv ("RUNNING_UNDER_GDM"))
    display = 1024;

  if (!manager->public_connection.name)
    {
      if (!choose_xdisplay (manager, &manager->public_connection, &display, error))
        return FALSE;

      display++;
      if (!choose_xdisplay (manager, &manager->private_connection, &display, error))
        return FALSE;

      if (!prepare_auth_file (manager, error))
        return FALSE;
    }
  else
    {
      if (!open_display_sockets (manager,
                                 manager->public_connection.display_index,
                                 &manager->public_connection.abstract_fd,
                                 &manager->public_connection.unix_fd,
                                 error))
        return FALSE;

      if (!open_display_sockets (manager,
                                 manager->private_connection.display_index,
                                 &manager->private_connection.abstract_fd,
                                 &manager->private_connection.unix_fd,
                                 error))
        return FALSE;
    }

  g_message ("Using public X11 display %s, (using %s for managed services)",
             manager->public_connection.name,
             manager->private_connection.name);

  manager->compositor = compositor;
  manager->wayland_display = wl_display;
  policy = meta_context_get_x11_display_policy (context);

  if (policy == META_X11_DISPLAY_POLICY_ON_DEMAND)
    {
      manager->abstract_fd_watch_id =
        g_unix_fd_add (manager->public_connection.abstract_fd, G_IO_IN,
                       xdisplay_connection_activity_cb, manager);
      manager->unix_fd_watch_id =
        g_unix_fd_add (manager->public_connection.unix_fd, G_IO_IN,
                       xdisplay_connection_activity_cb, manager);
    }

  if (policy != META_X11_DISPLAY_POLICY_DISABLED)
    manager->prepare_shutdown_id = g_signal_connect (compositor, "prepare-shutdown",
                                                     G_CALLBACK (meta_xwayland_shutdown), 
                                                     NULL);

  /* Xwayland specific protocol, needs to be filtered out for all other clients */
  meta_xwayland_grab_keyboard_init (compositor);

  return TRUE;
}

static void
monitors_changed_cb (MetaMonitorManager  *monitor_manager,
                     MetaXWaylandManager *manager)
{
  MetaContext *context =
    meta_wayland_compositor_get_context (manager->compositor);
  MetaDisplay *display = meta_context_get_display (context);
  MetaX11Display *x11_display = display->x11_display;

  meta_xwayland_set_primary_output (x11_display);
}

static void
on_x11_display_closing (MetaDisplay         *display,
                        MetaXWaylandManager *manager)
{
  MetaX11Display *x11_display = meta_display_get_x11_display (display);
  MetaMonitorManager *monitor_manager =
    monitor_manager_from_x11_display (x11_display);

  meta_xwayland_shutdown_dnd (manager, x11_display);
  g_signal_handlers_disconnect_by_func (monitor_manager,
                                        monitors_changed_cb,
                                        manager);
}

static void
meta_xwayland_init_xrandr (MetaXWaylandManager *manager,
                           MetaX11Display      *x11_display)
{
  MetaMonitorManager *monitor_manager =
    monitor_manager_from_x11_display (x11_display);
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);

  manager->has_xrandr = XRRQueryExtension (xdisplay,
                                           &manager->rr_event_base,
                                           &manager->rr_error_base);

  if (!manager->has_xrandr)
    return;

  XRRSelectInput (xdisplay, DefaultRootWindow (xdisplay),
                  RRCrtcChangeNotifyMask | RROutputChangeNotifyMask);

  g_signal_connect (monitor_manager, "monitors-changed",
                    G_CALLBACK (monitors_changed_cb), manager);

  meta_xwayland_set_primary_output (x11_display);
}

static void
on_x11_display_setup (MetaDisplay         *display,
                      MetaXWaylandManager *manager)
{
  MetaX11Display *x11_display = meta_display_get_x11_display (display);

  meta_xwayland_init_dnd (x11_display);
  meta_xwayland_init_xrandr (manager, x11_display);
}

void
meta_xwayland_init_display (MetaXWaylandManager *manager,
                            MetaDisplay         *display)
{
  g_signal_connect (display, "x11-display-setup",
                    G_CALLBACK (on_x11_display_setup), manager);
  g_signal_connect (display, "x11-display-closing",
                    G_CALLBACK (on_x11_display_closing), manager);
}

void
meta_xwayland_setup_xdisplay (MetaXWaylandManager *manager,
                              Display             *xdisplay)
{
  /* We install an X IO error handler in addition to the child watch,
     because after Xlib connects our child watch may not be called soon
     enough, and therefore we won't crash when X exits (and most important
     we won't reset the tty).
  */
  XSetIOErrorHandler (x_io_error);
  XSetIOErrorExitHandler (xdisplay, x_io_error_exit, manager);

  XFixesSetClientDisconnectMode (xdisplay, XFixesClientDisconnectFlagTerminate);
}

static void
meta_xwayland_set_primary_output (MetaX11Display *x11_display)
{
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);
  MetaMonitorManager *monitor_manager =
    monitor_manager_from_x11_display (x11_display);
  XRRScreenResources *resources;
  MetaLogicalMonitor *primary_monitor;
  int i;

  primary_monitor =
    meta_monitor_manager_get_primary_logical_monitor (monitor_manager);

  if (!primary_monitor)
    return;

  resources = XRRGetScreenResourcesCurrent (xdisplay,
                                            DefaultRootWindow (xdisplay));
  if (!resources)
    return;

  mtk_x11_error_trap_push (x11_display->xdisplay);
  for (i = 0; i < resources->noutput; i++)
    {
      RROutput output_id = resources->outputs[i];
      XRROutputInfo *xrandr_output;
      XRRCrtcInfo *crtc_info = NULL;
      MtkRectangle crtc_geometry;

      xrandr_output = XRRGetOutputInfo (xdisplay, resources, output_id);
      if (!xrandr_output)
        continue;

      if (xrandr_output->crtc)
        crtc_info = XRRGetCrtcInfo (xdisplay, resources, xrandr_output->crtc);

      XRRFreeOutputInfo (xrandr_output);

      if (!crtc_info)
        continue;

      crtc_geometry.x = crtc_info->x;
      crtc_geometry.y = crtc_info->y;
      crtc_geometry.width = crtc_info->width;
      crtc_geometry.height = crtc_info->height;

      XRRFreeCrtcInfo (crtc_info);

      if (mtk_rectangle_equal (&crtc_geometry, &primary_monitor->rect))
        {
          XRRSetOutputPrimary (xdisplay, DefaultRootWindow (xdisplay),
                               output_id);
          break;
        }
    }
  mtk_x11_error_trap_pop (x11_display->xdisplay);

  XRRFreeScreenResources (resources);
}

gboolean
meta_xwayland_manager_handle_xevent (MetaXWaylandManager *manager,
                                     XEvent              *event)
{
  if (meta_xwayland_dnd_handle_xevent (manager, event))
    return TRUE;

  if (manager->has_xrandr && event->type == manager->rr_event_base + RRNotify)
    {
      MetaContext *context =
        meta_wayland_compositor_get_context (manager->compositor);
      MetaDisplay *display = meta_context_get_display (context);
      MetaX11Display *x11_display = meta_display_get_x11_display (display);

      meta_xwayland_set_primary_output (x11_display);
      return TRUE;
    }

  return FALSE;
}

gboolean
meta_xwayland_signal (MetaXWaylandManager  *manager,
                      int                   signum,
                      GError              **error)
{
  if (!manager->proc)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Can't send signal, Xwayland not running");
      return FALSE;
    }

  g_subprocess_send_signal (manager->proc, signum);
  return TRUE;
}

void
meta_xwayland_set_should_enable_ei_portal (MetaXWaylandManager  *manager,
                                           gboolean              should_enable_ei_portal)
{
  manager->should_enable_ei_portal = should_enable_ei_portal;
}
