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

#include <glib.h>
#include <stdint.h>

#include "mtk/mtk-macros.h"

typedef struct _MtkAnonymousFile MtkAnonymousFile;

typedef enum _MtkAnonymousFileMapmode
{
  MTK_ANONYMOUS_FILE_MAPMODE_PRIVATE,
  MTK_ANONYMOUS_FILE_MAPMODE_SHARED,
} MtkAnonymousFileMapmode;

MTK_EXPORT
MtkAnonymousFile * mtk_anonymous_file_new (const char    *name,
                                           size_t         size,
                                           const uint8_t *data);

MTK_EXPORT
void mtk_anonymous_file_free (MtkAnonymousFile *file);

MTK_EXPORT
size_t mtk_anonymous_file_size (const MtkAnonymousFile *file);

MTK_EXPORT
int mtk_anonymous_file_open_fd (const MtkAnonymousFile  *file,
                                MtkAnonymousFileMapmode  mapmode);

MTK_EXPORT
void mtk_anonymous_file_close_fd (int fd);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MtkAnonymousFile, mtk_anonymous_file_free)
