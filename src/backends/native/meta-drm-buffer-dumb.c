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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/native/meta-drm-buffer-dumb.h"

#include <drm_fourcc.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
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
  int offset;
};

G_DEFINE_TYPE (MetaDrmBufferDumb, meta_drm_buffer_dumb, META_TYPE_DRM_BUFFER)

static int
meta_drm_buffer_dumb_export_fd (MetaDrmBuffer  *buffer,
                                GError        **error)
{
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Can't export fd for dumb buffer");
  return -1;
}

static int
meta_drm_buffer_dumb_export_fd_for_plane (MetaDrmBuffer  *buffer,
                                          int             plane,
                                          GError        **error)
{
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Can't export fd for dumb buffer");
  return -1;
}

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
meta_drm_buffer_dumb_get_n_planes (MetaDrmBuffer *buffer)
{
  return 1;
}

static int
meta_drm_buffer_dumb_get_stride (MetaDrmBuffer *buffer)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (buffer);

  return buffer_dumb->stride_bytes;
}

static int
meta_drm_buffer_dumb_get_stride_for_plane (MetaDrmBuffer *buffer,
                                           int            plane)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (buffer);

  g_warn_if_fail (plane == 0);

  return buffer_dumb->stride_bytes;
}

static uint32_t
meta_drm_buffer_dumb_get_format (MetaDrmBuffer *buffer)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (buffer);

  return buffer_dumb->drm_format;
}

static int
meta_drm_buffer_dumb_get_bpp (MetaDrmBuffer *buffer)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (buffer);

  switch (buffer_dumb->drm_format)
    {
    case DRM_FORMAT_C8:
    case DRM_FORMAT_R8:
    case DRM_FORMAT_RGB332:
    case DRM_FORMAT_BGR233:
      return 8;
    case DRM_FORMAT_GR88:
    case DRM_FORMAT_XRGB4444:
    case DRM_FORMAT_XBGR4444:
    case DRM_FORMAT_RGBX4444:
    case DRM_FORMAT_BGRX4444:
    case DRM_FORMAT_ARGB4444:
    case DRM_FORMAT_ABGR4444:
    case DRM_FORMAT_RGBA4444:
    case DRM_FORMAT_BGRA4444:
    case DRM_FORMAT_XRGB1555:
    case DRM_FORMAT_XBGR1555:
    case DRM_FORMAT_RGBX5551:
    case DRM_FORMAT_BGRX5551:
    case DRM_FORMAT_ARGB1555:
    case DRM_FORMAT_ABGR1555:
    case DRM_FORMAT_RGBA5551:
    case DRM_FORMAT_BGRA5551:
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_BGR565:
      return 16;
    case DRM_FORMAT_RGB888:
    case DRM_FORMAT_BGR888:
      return 24;
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_RGBX1010102:
    case DRM_FORMAT_BGRX1010102:
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_RGBA1010102:
    case DRM_FORMAT_BGRA1010102:
      return 32;
    case DRM_FORMAT_XBGR16161616F:
    case DRM_FORMAT_ABGR16161616F:
      return 64;
    default:
      g_warn_if_reached ();
      return 0;
    }
}

static int
meta_drm_buffer_dumb_get_offset_for_plane (MetaDrmBuffer *buffer,
                                           int            plane)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (buffer);

  g_warn_if_fail (plane == 0);

  return buffer_dumb->offset;
}

static uint64_t
meta_drm_buffer_dumb_get_modifier (MetaDrmBuffer *buffer)
{
  return DRM_FORMAT_MOD_LINEAR;
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
    .handle = create_arg.handle,
    .handles = { create_arg.handle },
    .strides = { create_arg.pitch },
  };
  if (!meta_drm_buffer_do_ensure_fb_id (buffer, &fb_args, error))
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
  buffer_dumb->offset = map_arg.offset;

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
                              "flags", META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS,
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

  g_clear_fd (&buffer_dumb->dmabuf_fd, NULL);
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

  buffer_class->export_fd = meta_drm_buffer_dumb_export_fd;
  buffer_class->export_fd_for_plane = meta_drm_buffer_dumb_export_fd_for_plane;
  buffer_class->get_width = meta_drm_buffer_dumb_get_width;
  buffer_class->get_height = meta_drm_buffer_dumb_get_height;
  buffer_class->get_n_planes = meta_drm_buffer_dumb_get_n_planes;
  buffer_class->get_stride = meta_drm_buffer_dumb_get_stride;
  buffer_class->get_stride_for_plane = meta_drm_buffer_dumb_get_stride_for_plane;
  buffer_class->get_bpp = meta_drm_buffer_dumb_get_bpp;
  buffer_class->get_format = meta_drm_buffer_dumb_get_format;
  buffer_class->get_offset_for_plane = meta_drm_buffer_dumb_get_offset_for_plane;
  buffer_class->get_modifier = meta_drm_buffer_dumb_get_modifier;
}
