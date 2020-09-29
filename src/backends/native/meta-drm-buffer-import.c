/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016,2017 Red Hat
 * Copyright (C) 2018,2019 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-drm-buffer-import.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <xf86drm.h>

#include "backends/native/meta-drm-buffer-gbm.h"
#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-renderer-native.h"

struct _MetaDrmBufferImport
{
  MetaDrmBuffer parent;

  MetaDrmBufferGbm *importee;
};

G_DEFINE_TYPE (MetaDrmBufferImport, meta_drm_buffer_import,
               META_TYPE_DRM_BUFFER)

static int
meta_drm_buffer_import_get_width (MetaDrmBuffer *buffer)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);

  return meta_drm_buffer_get_width (META_DRM_BUFFER (buffer_import->importee));
}

static int
meta_drm_buffer_import_get_height (MetaDrmBuffer *buffer)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);

  return meta_drm_buffer_get_height (META_DRM_BUFFER (buffer_import->importee));
}

static int
meta_drm_buffer_import_get_stride (MetaDrmBuffer *buffer)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);

  return meta_drm_buffer_get_stride (META_DRM_BUFFER (buffer_import->importee));
}

static uint32_t
meta_drm_buffer_import_get_format (MetaDrmBuffer *buffer)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);

  return meta_drm_buffer_get_format (META_DRM_BUFFER (buffer_import->importee));
}

static struct gbm_bo *
dmabuf_to_gbm_bo (struct gbm_device *importer,
                  int                dmabuf_fd,
                  uint32_t           width,
                  uint32_t           height,
                  uint32_t           stride,
                  uint32_t           format)
{
  struct gbm_import_fd_data data = {
    .fd = dmabuf_fd,
    .width = width,
    .height = height,
    .stride = stride,
    .format = format
  };

  return gbm_bo_import (importer,
                        GBM_BO_IMPORT_FD,
                        &data,
                        GBM_BO_USE_SCANOUT);
}

static gboolean
import_gbm_buffer (MetaDrmBufferImport  *buffer_import,
                   struct gbm_device    *importer,
                   GError              **error)
{
  MetaDrmFbArgs fb_args = { 0, };
  struct gbm_bo *primary_bo;
  struct gbm_bo *imported_bo;
  int dmabuf_fd;
  gboolean ret;

  primary_bo = meta_drm_buffer_gbm_get_bo (buffer_import->importee);

  dmabuf_fd = gbm_bo_get_fd (primary_bo);
  if (dmabuf_fd == -1)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "getting dmabuf fd failed");
      return FALSE;
    }

  fb_args.strides[0] = gbm_bo_get_stride (primary_bo);
  fb_args.width = gbm_bo_get_width (primary_bo);
  fb_args.height = gbm_bo_get_height (primary_bo);
  fb_args.format = gbm_bo_get_format (primary_bo);

  imported_bo = dmabuf_to_gbm_bo (importer,
                                  dmabuf_fd,
                                  fb_args.width,
                                  fb_args.height,
                                  fb_args.strides[0],
                                  fb_args.format);
  if (!imported_bo)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "importing dmabuf fd failed");
      ret = FALSE;
      goto out_close;
    }

  fb_args.handles[0] = gbm_bo_get_handle (imported_bo).u32;

  ret = meta_drm_buffer_ensure_fb_id (META_DRM_BUFFER (buffer_import),
                                      FALSE /* use_modifiers */,
                                      &fb_args,
                                      error);

  gbm_bo_destroy (imported_bo);

out_close:
  close (dmabuf_fd);

  return ret;
}

MetaDrmBufferImport *
meta_drm_buffer_import_new (MetaKmsDevice      *device,
                            struct gbm_device  *gbm_device,
                            MetaDrmBufferGbm   *buffer_gbm,
                            GError            **error)
{
  MetaDrmBufferImport *buffer_import;

  buffer_import = g_object_new (META_TYPE_DRM_BUFFER_IMPORT,
                                "device", device,
                                NULL);
  g_set_object (&buffer_import->importee, buffer_gbm);

  if (!import_gbm_buffer (buffer_import, gbm_device, error))
    {
      g_object_unref (buffer_import);
      return NULL;
    }

  return buffer_import;
}

static void
meta_drm_buffer_import_finalize (GObject *object)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (object);

  g_clear_object (&buffer_import->importee);

  G_OBJECT_CLASS (meta_drm_buffer_import_parent_class)->finalize (object);
}

static void
meta_drm_buffer_import_init (MetaDrmBufferImport *buffer_import)
{
}

static void
meta_drm_buffer_import_class_init (MetaDrmBufferImportClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  MetaDrmBufferClass *buffer_class = META_DRM_BUFFER_CLASS (klass);

  object_class->finalize = meta_drm_buffer_import_finalize;

  buffer_class->get_width = meta_drm_buffer_import_get_width;
  buffer_class->get_height = meta_drm_buffer_import_get_height;
  buffer_class->get_stride = meta_drm_buffer_import_get_stride;
  buffer_class->get_format = meta_drm_buffer_import_get_format;
}
