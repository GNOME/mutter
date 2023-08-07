/*
 * Copyright (C) 2021 Red Hat, Inc.
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

#pragma once

#include <glib-object.h>
#include <stdint.h>

#include "core/util-private.h"

typedef enum _MetaDeviceFileFlags
{
  META_DEVICE_FILE_FLAG_NONE = 0,
  META_DEVICE_FILE_FLAG_TAKE_CONTROL = 1 << 0,
  META_DEVICE_FILE_FLAG_READ_ONLY = 1 << 1,
} MetaDeviceFileFlags;

typedef enum _MetaDeviceFileTags
{
  META_DEVICE_FILE_TAG_KMS,

  META_DEVICE_FILE_N_TAGS,
} MetaDeviceFileTags;

typedef struct _MetaDeviceFile MetaDeviceFile;
typedef struct _MetaDevicePool MetaDevicePool;

META_EXPORT_TEST
int meta_device_file_get_fd (MetaDeviceFile *device_file);

const char * meta_device_file_get_path (MetaDeviceFile *device_file);

void meta_device_file_tag (MetaDeviceFile     *device_file,
                           MetaDeviceFileTags  tag,
                           uint32_t            value);

uint32_t meta_device_file_has_tag (MetaDeviceFile     *device_file,
                                   MetaDeviceFileTags  tag,
                                   uint32_t            value);

MetaDeviceFile * meta_device_file_acquire (MetaDeviceFile *file);

META_EXPORT_TEST
void meta_device_file_release (MetaDeviceFile *device_file);

MetaDevicePool * meta_device_file_get_pool (MetaDeviceFile *device_file);

META_EXPORT_TEST
MetaDeviceFile * meta_device_pool_open (MetaDevicePool       *pool,
                                        const char           *path,
                                        MetaDeviceFileFlags   flags,
                                        GError              **error);

MetaBackend * meta_device_pool_get_backend (MetaDevicePool *pool);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaDeviceFile, meta_device_file_release)
