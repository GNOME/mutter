/*
 * Copyright © 2024 GNOME Foundation Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Julian Sparber <jsparber@gnome.org>
 */

#include "config.h"

#include "core/meta-sealed-fd.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define REQUIRED_SEALS (F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SHRINK)

struct _MetaSealedFd
{
  GObject parent_instance;

  int fd;
};

G_DEFINE_FINAL_TYPE (MetaSealedFd, meta_sealed_fd, G_TYPE_OBJECT)

static void
meta_sealed_fd_finalize (GObject *object)
{
  MetaSealedFd *sealed_fd = META_SEALED_FD (object);
  g_autoptr (GError) error = NULL;

  if (!g_clear_fd (&sealed_fd->fd, &error))
    g_warning ("Error closing sealed fd: %s", error->message);

  G_OBJECT_CLASS (meta_sealed_fd_parent_class)->finalize (object);
}

static void
meta_sealed_fd_class_init (MetaSealedFdClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_sealed_fd_finalize;
}

static void
meta_sealed_fd_init (MetaSealedFd *sealed_fd)
{
  sealed_fd->fd = -1;
}

MetaSealedFd *
meta_sealed_fd_new_take_memfd (int      memfd,
                               GError **error)
{
  g_autoptr (MetaSealedFd) sealed_fd = NULL;
  g_autofd int fd = g_steal_fd (&memfd);
  int saved_errno = -1;
  int seals;

  g_return_val_if_fail (fd != -1, NULL);

  seals = fcntl (fd, F_GET_SEALS);
  if (seals == -1)
   {
      saved_errno = errno;

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (saved_errno),
                   "fcntl F_GET_SEALS: %s", g_strerror (saved_errno));
      return NULL;
    }

  /* If the seal seal is set and some required seal is missing report EPERM error directly */
  if ((seals & F_SEAL_SEAL) && (seals & REQUIRED_SEALS) != REQUIRED_SEALS)
    saved_errno = EPERM;
  else if (fcntl (fd, F_ADD_SEALS, REQUIRED_SEALS) == -1)
    saved_errno = errno;

  if (saved_errno != -1)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (saved_errno),
                   "fcntl F_ADD_SEALS: %s", g_strerror (saved_errno));
      return NULL;
    }

  sealed_fd = g_object_new (META_TYPE_SEALED_FD, NULL);
  sealed_fd->fd = g_steal_fd (&fd);

  return g_steal_pointer (&sealed_fd);
}

MetaSealedFd *
meta_sealed_fd_new_from_handle (GVariant     *handle,
                                GUnixFDList  *fd_list,
                                GError      **error)
{
  g_autofd int fd = -1;
  int fd_id;

  if (!g_variant_is_of_type (handle, G_VARIANT_TYPE_HANDLE))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "GVariant is not a file descriptor handle");
      return NULL;
    }

  if (!fd_list)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "Invalid file descriptor: index not found (empty list)");
      return NULL;
    }

  fd_id = g_variant_get_handle (handle);
  if (fd_id >= g_unix_fd_list_get_length (fd_list))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "Invalid file descriptor: index not found");
      return NULL;
    }

  fd = g_unix_fd_list_get (fd_list, fd_id, error);
  if (fd == -1)
    return NULL;

  return meta_sealed_fd_new_take_memfd (g_steal_fd (&fd), error);
}

int
meta_sealed_fd_get_fd (MetaSealedFd *sealed_fd)
{
  g_return_val_if_fail (META_IS_SEALED_FD (sealed_fd), -1);

  return sealed_fd->fd;
}

int
meta_sealed_fd_dup_fd (MetaSealedFd *sealed_fd)
{
  g_return_val_if_fail (META_IS_SEALED_FD (sealed_fd), -1);

  return dup (sealed_fd->fd);
}

GBytes *
meta_sealed_fd_get_bytes (MetaSealedFd  *sealed_fd,
                          GError       **error)
{
  g_autoptr (GMappedFile) mapped = NULL;

  mapped = g_mapped_file_new_from_fd (sealed_fd->fd, FALSE, error);
  return g_mapped_file_get_bytes (mapped);
}
