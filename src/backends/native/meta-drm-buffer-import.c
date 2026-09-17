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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
meta_drm_buffer_import_export_fd (MetaDrmBuffer  *buffer,
                                  GError        **error)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);

  return meta_drm_buffer_export_fd (META_DRM_BUFFER (buffer_import->importee),
                                    error);
}

static int
meta_drm_buffer_import_export_fd_for_plane (MetaDrmBuffer  *buffer,
                                            int             plane,
                                            GError        **error)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);
  MetaDrmBuffer *importee = META_DRM_BUFFER (buffer_import->importee);

  return meta_drm_buffer_export_fd_for_plane (importee, plane, error);
}

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
meta_drm_buffer_import_get_n_planes (MetaDrmBuffer *buffer)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);
  MetaDrmBuffer *importee = META_DRM_BUFFER (buffer_import->importee);

  return meta_drm_buffer_get_n_planes (importee);
}

static int
meta_drm_buffer_import_get_stride (MetaDrmBuffer *buffer)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);

  return meta_drm_buffer_get_stride (META_DRM_BUFFER (buffer_import->importee));
}

static int
meta_drm_buffer_import_get_stride_for_plane (MetaDrmBuffer *buffer,
                                             int            plane)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);
  MetaDrmBuffer *importee = META_DRM_BUFFER (buffer_import->importee);

  return meta_drm_buffer_get_stride_for_plane (importee, plane);
}

static int
meta_drm_buffer_import_get_bpp (MetaDrmBuffer *buffer)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);

  return meta_drm_buffer_get_bpp (META_DRM_BUFFER (buffer_import->importee));
}

static uint32_t
meta_drm_buffer_import_get_format (MetaDrmBuffer *buffer)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);

  return meta_drm_buffer_get_format (META_DRM_BUFFER (buffer_import->importee));
}

static int
meta_drm_buffer_import_get_offset_for_plane (MetaDrmBuffer *buffer,
                                             int            plane)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);
  MetaDrmBuffer *importee = META_DRM_BUFFER (buffer_import->importee);

  return meta_drm_buffer_get_offset_for_plane (importee, plane);
}

static uint64_t
meta_drm_buffer_import_get_modifier (MetaDrmBuffer *buffer)
{
  MetaDrmBufferImport *buffer_import = META_DRM_BUFFER_IMPORT (buffer);
  MetaDrmBuffer *importee = META_DRM_BUFFER (buffer_import->importee);

  return meta_drm_buffer_get_modifier (importee);
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
  int dmabuf_fds[GBM_MAX_PLANES] = { -1, -1, -1, -1 };
  int n_planes;
  int i;
  gboolean use_modifiers;
  gboolean ret;

  primary_bo = meta_drm_buffer_gbm_get_bo (buffer_import->importee);
  n_planes = gbm_bo_get_plane_count (primary_bo);
  if (n_planes < 1 || n_planes > GBM_MAX_PLANES)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Unsupported DMA buffer plane count");
      return FALSE;
    }
  use_modifiers = meta_drm_buffer_uses_explicit_modifiers (
    META_DRM_BUFFER (buffer_import->importee));
  if (!use_modifiers && n_planes != 1)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Implicit DMA buffer has multiple planes");
      return FALSE;
    }

  fb_args.width = gbm_bo_get_width (primary_bo);
  fb_args.height = gbm_bo_get_height (primary_bo);
  fb_args.format = gbm_bo_get_format (primary_bo);
  for (i = 0; i < n_planes; i++)
    {
      dmabuf_fds[i] = use_modifiers ?
        gbm_bo_get_fd_for_plane (primary_bo, i) :
        gbm_bo_get_fd (primary_bo);
      if (dmabuf_fds[i] < 0)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       "getting DMA buffer plane %d fd failed",
                       i);
          ret = FALSE;
          goto out_close;
        }

      fb_args.strides[i] = gbm_bo_get_stride_for_plane (primary_bo, i);
      fb_args.offsets[i] = gbm_bo_get_offset (primary_bo, i);
      fb_args.modifiers[i] = gbm_bo_get_modifier (primary_bo);
    }

  if (use_modifiers)
    {
      struct gbm_import_fd_modifier_data data = {
        .width = fb_args.width,
        .height = fb_args.height,
        .format = fb_args.format,
        .num_fds = n_planes,
        .modifier = fb_args.modifiers[0],
      };

      for (i = 0; i < n_planes; i++)
        {
          data.fds[i] = dmabuf_fds[i];
          data.strides[i] = fb_args.strides[i];
          data.offsets[i] = fb_args.offsets[i];
        }

      imported_bo = gbm_bo_import (importer,
                                   GBM_BO_IMPORT_FD_MODIFIER,
                                   &data,
                                   GBM_BO_USE_SCANOUT);
    }
  else
    {
      imported_bo = dmabuf_to_gbm_bo (importer,
                                      dmabuf_fds[0],
                                      fb_args.width,
                                      fb_args.height,
                                      fb_args.strides[0],
                                      fb_args.format);
    }
  if (!imported_bo)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "importing dmabuf fd failed");
      ret = FALSE;
      goto out_close;
    }

  for (i = 0; i < n_planes; i++)
    fb_args.handles[i] = use_modifiers ?
      gbm_bo_get_handle_for_plane (imported_bo, i).u32 :
      gbm_bo_get_handle (imported_bo).u32;
  fb_args.handle = fb_args.handles[0];

  ret = meta_drm_buffer_do_ensure_fb_id (META_DRM_BUFFER (buffer_import),
                                         &fb_args,
                                         error);

  gbm_bo_destroy (imported_bo);

out_close:
  for (i = 0; i < n_planes; i++)
    {
      if (dmabuf_fds[i] >= 0)
        close (dmabuf_fds[i]);
    }

  return ret;
}

MetaDrmBufferImport *
meta_drm_buffer_import_new (MetaDeviceFile     *device_file,
                            struct gbm_device  *gbm_device,
                            MetaDrmBufferGbm   *buffer_gbm,
                            GError            **error)
{
  MetaDrmBufferImport *buffer_import;

  buffer_import = g_object_new (META_TYPE_DRM_BUFFER_IMPORT,
                                "device-file", device_file,
                                "flags",
                                meta_drm_buffer_uses_explicit_modifiers (
                                  META_DRM_BUFFER (buffer_gbm)) ?
                                  META_DRM_BUFFER_FLAG_NONE :
                                  META_DRM_BUFFER_FLAG_DISABLE_MODIFIERS,
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

  buffer_class->export_fd = meta_drm_buffer_import_export_fd;
  buffer_class->export_fd_for_plane = meta_drm_buffer_import_export_fd_for_plane;
  buffer_class->get_width = meta_drm_buffer_import_get_width;
  buffer_class->get_height = meta_drm_buffer_import_get_height;
  buffer_class->get_n_planes = meta_drm_buffer_import_get_n_planes;
  buffer_class->get_stride = meta_drm_buffer_import_get_stride;
  buffer_class->get_stride_for_plane = meta_drm_buffer_import_get_stride_for_plane;
  buffer_class->get_bpp = meta_drm_buffer_import_get_bpp;
  buffer_class->get_format = meta_drm_buffer_import_get_format;
  buffer_class->get_offset_for_plane = meta_drm_buffer_import_get_offset_for_plane;
  buffer_class->get_modifier = meta_drm_buffer_import_get_modifier;
}
