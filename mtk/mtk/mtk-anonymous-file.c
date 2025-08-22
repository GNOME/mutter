/*
 * Copyright (C) 2020 Sebastian Wick
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
 *
 * Author: Sebastian Wick <sebastian@sebastianwick.net>
 */

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mtk/mtk-anonymous-file.h"

struct _MtkAnonymousFile
{
  char *name;
  int fd;
  size_t size;
};

#define READONLY_SEALS (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)

static int
create_tmpfile_cloexec (char *tmpname)
{
  int fd;

#if defined(HAVE_MKOSTEMP)
  fd = mkostemp (tmpname, O_CLOEXEC);
  if (fd >= 0)
    unlink (tmpname);
#else
  fd = mkstemp (tmpname);
  if (fd >= 0)
    {
      long flags;

      unlink (tmpname);

      flags = fcntl (fd, F_GETFD);
      if (flags == -1 ||
          fcntl (fd, F_SETFD, flags | FD_CLOEXEC) == -1)
        {
          close (fd);
          return -1;
        }
    }
#endif

  return fd;
}

/*
 * Create a new, unique, anonymous file of the given size, and
 * return the file descriptor for it. The file descriptor is set
 * CLOEXEC. The file is immediately suitable for mmap()'ing
 * the given size at offset zero.
 *
 * The file should not have a permanent backing store like a disk,
 * but may have if XDG_RUNTIME_DIR is not properly implemented in OS.
 *
 * The file name is deleted from the file system.
 *
 * The file is suitable for buffer sharing between processes by
 * transmitting the file descriptor over Unix sockets using the
 * SCM_RIGHTS methods.
 *
 * If the C library implements posix_fallocate(), it is used to
 * guarantee that disk space is available for the file at the
 * given size. If disk space is insufficient, errno is set to ENOSPC.
 * If posix_fallocate() is not supported, program may receive
 * SIGBUS on accessing mmap()'ed file contents instead.
 *
 * If the C library implements memfd_create(), it is used to create the
 * file purely in memory, without any backing file name on the file
 * system, and then sealing off the possibility of shrinking it. This
 * can then be checked before accessing mmap()'ed file contents, to make
 * sure SIGBUS can't happen. It also avoids requiring XDG_RUNTIME_DIR.
 */
static int
create_anonymous_file (const char *name,
                       off_t       size)
{
  g_autofree char *memfd_name = NULL;
  int fd, ret;

#if defined(HAVE_MEMFD_CREATE)
  memfd_name = g_strdup_printf ("mutter-anonymous-file-%s", name);
  fd = memfd_create (memfd_name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd >= 0)
    {
      /* We can add this seal before calling posix_fallocate(), as
       * the file is currently zero-sized anyway.
       *
       * There is also no need to check for the return value, we
       * couldn't do anything with it anyway.
       */
      fcntl (fd, F_ADD_SEALS, F_SEAL_SHRINK);
    }
  else
#endif
    {
      g_autofree char *template = NULL;
      const char *path;
      g_autofree char *filename = NULL;

      path = g_get_user_runtime_dir ();
      if (!path)
        {
          errno = ENOENT;
          return -1;
        }

      template = g_strdup_printf ("mutter-anonymous-file-%s-XXXXXX", name);
      filename = g_strconcat (path, template, NULL);
      fd = create_tmpfile_cloexec (filename);

      if (fd < 0)
        return -1;
    }

  if (size == 0)
    return fd;

#if defined(HAVE_POSIX_FALLOCATE)
  do
    {
      ret = posix_fallocate (fd, 0, size);
    }
  while (ret == EINTR);

  if (ret != 0)
    {
      close (fd);
      errno = ret;
      return -1;
    }
#else
  do
    {
      ret = ftruncate (fd, size);
    }
  while (ret < 0 && errno == EINTR);

  if (ret < 0)
    {
      close (fd);
      return -1;
    }
#endif

  return fd;
}

/**
 * mtk_anonymous_file_new: (skip)
 * @name: Name of the file
 * @size: The size of @data
 * @data: The data of the file with the size @size
 *
 * Create a new anonymous read-only file of the given size and the given data
 * The intended use-case is for sending mid-sized data from the compositor
 * to clients.
 *
 * When done, free the data using mtk_anonymous_file_free().
 *
 * If this function fails errno is set.
 *
 * The name is used to as part of the file name.
 *
 * Returns: The newly created #MetaAnonymousFile, or NULL on failure. Use
 *   mtk_anonymous_file_free() to free the resources when done.
 */
MtkAnonymousFile *
mtk_anonymous_file_new (const char    *name,
                        size_t         size,
                        const uint8_t *data)
{
  g_autoptr (MtkAnonymousFile) file = NULL;

  file = g_malloc0 (sizeof *file);
  file->name = g_strdup (name);
  file->size = size;
  file->fd = create_anonymous_file (name, size);
  if (file->fd == -1)
    return NULL;

  if (size > 0)
    {
      void *map;

      map = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, file->fd, 0);
      if (map == MAP_FAILED)
        return NULL;

      memcpy (map, data, size);
      munmap (map, size);
    }

#if defined(HAVE_MEMFD_CREATE)
  /* try to put seals on the file to make it read-only so that we can
   * return the fd later directly when MAPMODE_SHARED is not set.
   * mtk_anonymous_file_open_fd can handle the fd even if it is not
   * sealed read-only and will instead create a new anonymous file on
   * each invocation.
   */
  fcntl (file->fd, F_ADD_SEALS, READONLY_SEALS);
#endif

  return g_steal_pointer (&file);
}


/**
 * mtk_anonymous_file_free: (skip)
 * @file: the #MetaAnonymousFile
 *
 * Free the resources used by an anonymous read-only file.
 */
void
mtk_anonymous_file_free (MtkAnonymousFile *file)
{
  g_clear_fd (&file->fd, NULL);
  g_free (file);
}

/**
 * mtk_anonymous_file_size: (skip)
 * @file: the #MetaAnonymousFile
 *
 * Get the size of an anonymous read-only file.
 *
 * Returns: The size of the anonymous read-only file.
 */
size_t
mtk_anonymous_file_size (const MtkAnonymousFile *file)
{
  return file->size;
}

/**
 * mtk_anonymous_file_open_fd: (skip)
 * @file: the #MetaAnonymousFile to get a file descriptor for
 * @mapmode: describes the ways in which the returned file descriptor can
 *   be used with mmap
 *
 * Returns a file descriptor for the given file, ready to be sent to a client.
 * The returned file descriptor must not be shared between multiple clients.
 * If @mapmode is %MTK_ANONYMOUS_FILE_MAPMODE_PRIVATE the file descriptor is
 * only guaranteed to be mmapable with MAP_PRIVATE. If @mapmode is
 * %MTK_ANONYMOUS_FILE_MAPMODE_SHARED the file descriptor can be mmaped with
 * either MAP_PRIVATE or MAP_SHARED.
 *
 * In case %MTK_ANONYMOUS_FILE_MAPMODE_PRIVATE is used, it is important to
 * only read the returned fd using mmap() since using read() will move the
 * read cursor of the fd and thus may cause read() calls on other returned
 * fds to fail.
 *
 * When done using the fd, it is required to call mtk_anonymous_file_close_fd()
 * instead of close().
 *
 * If this function fails errno is set.
 *
 * Returns: A file descriptor for the given file that can be sent to a client
 *   or -1 on failure. Use mtk_anonymous_file_close_fd() to release the fd
 *   when done.
 */
int
mtk_anonymous_file_open_fd (const MtkAnonymousFile  *file,
                            MtkAnonymousFileMapmode  mapmode)
{
  void *src, *dst;
  int fd;

#if defined(HAVE_MEMFD_CREATE)
  int seals;

  seals = fcntl (file->fd, F_GET_SEALS);

  /* file was sealed for read-only and we don't have to support MAP_SHARED
   * so we can simply pass the memfd fd
   */
  if (seals != -1 && mapmode == MTK_ANONYMOUS_FILE_MAPMODE_PRIVATE &&
      (seals & READONLY_SEALS) == READONLY_SEALS)
    return file->fd;
#endif

  /* for all other cases we create a new anonymous file that can be mapped
   * with MAP_SHARED and copy the contents to it and return that instead
   */
  fd = create_anonymous_file (file->name, file->size);
  if (fd == -1)
    return fd;

  if (file->size == 0)
    return fd;

  src = mmap (NULL, file->size, PROT_READ, MAP_PRIVATE, file->fd, 0);
  if (src == MAP_FAILED)
    {
      close (fd);
      return -1;
    }

  dst = mmap (NULL, file->size, PROT_WRITE, MAP_SHARED, fd, 0);
  if (dst == MAP_FAILED)
    {
      close (fd);
      munmap (src, file->size);
      return -1;
    }

  memcpy (dst, src, file->size);
  munmap (src, file->size);
  munmap (dst, file->size);

  return fd;
}

/**
 * mtk_anonymous_file_close_fd: (skip)
 * @fd: A file descriptor obtained using mtk_anonymous_file_open_fd()
 *
 * Release a file descriptor returned by mtk_anonymous_file_open_fd().
 * This function must be called for every file descriptor created with
 * mtk_anonymous_file_open_fd() to not leak any resources.
 *
 * If this function fails errno is set.
 */
void
mtk_anonymous_file_close_fd (int fd)
{
#if defined(HAVE_MEMFD_CREATE)
  int seals;

  seals = fcntl (fd, F_GET_SEALS);
  if (seals == -1 && errno != EINVAL)
    {
      g_warning ("Reading seals of anonymous file %d failed", fd);
      return;
    }

  /* The only case in which we do NOT have to close the file is when the file
   * was sealed for read-only
   */
  if (seals != -1 && (seals & READONLY_SEALS) == READONLY_SEALS)
    return;
#endif

  close (fd);
}
