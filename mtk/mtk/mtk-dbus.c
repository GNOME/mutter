/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2025 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "mtk/mtk-dbus.h"

#include <glib/gstdio.h>
#include <stdint.h>

#define DBUS_NAME_DBUS "org.freedesktop.DBus"
#define DBUS_INTERFACE_DBUS DBUS_NAME_DBUS
#define DBUS_PATH_DBUS "/org/freedesktop/DBus"

typedef struct _MtkDbusPidfd
{
  pid_t pid;
  int pidfd;
} MtkDbusPidfd;

static MtkDbusPidfd *
mtk_dbus_pidfd_new (pid_t pid,
                    int   pidfd)
{
  MtkDbusPidfd *pfd = g_new0 (MtkDbusPidfd, 1);

  pfd->pid = pid;
  pfd->pidfd = g_steal_fd (&pidfd);

  return pfd;
}

void
mtk_dbus_pidfd_free (MtkDbusPidfd *pfd)
{
  g_clear_fd (&pfd->pidfd, NULL);
  free (pfd);
}

pid_t
mtk_dbus_pidfd_get_pid (MtkDbusPidfd *dbus_pidfd)
{
  return dbus_pidfd->pid;
}

int
mtk_dbus_pidfd_get_pidfd (MtkDbusPidfd *dbus_pidfd)
{
  return dbus_pidfd->pidfd;
}

static void
on_get_connection_pid (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  GDBusConnection *connection = G_DBUS_CONNECTION (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;
  int pid;

  reply = g_dbus_connection_call_finish (connection, res, &error);
  if (!reply)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_variant_get (reply, "(u)", &pid);

  g_task_return_pointer (task,
                         mtk_dbus_pidfd_new (pid, -1),
                         (GDestroyNotify) mtk_dbus_pidfd_free);
}

static gboolean
credentials_get_pidfd (GVariant      *credentials,
                       GUnixFDList   *fd_list,
                       MtkDbusPidfd **pfd_out)
{
  g_autoptr (GVariant) dict = NULL;
  g_autoptr (GVariant) process_id = NULL;
  uint32_t pid;
  g_autoptr (GVariant) process_fd = NULL;
  g_autofd int pidfd = -1;

  g_variant_get (credentials, "(@a{sv})", &dict);

  process_id =
    g_variant_lookup_value (dict, "ProcessID", G_VARIANT_TYPE_UINT32);

  if (!process_id)
    return FALSE;

  pid = g_variant_get_uint32 (process_id);

  process_fd =
    g_variant_lookup_value (dict, "ProcessFD", G_VARIANT_TYPE_HANDLE);
  if (process_fd)
    {
      int fd_index;

      fd_index = g_variant_get_handle (process_fd);
      g_warn_if_fail (fd_list &&
                      fd_index < g_unix_fd_list_get_length (fd_list));

      pidfd = g_unix_fd_list_get (fd_list, fd_index, NULL);
      if (pidfd < 0)
        return FALSE;
    }

  *pfd_out = mtk_dbus_pidfd_new (pid, g_steal_fd (&pidfd));
  return TRUE;
}

static void
on_get_connection_credentials (GObject      *source_object,
                               GAsyncResult *res,
                               gpointer      user_data)
{
  GDBusConnection *connection = G_DBUS_CONNECTION (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  const char *sender = g_task_get_task_data (task);
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autoptr (GError) error = NULL;
  GCancellable *cancellable;

  reply = g_dbus_connection_call_with_unix_fd_list_finish (connection,
                                                           &fd_list,
                                                           res,
                                                           &error);

  if (reply)
    {
      g_autoptr (MtkDbusPidfd) pfd = NULL;

      if (credentials_get_pidfd (reply, fd_list, &pfd))
        {
          g_task_return_pointer (task,
                                 g_steal_pointer (&pfd),
                                 (GDestroyNotify) mtk_dbus_pidfd_free);
          return;
        }
    }

  if (!g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  cancellable = g_task_get_cancellable (task);
  g_dbus_connection_call (connection,
                          DBUS_NAME_DBUS,
                          DBUS_PATH_DBUS,
                          DBUS_INTERFACE_DBUS,
                          "GetConnectionUnixProcessID",
                          g_variant_new ("(s)", sender),
                          G_VARIANT_TYPE ("(u)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          5000,
                          cancellable,
                          on_get_connection_pid,
                          g_steal_pointer (&task));
}

/**
 * mtk_dbus_pidfd_new_for_connection_async: (skip):
 */
void
mtk_dbus_pidfd_new_for_connection_async (GDBusConnection     *connection,
                                         const char          *sender,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;

  task = g_task_new (connection, cancellable, callback, user_data);
  g_task_set_task_data (task, g_strdup (sender), g_free);

  g_dbus_connection_call_with_unix_fd_list (connection,
                                            DBUS_NAME_DBUS,
                                            DBUS_PATH_DBUS,
                                            DBUS_INTERFACE_DBUS,
                                            "GetConnectionCredentials",
                                            g_variant_new ("(s)", sender),
                                            G_VARIANT_TYPE ("(a{sv})"),
                                            G_DBUS_CALL_FLAGS_NONE,
                                            5000,
                                            NULL,
                                            cancellable,
                                            on_get_connection_credentials,
                                            g_steal_pointer (&task));
}


/**
 * mtk_dbus_pidfd_new_for_connection_finish: (skip):
 */
MtkDbusPidfd *
mtk_dbus_pidfd_new_for_connection_finish (GDBusConnection  *connection,
                                          GAsyncResult     *result,
                                          GError          **error)
{
  g_return_val_if_fail (g_task_is_valid (result, connection), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}
