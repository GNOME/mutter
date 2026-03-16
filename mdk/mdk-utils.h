/*
 * Copyright (C) 2026 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>

typedef struct _MdkSize
{
  int width;
  int height;
} MdkSize;

#define MDK_TYPE_SIZE (mdk_size_get_type ())

GType mdk_size_get_type (void);

MdkSize * mdk_size_copy (MdkSize *size);

void mdk_size_free (MdkSize *size);

gboolean mdk_size_is_empty (MdkSize *size);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MdkSize, mdk_size_free)
