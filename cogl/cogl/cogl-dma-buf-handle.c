/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2020 Endless, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 */

#include "config.h"

#include "cogl/cogl-dma-buf-handle.h"

#include <errno.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct _CoglDmaBufHandle
{
  CoglFramebuffer *framebuffer;
  int width;
  int height;
  int *fds;
  uint32_t *strides;
  uint32_t *offsets;
  int bpp;
  int n_planes;
  uint32_t format;
  uint64_t modifier;
  gpointer user_data;
  GDestroyNotify destroy_func;
};

CoglDmaBufHandle *
cogl_dma_buf_handle_new (CoglFramebuffer *framebuffer,
                         int              width,
                         int              height,
                         uint32_t         format,
                         uint64_t         modifier,
                         int              n_planes,
                         int             *fds,
                         uint32_t        *strides,
                         uint32_t        *offsets,
                         int              bpp,
                         gpointer         user_data,
                         GDestroyNotify   destroy_func)
{
  CoglDmaBufHandle *dmabuf_handle;

  g_assert (framebuffer);

  dmabuf_handle = g_new0 (CoglDmaBufHandle, 1);
  dmabuf_handle->framebuffer = g_object_ref (framebuffer);
  dmabuf_handle->user_data = user_data;
  dmabuf_handle->destroy_func = destroy_func;

  dmabuf_handle->width = width;
  dmabuf_handle->height = height;
  dmabuf_handle->fds = g_memdup2 (fds, sizeof (*fds) * n_planes);
  dmabuf_handle->strides = g_memdup2 (strides, sizeof (*strides) * n_planes);
  dmabuf_handle->offsets = g_memdup2 (offsets, sizeof (*offsets) * n_planes);
  dmabuf_handle->bpp = bpp;
  dmabuf_handle->n_planes = n_planes;
  dmabuf_handle->format = format;
  dmabuf_handle->modifier = modifier;

  return dmabuf_handle;
}

void
cogl_dma_buf_handle_free (CoglDmaBufHandle *dmabuf_handle)
{
  int i;

  g_return_if_fail (dmabuf_handle != NULL);

  g_clear_object (&dmabuf_handle->framebuffer);

  if (dmabuf_handle->destroy_func)
    g_clear_pointer (&dmabuf_handle->user_data, dmabuf_handle->destroy_func);

  for (i = 0; i < dmabuf_handle->n_planes; i++)
    g_clear_fd (&dmabuf_handle->fds[i], NULL);

  g_clear_pointer (&dmabuf_handle->fds, g_free);
  g_clear_pointer (&dmabuf_handle->strides, g_free);
  g_clear_pointer (&dmabuf_handle->offsets, g_free);

  g_free (dmabuf_handle);
}

static gboolean
sync_read (CoglDmaBufHandle  *dmabuf_handle,
           uint64_t           start_or_end,
           GError           **error)
{
  struct dma_buf_sync sync = { 0 };

  g_assert (dmabuf_handle->n_planes == 1);

  sync.flags = start_or_end | DMA_BUF_SYNC_READ;

  while (TRUE)
    {
      int ret;

      ret = ioctl (dmabuf_handle->fds[0], DMA_BUF_IOCTL_SYNC, &sync);
      if (ret == -1 && errno == EINTR)
        {
          continue;
        }
      else if (ret == -1)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "ioctl: %s", g_strerror (errno));
          return FALSE;
        }
      else
        {
          break;
        }
    }

  return TRUE;
}

gboolean
cogl_dma_buf_handle_sync_read_start (CoglDmaBufHandle  *dmabuf_handle,
                                     GError           **error)
{
  return sync_read (dmabuf_handle, DMA_BUF_SYNC_START, error);
}

gboolean
cogl_dma_buf_handle_sync_read_end (CoglDmaBufHandle  *dmabuf_handle,
                                   GError           **error)
{
  return sync_read (dmabuf_handle, DMA_BUF_SYNC_END, error);
}

gpointer
cogl_dma_buf_handle_mmap (CoglDmaBufHandle  *dmabuf_handle,
                          GError           **error)
{
  size_t size;
  gpointer data;

  g_assert (dmabuf_handle->n_planes == 1);

  size = dmabuf_handle->height * dmabuf_handle->strides[0];

  data = mmap (NULL, size, PROT_READ, MAP_PRIVATE,
               dmabuf_handle->fds[0],
               dmabuf_handle->offsets[0]);
  if (data == MAP_FAILED)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "mmap failed: %s", g_strerror (errno));
      return NULL;
    }

  return data;
}

gboolean
cogl_dma_buf_handle_munmap (CoglDmaBufHandle  *dmabuf_handle,
                            gpointer           data,
                            GError           **error)
{
  size_t size;

  g_assert (dmabuf_handle->n_planes == 1);

  size = dmabuf_handle->height * dmabuf_handle->strides[0];
  if (munmap (data, size) != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "munmap failed: %s", g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

CoglFramebuffer *
cogl_dma_buf_handle_get_framebuffer (CoglDmaBufHandle *dmabuf_handle)
{
  return dmabuf_handle->framebuffer;
}

int
cogl_dma_buf_handle_get_fd (CoglDmaBufHandle *dmabuf_handle,
                            int               plane)
{
  g_return_val_if_fail (plane < dmabuf_handle->n_planes, -1);

  return dmabuf_handle->fds[plane];
}

int
cogl_dma_buf_handle_get_width (CoglDmaBufHandle *dmabuf_handle)
{
  return dmabuf_handle->width;
}

int
cogl_dma_buf_handle_get_height (CoglDmaBufHandle *dmabuf_handle)
{
  return dmabuf_handle->height;
}

int
cogl_dma_buf_handle_get_stride (CoglDmaBufHandle *dmabuf_handle,
                                int               plane)
{
  return dmabuf_handle->strides[plane];
}

int
cogl_dma_buf_handle_get_offset (CoglDmaBufHandle *dmabuf_handle,
                                int               plane)
{
  return dmabuf_handle->offsets[plane];
}

int
cogl_dma_buf_handle_get_bpp (CoglDmaBufHandle *dmabuf_handle)
{
  return dmabuf_handle->bpp;
}

int
cogl_dma_buf_handle_get_n_planes (CoglDmaBufHandle *dmabuf_handle)
{
  return dmabuf_handle->n_planes;
}

uint64_t
cogl_dma_buf_handle_get_modifier (CoglDmaBufHandle *dmabuf_handle)
{
  return dmabuf_handle->modifier;
}
