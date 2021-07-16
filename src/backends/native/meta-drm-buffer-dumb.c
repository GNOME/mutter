/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
 * Copyright (C) 2018 Canonical Ltd.
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
 *
 */

#include "config.h"

#include "backends/native/meta-drm-buffer-dumb.h"

#include <gio/gio.h>
#include <xf86drm.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "backends/native/meta-device-pool.h"

struct _MetaDrmBufferDumb
{
  MetaDrmBuffer parent;

  uint32_t handle;
  void *map;
  uint64_t map_size;
  int width;
  int height;
  int stride_bytes;
  uint32_t drm_format;
  int dmabuf_fd;
};

G_DEFINE_TYPE (MetaDrmBufferDumb, meta_drm_buffer_dumb, META_TYPE_DRM_BUFFER)

static int
meta_drm_buffer_dumb_get_width (MetaDrmBuffer *buffer)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (buffer);

  return buffer_dumb->width;
}

static int
meta_drm_buffer_dumb_get_height (MetaDrmBuffer *buffer)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (buffer);

  return buffer_dumb->height;
}

static int
meta_drm_buffer_dumb_get_stride (MetaDrmBuffer *buffer)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (buffer);

  return buffer_dumb->stride_bytes;
}

static uint32_t
meta_drm_buffer_dumb_get_format (MetaDrmBuffer *buffer)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (buffer);

  return buffer_dumb->drm_format;
}

static int
handle_to_dmabuf_fd (MetaDrmBufferDumb  *buffer_dumb,
                     GError            **error)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (buffer_dumb);
  MetaDeviceFile *device_file;
  int fd;
  int ret;
  int dmabuf_fd;

  device_file = meta_drm_buffer_get_device_file (buffer);
  fd = meta_device_file_get_fd (device_file);

  ret = drmPrimeHandleToFD (fd, buffer_dumb->handle, DRM_CLOEXEC,
                            &dmabuf_fd);
  if (ret)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmPrimeHandleToFd: %s", g_strerror (-ret));
      return -1;
    }

  return dmabuf_fd;
}

int
meta_drm_buffer_dumb_ensure_dmabuf_fd (MetaDrmBufferDumb  *buffer_dumb,
                                       GError            **error)
{
  if (buffer_dumb->dmabuf_fd != -1)
    return buffer_dumb->dmabuf_fd;

  buffer_dumb->dmabuf_fd = handle_to_dmabuf_fd (buffer_dumb, error);
  return buffer_dumb->dmabuf_fd;
}

void *
meta_drm_buffer_dumb_get_data (MetaDrmBufferDumb *buffer_dumb)
{
  return buffer_dumb->map;
}

static gboolean
init_dumb_buffer (MetaDrmBufferDumb  *buffer_dumb,
                  int                 width,
                  int                 height,
                  uint32_t            format,
                  GError            **error)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (buffer_dumb);
  MetaDeviceFile *device_file;
  int fd;
  struct drm_mode_create_dumb create_arg;
  struct drm_mode_destroy_dumb destroy_arg;
  struct drm_mode_map_dumb map_arg;
  void *map;
  MetaDrmFbArgs fb_args;

  device_file = meta_drm_buffer_get_device_file (buffer);
  fd = meta_device_file_get_fd (device_file);

  create_arg = (struct drm_mode_create_dumb) {
    .bpp = 32, /* RGBX8888 */
    .width = width,
    .height = height
  };
  if (drmIoctl (fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) != 0)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to create dumb drm buffer: %s",
                   g_strerror (errno));
      goto err_ioctl;
    }

  fb_args = (MetaDrmFbArgs) {
    .width = width,
    .height = height,
    .format = format,
    .handles = { create_arg.handle },
    .strides = { create_arg.pitch },
  };
  if (!meta_drm_buffer_ensure_fb_id (buffer, FALSE, &fb_args, error))
    goto err_add_fb;

  map_arg = (struct drm_mode_map_dumb) {
    .handle = create_arg.handle
  };
  if (drmIoctl (fd, DRM_IOCTL_MODE_MAP_DUMB,
                &map_arg) != 0)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to map dumb drm buffer: %s",
                   g_strerror (errno));
      goto err_map_dumb;
    }

  map = mmap (NULL, create_arg.size, PROT_WRITE, MAP_SHARED,
              fd, map_arg.offset);
  if (map == MAP_FAILED)
    {
      g_set_error (error, G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Failed to mmap dumb drm buffer memory: %s",
                   g_strerror (errno));
      goto err_mmap;
    }

  buffer_dumb->handle = create_arg.handle;
  buffer_dumb->map = map;
  buffer_dumb->map_size = create_arg.size;
  buffer_dumb->width = width;
  buffer_dumb->height = height;
  buffer_dumb->stride_bytes = create_arg.pitch;
  buffer_dumb->drm_format = format;

  return TRUE;

err_mmap:
err_map_dumb:
err_add_fb:
  destroy_arg = (struct drm_mode_destroy_dumb) {
    .handle = create_arg.handle
  };
  drmIoctl (fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);

err_ioctl:
  return FALSE;
}

MetaDrmBufferDumb *
meta_drm_buffer_dumb_new (MetaDeviceFile  *device_file,
                          int              width,
                          int              height,
                          uint32_t         format,
                          GError         **error)
{
  MetaDrmBufferDumb *buffer_dumb;

  buffer_dumb = g_object_new (META_TYPE_DRM_BUFFER_DUMB,
                              "device-file", device_file,
                              NULL);

  if (!init_dumb_buffer (buffer_dumb, width, height, format, error))
    {
      g_object_unref (buffer_dumb);
      return NULL;
    }

  return buffer_dumb;
}

static void
destroy_dumb_buffer (MetaDrmBufferDumb *buffer_dumb)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (buffer_dumb);
  MetaDeviceFile *device_file;
  int fd;
  struct drm_mode_destroy_dumb destroy_arg;

  device_file = meta_drm_buffer_get_device_file (buffer);
  fd = meta_device_file_get_fd (device_file);

  munmap (buffer_dumb->map, buffer_dumb->map_size);

  destroy_arg = (struct drm_mode_destroy_dumb) {
    .handle = buffer_dumb->handle
  };
  drmIoctl (fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);

  if (buffer_dumb->dmabuf_fd != -1)
    close (buffer_dumb->dmabuf_fd);
}

static void
meta_drm_buffer_dumb_finalize (GObject *object)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (object);

  if (buffer_dumb->handle)
    destroy_dumb_buffer (buffer_dumb);

  G_OBJECT_CLASS (meta_drm_buffer_dumb_parent_class)->finalize (object);
}

static void
meta_drm_buffer_dumb_init (MetaDrmBufferDumb *buffer_dumb)
{
  buffer_dumb->dmabuf_fd = -1;
}

static void
meta_drm_buffer_dumb_class_init (MetaDrmBufferDumbClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaDrmBufferClass *buffer_class = META_DRM_BUFFER_CLASS (klass);

  object_class->finalize = meta_drm_buffer_dumb_finalize;

  buffer_class->get_width = meta_drm_buffer_dumb_get_width;
  buffer_class->get_height = meta_drm_buffer_dumb_get_height;
  buffer_class->get_stride = meta_drm_buffer_dumb_get_stride;
  buffer_class->get_format = meta_drm_buffer_dumb_get_format;
}
