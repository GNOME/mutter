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

#pragma once

#include "meta/common.h"
#include "core/util-private.h"

typedef struct _MetaAnonymousFile MetaAnonymousFile;

typedef enum _MetaAnonymousFileMapmode
{
  META_ANONYMOUS_FILE_MAPMODE_PRIVATE,
  META_ANONYMOUS_FILE_MAPMODE_SHARED,
} MetaAnonymousFileMapmode;

META_EXPORT_TEST
MetaAnonymousFile * meta_anonymous_file_new (const char    *name,
                                             size_t         size,
                                             const uint8_t *data);

META_EXPORT_TEST
void meta_anonymous_file_free (MetaAnonymousFile *file);

META_EXPORT_TEST
size_t meta_anonymous_file_size (MetaAnonymousFile *file);

META_EXPORT_TEST
int meta_anonymous_file_open_fd (MetaAnonymousFile        *file,
                                 MetaAnonymousFileMapmode  mapmode);

META_EXPORT_TEST
void meta_anonymous_file_close_fd (int fd);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaAnonymousFile, meta_anonymous_file_free)
