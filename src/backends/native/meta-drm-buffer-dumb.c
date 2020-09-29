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

#include <xf86drm.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-private.h"

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

typedef struct
{
  MetaDrmBufferDumb *buffer_dumb;

  int out_dmabuf_fd;
} HandleToFdData;

static gpointer
handle_to_fd_in_impl (MetaKmsImpl  *impl,
                      gpointer      user_data,
                      GError      **error)
{
  HandleToFdData *data = user_data;
  MetaDrmBufferDumb *buffer_dumb = data->buffer_dumb;
  MetaDrmBuffer *buffer = META_DRM_BUFFER (buffer_dumb);
  MetaKmsDevice *device = meta_drm_buffer_get_device (buffer);
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  int fd;
  int ret;
  int dmabuf_fd;

  fd = meta_kms_impl_device_get_fd (impl_device);

  ret = drmPrimeHandleToFD (fd, buffer_dumb->handle, DRM_CLOEXEC,
                            &dmabuf_fd);
  if (ret)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmPrimeHandleToFd: %s", g_strerror (-ret));
      return GINT_TO_POINTER (FALSE);
    }

  data->out_dmabuf_fd = dmabuf_fd;

  return GINT_TO_POINTER (TRUE);
}

int
meta_drm_buffer_dumb_ensure_dmabuf_fd (MetaDrmBufferDumb  *buffer_dumb,
                                       GError            **error)
{
  MetaDrmBuffer *buffer = META_DRM_BUFFER (buffer_dumb);
  MetaKmsDevice *device = meta_drm_buffer_get_device (buffer);
  HandleToFdData data;

  if (buffer_dumb->dmabuf_fd != -1)
    return buffer_dumb->dmabuf_fd;

  data = (HandleToFdData) {
    .buffer_dumb = buffer_dumb,
  };

  if (!meta_kms_run_impl_task_sync (meta_kms_device_get_kms (device),
                                   handle_to_fd_in_impl,
                                   &data,
                                   error))
    return -1;

  buffer_dumb->dmabuf_fd = data.out_dmabuf_fd;
  return buffer_dumb->dmabuf_fd;
}

void *
meta_drm_buffer_dumb_get_data (MetaDrmBufferDumb *buffer_dumb)
{
  return buffer_dumb->map;
}

typedef struct
{
  MetaDrmBufferDumb *buffer_dumb;
  int width;
  int height;
  uint32_t format;
} InitDumbData;

static gpointer
init_dumb_buffer_in_impl (MetaKmsImpl  *impl,
                          gpointer      user_data,
                          GError      **error)
{
  InitDumbData *data = user_data;
  MetaDrmBufferDumb *buffer_dumb = data->buffer_dumb;
  MetaDrmBuffer *buffer = META_DRM_BUFFER (buffer_dumb);
  MetaKmsDevice *device = meta_drm_buffer_get_device (buffer);
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  int fd;
  struct drm_mode_create_dumb create_arg;
  struct drm_mode_destroy_dumb destroy_arg;
  struct drm_mode_map_dumb map_arg;
  void *map;
  MetaDrmFbArgs fb_args;

  fd = meta_kms_impl_device_get_fd (impl_device);

  create_arg = (struct drm_mode_create_dumb) {
    .bpp = 32, /* RGBX8888 */
    .width = data->width,
    .height = data->height
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
    .width = data->width,
    .height = data->height,
    .format = data->format,
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
  buffer_dumb->width = data->width;
  buffer_dumb->height = data->height;
  buffer_dumb->stride_bytes = create_arg.pitch;
  buffer_dumb->drm_format = data->format;

  return FALSE;

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
meta_drm_buffer_dumb_new (MetaKmsDevice  *device,
                          int             width,
                          int             height,
                          uint32_t        format,
                          GError        **error)
{
  MetaDrmBufferDumb *buffer_dumb;
  InitDumbData data;

  buffer_dumb = g_object_new (META_TYPE_DRM_BUFFER_DUMB,
                              "device", device,
                              NULL);

  data = (InitDumbData) {
    .buffer_dumb = buffer_dumb,
    .width = width,
    .height = height,
    .format = format,
  };

  if (meta_kms_run_impl_task_sync (meta_kms_device_get_kms (device),
                                   init_dumb_buffer_in_impl,
                                   &data,
                                   error))
    {
      g_object_unref (buffer_dumb);
      return NULL;
    }

  return buffer_dumb;
}

static gpointer
destroy_dumb_in_impl (MetaKmsImpl  *impl,
                      gpointer      user_data,
                      GError      **error)
{
  MetaDrmBufferDumb *buffer_dumb = user_data;
  MetaDrmBuffer *buffer = META_DRM_BUFFER (buffer_dumb);
  MetaKmsDevice *device = meta_drm_buffer_get_device (buffer);
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);
  int fd;
  struct drm_mode_destroy_dumb destroy_arg;

  fd = meta_kms_impl_device_get_fd (impl_device);

  munmap (buffer_dumb->map, buffer_dumb->map_size);

  destroy_arg = (struct drm_mode_destroy_dumb) {
    .handle = buffer_dumb->handle
  };
  drmIoctl (fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);

  if (buffer_dumb->dmabuf_fd != -1)
    close (buffer_dumb->dmabuf_fd);

  return GINT_TO_POINTER (TRUE);
}

static void
meta_drm_buffer_dumb_finalize (GObject *object)
{
  MetaDrmBufferDumb *buffer_dumb = META_DRM_BUFFER_DUMB (object);

  if (buffer_dumb->handle)
    {
      MetaDrmBuffer *buffer = META_DRM_BUFFER (buffer_dumb);
      MetaKmsDevice *device = meta_drm_buffer_get_device (buffer);

      meta_kms_run_impl_task_sync (meta_kms_device_get_kms (device),
                                   destroy_dumb_in_impl,
                                   buffer_dumb,
                                   NULL);
    }

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
