/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2023 Red Hat
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

#include "mtk/mtk-rectangle.h"


MtkRectangle *
mtk_rectangle_copy (const MtkRectangle *rect)
{
  return g_memdup2 (rect, sizeof (MtkRectangle));
}

void
mtk_rectangle_free (MtkRectangle *rect)
{
  g_free (rect);
}

G_DEFINE_BOXED_TYPE (MtkRectangle, mtk_rectangle,
                     mtk_rectangle_copy, mtk_rectangle_free);
