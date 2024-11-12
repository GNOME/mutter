/*
 * Copyright (C) 2013-2021 Red Hat, Inc.
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

#include "backends/native/meta-device-pool-private.h"

#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include "backends/meta-launcher.h"
#include "backends/native/meta-backend-native.h"
#include "meta/meta-backend.h"
#include "meta/util.h"

#include "meta-dbus-login1.h"

struct _MetaDeviceFile
{
  MetaDevicePool *pool;

  grefcount ref_count;

  char *path;
  int major;
  int minor;
  int fd;
  MetaDeviceFileFlags flags;
  uint32_t tags[META_DEVICE_FILE_N_TAGS];
};

struct _MetaDevicePool
{
  GObject parent;

  MetaBackend *backend;

  MetaDBusLogin1Session *session_proxy;

  GMutex mutex;

  GList *files;
};

G_DEFINE_TYPE (MetaDevicePool, meta_device_pool, G_TYPE_OBJECT)

static void
release_device_file (MetaDevicePool *pool,
                     MetaDeviceFile *file);

static MetaDeviceFile *
meta_device_file_new (MetaDevicePool      *pool,
                      const char          *path,
                      int                  major,
                      int                  minor,
                      int                  fd,
                      MetaDeviceFileFlags  flags)
{
  MetaDeviceFile *file;

  file = g_new0 (MetaDeviceFile, 1);

  file->pool = pool;
  g_ref_count_init (&file->ref_count);

  file->path = g_strdup (path);
  file->major = major;
  file->minor = minor;
  file->fd = fd;
  file->flags = flags;

  return file;
}

static void
meta_device_file_free (MetaDeviceFile *file)
{
  g_free (file->path);
  g_free (file);
}

int
meta_device_file_get_fd (MetaDeviceFile *device_file)
{
  g_assert (!g_ref_count_compare (&device_file->ref_count, 0));

  return device_file->fd;
}

const char *
meta_device_file_get_path (MetaDeviceFile *device_file)
{
  return device_file->path;
}

void
meta_device_file_tag (MetaDeviceFile     *device_file,
                      MetaDeviceFileTags  tag,
                      uint32_t            value)
{
  device_file->tags[tag] |= value;
}

uint32_t
meta_device_file_has_tag (MetaDeviceFile     *device_file,
                          MetaDeviceFileTags  tag,
                          uint32_t            value)
{
  return (device_file->tags[tag] & value) == value;
}

static MetaDeviceFile *
meta_device_file_acquire_locked (MetaDeviceFile *file)
{
  g_ref_count_inc (&file->ref_count);
  return file;
}

MetaDeviceFile *
meta_device_file_acquire (MetaDeviceFile *file)
{
  g_mutex_lock (&file->pool->mutex);
  meta_topic (META_DEBUG_BACKEND, "Acquiring device file '%s'", file->path);
  meta_device_file_acquire_locked (file);
  g_mutex_unlock (&file->pool->mutex);

  return file;
}

void
meta_device_file_release (MetaDeviceFile *file)
{
  g_warn_if_fail (file->fd != -1);

  release_device_file (file->pool, file);
}

MetaDevicePool *
meta_device_file_get_pool (MetaDeviceFile *device_file)
{
  return device_file->pool;
}

static MetaDeviceFile *
find_device_file_from_path (MetaDevicePool *pool,
                            const char     *path)
{
  GList *l;

  for (l = pool->files; l; l = l->next)
    {
      MetaDeviceFile *file = l->data;

      if (g_strcmp0 (file->path, path) == 0)
        return file;
    }

  return NULL;
}

static gboolean
take_device (MetaDBusLogin1Session  *session_proxy,
             int                     dev_major,
             int                     dev_minor,
             int                    *out_fd,
             GCancellable           *cancellable,
             GError                **error)
{
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  int fd = -1;

  if (!meta_dbus_login1_session_call_take_device_sync (session_proxy,
                                                       dev_major,
                                                       dev_minor,
                                                       NULL,
                                                       &fd_variant,
                                                       NULL, /* paused */
                                                       &fd_list,
                                                       cancellable,
                                                       error))
    return FALSE;

  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_variant), error);
  if (fd == -1)
    return FALSE;

  *out_fd = fd;
  return TRUE;
}

static gboolean
get_device_info_from_path (const char *path,
                           int        *out_major,
                           int        *out_minor)
{
  int ret;
  struct stat st;

  ret = stat (path, &st);
  if (ret < 0 || !S_ISCHR (st.st_mode))
    return FALSE;

  *out_major = major (st.st_rdev);
  *out_minor = minor (st.st_rdev);
  return TRUE;
}

MetaDeviceFile *
meta_device_pool_open (MetaDevicePool       *pool,
                       const char           *path,
                       MetaDeviceFileFlags   flags,
                       GError              **error)
{
  g_autoptr (GMutexLocker) locker = NULL;
  MetaDeviceFile *file;
  int major = -1, minor = -1;
  int fd;

  locker = g_mutex_locker_new (&pool->mutex);

  file = find_device_file_from_path (pool, path);
  if (file)
    {
      g_warn_if_fail (file->flags == flags);
      meta_device_file_acquire_locked (file);
      return file;
    }

  if (flags & META_DEVICE_FILE_FLAG_TAKE_CONTROL)
    {
      meta_topic (META_DEBUG_BACKEND,
                  "Opening and taking control of device file '%s'",
                  path);

      if (!pool->session_proxy)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                       "Can't take control without logind session");
          return NULL;
        }

      if (!get_device_info_from_path (path, &major, &minor))
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_NOT_FOUND,
                       "Could not get device info for path %s: %m", path);
          return NULL;
        }

      if (!take_device (pool->session_proxy, major, minor, &fd, NULL, error))
        return NULL;
    }
  else
    {
      int open_flags;

      meta_topic (META_DEBUG_BACKEND,
                  "Opening device file '%s'",
                  path);

      if (flags & META_DEVICE_FILE_FLAG_READ_ONLY)
        open_flags = O_RDONLY;
      else
        open_flags = O_RDWR;
      open_flags |= O_CLOEXEC;

      do
        {
          fd = open (path, open_flags);
        }
      while (fd == -1 && errno == EINTR);

      if (fd == -1)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to open device '%s': %s",
                       path, g_strerror (errno));
          return NULL;
        }
    }

  file = meta_device_file_new (pool, path, major, minor, fd, flags);
  pool->files = g_list_prepend (pool->files, file);

  return file;
}

static void
release_device_file (MetaDevicePool *pool,
                     MetaDeviceFile *file)
{
  g_autoptr (GMutexLocker) locker = NULL;
  g_autoptr (GError) error = NULL;

  locker = g_mutex_locker_new (&pool->mutex);

  meta_topic (META_DEBUG_BACKEND, "Releasing device file '%s'", file->path);

  if (!g_ref_count_dec (&file->ref_count))
    return;

  pool->files = g_list_remove (pool->files, file);

  if (file->flags & META_DEVICE_FILE_FLAG_TAKE_CONTROL)
    {
      MetaDBusLogin1Session *session_proxy;

      meta_topic (META_DEBUG_BACKEND,
                  "Releasing control of and closing device file '%s'",
                  file->path);

      session_proxy = pool->session_proxy;
      if (!meta_dbus_login1_session_call_release_device_sync (session_proxy,
                                                              file->major,
                                                              file->minor,
                                                              NULL, &error))
        {
          g_warning ("Could not release device '%s' (%d,%d): %s",
                     file->path,
                     file->major, file->minor,
                     error->message);
        }
    }
  else
    {
      meta_topic (META_DEBUG_BACKEND,
                  "Closing device file '%s'",
                  file->path);
    }

  close (file->fd);

  meta_device_file_free (file);
}

MetaDevicePool *
meta_device_pool_new (MetaBackendNative *backend_native)
{
  MetaDevicePool *pool;
  MetaLauncher *launcher;

  pool = g_object_new (META_TYPE_DEVICE_POOL, NULL);

  pool->backend = META_BACKEND (backend_native);

  launcher = meta_backend_get_launcher (pool->backend);
  if (launcher)
    pool->session_proxy = meta_launcher_get_session_proxy (launcher);

  return pool;
}

static void
meta_device_pool_finalize (GObject *object)
{
  MetaDevicePool *pool = META_DEVICE_POOL (object);

  g_mutex_clear (&pool->mutex);
  g_warn_if_fail (!pool->files);

  G_OBJECT_CLASS (meta_device_pool_parent_class)->finalize (object);
}

static void
meta_device_pool_init (MetaDevicePool *pool)
{
  g_mutex_init (&pool->mutex);
}

static void
meta_device_pool_class_init (MetaDevicePoolClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_device_pool_finalize;
}

MetaBackend *
meta_device_pool_get_backend (MetaDevicePool *pool)
{
  return pool->backend;
}
