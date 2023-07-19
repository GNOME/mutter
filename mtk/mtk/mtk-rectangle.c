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

/**
 * mtk_rectangle_equal:
 * @src1: The first rectangle
 * @src2: The second rectangle
 *
 * Compares the two rectangles
 *
 * Returns: Whether the two rectangles are equal
 */
gboolean
mtk_rectangle_equal (const MtkRectangle *src1,
                     const MtkRectangle *src2)
{
  return ((src1->x == src2->x) &&
          (src1->y == src2->y) &&
          (src1->width == src2->width) &&
          (src1->height == src2->height));
}

/**
 * mtk_rectangle_union:
 * @rect1: a #MtkRectangle
 * @rect2: another #MtkRectangle
 * @dest: (out caller-allocates): an empty #MtkRectangle, to be filled
 *   with the coordinates of the bounding box.
 * 
 * Computes the union of the two rectangles
 */
void
mtk_rectangle_union (const MtkRectangle *rect1,
                     const MtkRectangle *rect2,
                     MtkRectangle       *dest)
{
  int dest_x, dest_y;
  int dest_w, dest_h;

  dest_x = rect1->x;
  dest_y = rect1->y;
  dest_w = rect1->width;
  dest_h = rect1->height;

  if (rect2->x < dest_x)
    {
      dest_w += dest_x - rect2->x;
      dest_x = rect2->x;
    }
  if (rect2->y < dest_y)
    {
      dest_h += dest_y - rect2->y;
      dest_y = rect2->y;
    }
  if (rect2->x + rect2->width > dest_x + dest_w)
    dest_w = rect2->x + rect2->width - dest_x;
  if (rect2->y + rect2->height > dest_y + dest_h)
    dest_h = rect2->y + rect2->height - dest_y;

  dest->x = dest_x;
  dest->y = dest_y;
  dest->width = dest_w;
  dest->height = dest_h;
}
