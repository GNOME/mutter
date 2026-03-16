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

#include "config.h"

#include "mdk-utils.h"

G_DEFINE_BOXED_TYPE (MdkSize, mdk_size, mdk_size_copy, mdk_size_free)

MdkSize *
mdk_size_copy (MdkSize *size)
{
  return g_memdup2 (size, sizeof (*size));
}

void
mdk_size_free (MdkSize *size)
{
  g_free (size);
}

gboolean
mdk_size_is_empty (MdkSize *size)
{
  return size->width == 0 || size->height == 0;
}
