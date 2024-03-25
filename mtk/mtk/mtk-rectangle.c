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

#include "config.h"

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
 * mtk_rectangle_new:
 * @x: X coordinate of the top left corner
 * @y: Y coordinate of the top left corner
 * @width: Width of the rectangle
 * @height: Height of the rectangle
 *
 * Creates a new rectangle
 */
MtkRectangle *
mtk_rectangle_new (int x,
                   int y,
                   int width,
                   int height)
{
  MtkRectangle *rect;

  rect = g_new0 (MtkRectangle, 1);
  rect->x = x;
  rect->y = y;
  rect->width = width;
  rect->height = height;

  return rect;
}

MtkRectangle *
mtk_rectangle_new_empty (void)
{
  return g_new0 (MtkRectangle, 1);
}

/**
 * mtk_rectangle_area:
 * @rect: A rectangle
 *
 * Returns: The area of the rectangle
 */
int
mtk_rectangle_area (const MtkRectangle *rect)
{
  g_return_val_if_fail (rect != NULL, 0);
  return rect->width * rect->height;
}

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

/**
 * mtk_rectangle_intersect:
 * @src1: a #MtkRectangle
 * @src2: another #MtkRectangle
 * @dest: (out caller-allocates): an empty #MtkRectangle, to be filled
 *   with the coordinates of the intersection.
 * 
 * Find the intersection between the two rectangles
 *
 * Returns: TRUE is some intersection exists and is not degenerate, FALSE
 *   otherwise.
 */
gboolean
mtk_rectangle_intersect (const MtkRectangle *src1,
                         const MtkRectangle *src2,
                         MtkRectangle       *dest)
{
  int dest_x, dest_y;
  int dest_w, dest_h;
  int return_val;

  g_return_val_if_fail (src1 != NULL, FALSE);
  g_return_val_if_fail (src2 != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  return_val = FALSE;

  dest_x = MAX (src1->x, src2->x);
  dest_y = MAX (src1->y, src2->y);
  dest_w = MIN (src1->x + src1->width, src2->x + src2->width) - dest_x;
  dest_h = MIN (src1->y + src1->height, src2->y + src2->height) - dest_y;

  if (dest_w > 0 && dest_h > 0)
    {
      dest->x = dest_x;
      dest->y = dest_y;
      dest->width = dest_w;
      dest->height = dest_h;
      return_val = TRUE;
    }
  else
    {
      dest->width = 0;
      dest->height = 0;
    }

  return return_val;
}

/**
 * mtk_rectangle_overlap:
 * @rect1: The first rectangle
 * @rect2: The second rectangle
 *
 * Similar to [method@Rectangle.intersect] but doesn't provide
 * the location of the intersection.
 *
 * Returns: Whether the two rectangles overlap
 */
gboolean
mtk_rectangle_overlap (const MtkRectangle *rect1,
                       const MtkRectangle *rect2)
{
  g_return_val_if_fail (rect1 != NULL, FALSE);
  g_return_val_if_fail (rect2 != NULL, FALSE);

  return !((rect1->x + rect1->width  <= rect2->x) ||
           (rect2->x + rect2->width  <= rect1->x) ||
           (rect1->y + rect1->height <= rect2->y) ||
           (rect2->y + rect2->height <= rect1->y));
}

/**
 * mtk_rectangle_vert_overlap:
 * @rect1: The first rectangle
 * @rect2: The second rectangle
 *
 * Similar to [method@Rectangle.overlap] but ignores the horizontal location.
 *
 * Returns: Whether the two rectangles overlap vertically
 */
gboolean
mtk_rectangle_vert_overlap (const MtkRectangle *rect1,
                            const MtkRectangle *rect2)
{
  return (rect1->y < rect2->y + rect2->height &&
          rect2->y < rect1->y + rect1->height);
}

/**
 * mtk_rectangle_horiz_overlap:
 * @rect1: The first rectangle
 * @rect2: The second rectangle
 *
 * Similar to [method@Rectangle.overlap] but ignores the vertical location.
 *
 * Returns: Whether the two rectangles overlap horizontally
 */
gboolean
mtk_rectangle_horiz_overlap (const MtkRectangle *rect1,
                             const MtkRectangle *rect2)
{
  return (rect1->x < rect2->x + rect2->width &&
          rect2->x < rect1->x + rect1->width);
}

/**
 * mtk_rectangle_could_fit_rect:
 * @outer_rect: The outer rectangle
 * @inner_rect: The inner rectangle
 *
 * Returns: Whether the inner rectangle could fit inside the outer one
 */
gboolean
mtk_rectangle_could_fit_rect (const MtkRectangle *outer_rect,
                              const MtkRectangle *inner_rect)
{
  return (outer_rect->width  >= inner_rect->width &&
          outer_rect->height >= inner_rect->height);
}

/**
 * mtk_rectangle_contains_rect:
 * @outer_rect: The outer rectangle
 * @inner_rect: The inner rectangle
 *
 * Returns: Whether the outer rectangle contains the inner one
 */
gboolean
mtk_rectangle_contains_rect (const MtkRectangle *outer_rect,
                             const MtkRectangle *inner_rect)
{
  return
    inner_rect->x                      >= outer_rect->x &&
    inner_rect->y                      >= outer_rect->y &&
    inner_rect->x + inner_rect->width  <= outer_rect->x + outer_rect->width &&
    inner_rect->y + inner_rect->height <= outer_rect->y + outer_rect->height;
}

/**
 * mtk_rectangle_to_graphene_rect:
 * @rect: A rectangle
 *
 * Returns: Return a graphene_rect_t created from `rect`
 */
graphene_rect_t
mtk_rectangle_to_graphene_rect (const MtkRectangle *rect)
{
  return (graphene_rect_t) {
           .origin = {
             .x = rect->x,
             .y = rect->y
           },
           .size = {
             .width = rect->width,
             .height = rect->height
           }
  };
}

/**
 * mtk_rectangle_from_graphene_rect:
 * @rect: A rectangle
 * @rounding_strategy: The rounding strategy
 * @dest: (out caller-allocates): an empty #MtkRectangle, to be filled
 *   with the coordinates of `rect`.
 */
void
mtk_rectangle_from_graphene_rect (const graphene_rect_t *rect,
                                  MtkRoundingStrategy    rounding_strategy,
                                  MtkRectangle          *dest)
{
  switch (rounding_strategy)
    {
    case MTK_ROUNDING_STRATEGY_SHRINK:
      {
        *dest = (MtkRectangle) {
          .x = ceilf (rect->origin.x),
          .y = ceilf (rect->origin.y),
          .width = floorf (rect->size.width),
          .height = floorf (rect->size.height),
        };
      }
      break;
    case MTK_ROUNDING_STRATEGY_GROW:
      {
        graphene_rect_t clamped = *rect;

        graphene_rect_round_extents (&clamped, &clamped);

        *dest = (MtkRectangle) {
          .x = clamped.origin.x,
          .y = clamped.origin.y,
          .width = clamped.size.width,
          .height = clamped.size.height,
        };
      }
      break;
    case MTK_ROUNDING_STRATEGY_ROUND:
      {
        *dest = (MtkRectangle) {
          .x = roundf (rect->origin.x),
          .y = roundf (rect->origin.y),
          .width = roundf (rect->size.width),
          .height = roundf (rect->size.height),
        };
      }
    }
}

void
mtk_rectangle_crop_and_scale (const MtkRectangle    *rect,
                              const graphene_rect_t *src_rect,
                              int                    dst_width,
                              int                    dst_height,
                              MtkRectangle          *dest)
{
  graphene_rect_t tmp = GRAPHENE_RECT_INIT (rect->x, rect->y,
                                            rect->width, rect->height);

  graphene_rect_scale (&tmp,
                       src_rect->size.width / dst_width,
                       src_rect->size.height / dst_height,
                       &tmp);
  graphene_rect_offset (&tmp, src_rect->origin.x, src_rect->origin.y);

  mtk_rectangle_from_graphene_rect (&tmp, MTK_ROUNDING_STRATEGY_GROW, dest);
}

void
mtk_rectangle_scale_double (const MtkRectangle  *rect,
                            double               scale,
                            MtkRoundingStrategy  rounding_strategy,
                            MtkRectangle        *dest)
{
  graphene_rect_t tmp = GRAPHENE_RECT_INIT (rect->x, rect->y,
                                            rect->width, rect->height);

  graphene_rect_scale (&tmp, scale, scale, &tmp);
  mtk_rectangle_from_graphene_rect (&tmp, rounding_strategy, dest);
}

gboolean
mtk_rectangle_is_adjacent_to (const MtkRectangle *rect,
                              const MtkRectangle *other)
{
  int rect_x1 = rect->x;
  int rect_y1 = rect->y;
  int rect_x2 = rect->x + rect->width;
  int rect_y2 = rect->y + rect->height;
  int other_x1 = other->x;
  int other_y1 = other->y;
  int other_x2 = other->x + other->width;
  int other_y2 = other->y + other->height;

  if ((rect_x1 == other_x2 || rect_x2 == other_x1) &&
      !(rect_y2 <= other_y1 || rect_y1 >= other_y2))
    return TRUE;
  else if ((rect_y1 == other_y2 || rect_y2 == other_y1) &&
           !(rect_x2 <= other_x1 || rect_x1 >= other_x2))
    return TRUE;
  else
    return FALSE;
}
