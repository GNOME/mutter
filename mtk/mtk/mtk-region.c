/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2023 Red Hat
 *
 * The implementation is heavily inspired by cairo_region_t.
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

#include <pixman.h>

#include "mtk/mtk-region.h"

struct _MtkRegion
{
  pixman_region32_t inner_region;
};

/**
 * mtk_region_ref:
 * @region: A region
 *
 * Increases the reference count
 *
 * Returns: (transfer none): The region
 */
MtkRegion *
mtk_region_ref (MtkRegion *region)
{
  g_return_val_if_fail (region != NULL, NULL);

  return g_atomic_rc_box_acquire (region);
}

static void
clear_region (gpointer data)
{
  MtkRegion *region = data;

  pixman_region32_fini (&region->inner_region);
}

void
mtk_region_unref (MtkRegion *region)
{
  g_return_if_fail (region != NULL);

  g_atomic_rc_box_release_full (region, clear_region);
}

G_DEFINE_BOXED_TYPE (MtkRegion, mtk_region,
                     mtk_region_ref, mtk_region_unref);

MtkRegion *
mtk_region_create (void)
{
  MtkRegion *region;

  region = g_atomic_rc_box_new0 (MtkRegion);

  pixman_region32_init (&region->inner_region);

  return region;
}


/**
 * mtk_region_copy:
 * @region: The region to copy
 *
 * Returns: (transfer full): A copy of the passed region
 */
MtkRegion *
mtk_region_copy (const MtkRegion *region)
{
  g_autoptr (MtkRegion) copy = NULL;

  g_return_val_if_fail (region != NULL, NULL);

  copy = mtk_region_create ();

  if (!pixman_region32_copy (&copy->inner_region,
                             &region->inner_region))
    return NULL;

  return g_steal_pointer (&copy);
}

gboolean
mtk_region_equal (const MtkRegion *region,
                  const MtkRegion *other)
{
  if (region == other)
    return TRUE;

  if (region == NULL || other == NULL)
    return FALSE;

  return pixman_region32_equal (&region->inner_region,
                                &other->inner_region);
}

gboolean
mtk_region_is_empty (const MtkRegion *region)
{
  g_return_val_if_fail (region != NULL, TRUE);

  return !pixman_region32_not_empty (&region->inner_region);
}

MtkRectangle
mtk_region_get_extents (const MtkRegion *region)
{
  pixman_box32_t *extents;

  g_return_val_if_fail (region != NULL, MTK_RECTANGLE_INIT (0, 0, 0, 0));

  extents = pixman_region32_extents (&region->inner_region);
  return MTK_RECTANGLE_INIT (extents->x1,
                             extents->y1,
                             extents->x2 - extents->x1,
                             extents->y2 - extents->y1);
}

int
mtk_region_num_rectangles (const MtkRegion *region)
{
  g_return_val_if_fail (region != NULL, 0);

  return pixman_region32_n_rects (&region->inner_region);
}

void
mtk_region_translate (MtkRegion *region,
                      int        dx,
                      int        dy)
{
  g_return_if_fail (region != NULL);

  pixman_region32_translate (&region->inner_region, dx, dy);
}

gboolean
mtk_region_contains_point (MtkRegion *region,
                           int        x,
                           int        y)
{
  g_return_val_if_fail (region != NULL, FALSE);

  return pixman_region32_contains_point (&region->inner_region, x, y, NULL);
}

void
mtk_region_union (MtkRegion       *region,
                  const MtkRegion *other)
{
  g_return_if_fail (region != NULL);
  g_return_if_fail (other != NULL);

  pixman_region32_union (&region->inner_region,
                         &region->inner_region,
                         &other->inner_region);
}

void
mtk_region_union_rectangle (MtkRegion          *region,
                            const MtkRectangle *rect)
{
  pixman_region32_t pixman_region;

  g_return_if_fail (region != NULL);
  g_return_if_fail (rect != NULL);

  pixman_region32_init_rect (&pixman_region,
                             rect->x, rect->y,
                             rect->width, rect->height);
  pixman_region32_union (&region->inner_region,
                         &region->inner_region,
                         &pixman_region);
  pixman_region32_fini (&pixman_region);
}

void
mtk_region_subtract (MtkRegion       *region,
                     const MtkRegion *other)
{
  g_return_if_fail (region != NULL);
  g_return_if_fail (other != NULL);

  pixman_region32_subtract (&region->inner_region,
                            &region->inner_region,
                            &other->inner_region);
}

void
mtk_region_subtract_rectangle (MtkRegion          *region,
                               const MtkRectangle *rect)
{
  g_return_if_fail (region != NULL);
  g_return_if_fail (rect != NULL);

  pixman_region32_t pixman_region;
  pixman_region32_init_rect (&pixman_region,
                             rect->x, rect->y,
                             rect->width, rect->height);

  pixman_region32_subtract (&region->inner_region,
                            &region->inner_region,
                            &pixman_region);
  pixman_region32_fini (&pixman_region);
}

void
mtk_region_intersect (MtkRegion       *region,
                      const MtkRegion *other)
{
  g_return_if_fail (region != NULL);
  g_return_if_fail (other != NULL);

  pixman_region32_intersect (&region->inner_region,
                             &region->inner_region,
                             &other->inner_region);
}

void
mtk_region_intersect_rectangle (MtkRegion          *region,
                                const MtkRectangle *rect)
{
  pixman_region32_t pixman_region;

  g_return_if_fail (region != NULL);

  pixman_region32_init_rect (&pixman_region,
                             rect->x, rect->y,
                             rect->width, rect->height);

  pixman_region32_intersect (&region->inner_region,
                             &region->inner_region,
                             &pixman_region);
  pixman_region32_fini (&pixman_region);
}

MtkRectangle
mtk_region_get_rectangle (const MtkRegion *region,
                          int              nth)
{
  pixman_box32_t *box;

  g_return_val_if_fail (region != NULL, MTK_RECTANGLE_INIT (0, 0, 0, 0));

  box = pixman_region32_rectangles (&region->inner_region, NULL) + nth;
  return MTK_RECTANGLE_INIT (box->x1, box->y1, box->x2 - box->x1, box->y2 - box->y1);
}

MtkRegion *
mtk_region_create_rectangle (const MtkRectangle *rect)
{
  MtkRegion *region;
  g_return_val_if_fail (rect != NULL, NULL);

  region = g_atomic_rc_box_new0 (MtkRegion);

  pixman_region32_init_rect (&region->inner_region,
                             rect->x, rect->y,
                             rect->width, rect->height);
  return region;
}

MtkRegion *
mtk_region_create_rectangles (const MtkRectangle *rects,
                              int                 n_rects)
{
  pixman_box32_t stack_boxes[512 * sizeof (int) / sizeof (pixman_box32_t)];
  pixman_box32_t *boxes = stack_boxes;
  int i;
  g_autoptr (MtkRegion) region = NULL;

  g_return_val_if_fail (rects != NULL, NULL);
  g_return_val_if_fail (n_rects != 0, NULL);

  region = g_atomic_rc_box_new0 (MtkRegion);

  if (n_rects == 1)
    {
      pixman_region32_init_rect (&region->inner_region,
                                 rects->x, rects->y,
                                 rects->width, rects->height);

      return g_steal_pointer (&region);
    }

  if (n_rects > sizeof (stack_boxes) / sizeof (stack_boxes[0]))
    {
      boxes = g_new0 (pixman_box32_t, n_rects);
      if (G_UNLIKELY (boxes == NULL))
        return NULL;
    }

  for (i = 0; i < n_rects; i++)
    {
      boxes[i].x1 = rects[i].x;
      boxes[i].y1 = rects[i].y;
      boxes[i].x2 = rects[i].x + rects[i].width;
      boxes[i].y2 = rects[i].y + rects[i].height;
    }

  i = pixman_region32_init_rects (&region->inner_region,
                                  boxes, n_rects);

  if (boxes != stack_boxes)
    free (boxes);

  if (G_UNLIKELY (i == 0))
    return NULL;

  return g_steal_pointer (&region);
}

MtkRegionOverlap
mtk_region_contains_rectangle (const MtkRegion    *region,
                               const MtkRectangle *rect)
{
  pixman_box32_t box;
  pixman_region_overlap_t overlap;

  g_return_val_if_fail (region != NULL, MTK_REGION_OVERLAP_OUT);
  g_return_val_if_fail (rect != NULL, MTK_REGION_OVERLAP_OUT);

  box.x1 = rect->x;
  box.y1 = rect->y;
  box.x2 = rect->x + rect->width;
  box.y2 = rect->y + rect->height;

  overlap = pixman_region32_contains_rectangle (&region->inner_region,
                                                &box);
  switch (overlap)
    {
    default:
    case PIXMAN_REGION_OUT:
      return MTK_REGION_OVERLAP_OUT;
    case PIXMAN_REGION_IN:
      return MTK_REGION_OVERLAP_IN;
    case PIXMAN_REGION_PART:
      return MTK_REGION_OVERLAP_PART;
    }
}

MtkRegion *
mtk_region_scale (MtkRegion *region,
                  int        scale)
{
  int n_rects, i;
  MtkRectangle *rects;
  MtkRegion *scaled_region;

  if (scale == 1)
    return mtk_region_copy (region);

  n_rects = mtk_region_num_rectangles (region);
  MTK_RECTANGLE_CREATE_ARRAY_SCOPED (n_rects, rects);
  for (i = 0; i < n_rects; i++)
    {
      rects[i] = mtk_region_get_rectangle (region, i);
      rects[i].x *= scale;
      rects[i].y *= scale;
      rects[i].width *= scale;
      rects[i].height *= scale;
    }

  scaled_region = mtk_region_create_rectangles (rects, n_rects);

  return scaled_region;
}

MtkRegion *
mtk_region_crop_and_scale (MtkRegion       *region,
                           graphene_rect_t *src_rect,
                           int              dst_width,
                           int              dst_height)
{
  int n_rects, i;
  MtkRectangle *rects;
  MtkRegion *viewport_region;

  if (G_APPROX_VALUE (src_rect->size.width, dst_width, FLT_EPSILON) &&
      G_APPROX_VALUE (src_rect->size.height, dst_height, FLT_EPSILON) &&
      G_APPROX_VALUE (roundf (src_rect->origin.x),
                      src_rect->origin.x, FLT_EPSILON) &&
      G_APPROX_VALUE (roundf (src_rect->origin.y),
                      src_rect->origin.y, FLT_EPSILON))
    {
      viewport_region = mtk_region_copy (region);

      if (!G_APPROX_VALUE (src_rect->origin.x, 0, FLT_EPSILON) ||
          !G_APPROX_VALUE (src_rect->origin.y, 0, FLT_EPSILON))
        {
          mtk_region_translate (viewport_region,
                                (int) src_rect->origin.x,
                                (int) src_rect->origin.y);
        }

      return viewport_region;
    }

  n_rects = mtk_region_num_rectangles (region);
  MTK_RECTANGLE_CREATE_ARRAY_SCOPED (n_rects, rects);
  for (i = 0; i < n_rects; i++)
    {
      rects[i] = mtk_region_get_rectangle (region, i);

      mtk_rectangle_crop_and_scale (&rects[i],
                                    src_rect,
                                    dst_width,
                                    dst_height,
                                    &rects[i]);
    }

  viewport_region = mtk_region_create_rectangles (rects, n_rects);

  return viewport_region;
}

MtkRegion *
mtk_region_apply_matrix_transform_expand (const MtkRegion   *region,
                                          graphene_matrix_t *transform)
{
  MtkRegion *transformed_region;
  MtkRectangle *rects;
  int n_rects, i;

  if (graphene_matrix_is_identity (transform))
    return mtk_region_copy (region);

  n_rects = mtk_region_num_rectangles (region);
  MTK_RECTANGLE_CREATE_ARRAY_SCOPED (n_rects, rects);
  for (i = 0; i < n_rects; i++)
    {
      graphene_rect_t transformed_rect, rect;
      MtkRectangle int_rect;

      int_rect = mtk_region_get_rectangle (region, i);
      rect = mtk_rectangle_to_graphene_rect (&int_rect);

      graphene_matrix_transform_bounds (transform, &rect, &transformed_rect);

      mtk_rectangle_from_graphene_rect (&transformed_rect,
                                        MTK_ROUNDING_STRATEGY_GROW,
                                        &rects[i]);
    }

  transformed_region = mtk_region_create_rectangles (rects, n_rects);

  return transformed_region;
}

void
mtk_region_iterator_init (MtkRegionIterator *iter,
                          MtkRegion         *region)
{
  iter->region = region;
  iter->i = 0;
  iter->n_rectangles = mtk_region_num_rectangles (region);
  iter->line_start = TRUE;

  if (iter->n_rectangles > 1)
    {
      iter->rectangle = mtk_region_get_rectangle (region, 0);
      iter->next_rectangle = mtk_region_get_rectangle (region, 1);

      iter->line_end = iter->next_rectangle.y != iter->rectangle.y;
    }
  else if (iter->n_rectangles > 0)
    {
      iter->rectangle = mtk_region_get_rectangle (region, 0);
      iter->line_end = TRUE;
    }
}

gboolean
mtk_region_iterator_at_end (MtkRegionIterator *iter)
{
  return iter->i >= iter->n_rectangles;
}

void
mtk_region_iterator_next (MtkRegionIterator *iter)
{
  iter->i++;
  iter->rectangle = iter->next_rectangle;
  iter->line_start = iter->line_end;

  if (iter->i + 1 < iter->n_rectangles)
    {
      iter->next_rectangle = mtk_region_get_rectangle (iter->region, iter->i + 1);
      iter->line_end = iter->next_rectangle.y != iter->rectangle.y;
    }
  else
    {
      iter->line_end = TRUE;
    }
}

/* Various algorithms in this file require unioning together a set of rectangles
 * that are unsorted or overlap; unioning such a set of rectangles 1-by-1
 * using mtk_region_union_rectangle() produces O(N^2) behavior (if the union
 * adds or removes rectangles in the middle of the region, then it has to
 * move all the rectangles after that.) To avoid this behavior, MtkRegionBuilder
 * creates regions for small groups of rectangles and merges them together in
 * a binary tree.
 *
 * Possible improvement: From a glance at the code, accumulating all the rectangles
 *  into a flat array and then calling the (not usefully documented)
 *  mtk_region_create_rectangles() would have the same behavior and would be
 *  simpler and a bit more efficient.
 */

/* Optimium performance seems to be with MAX_CHUNK_RECTANGLES=4; 8 is about 10% slower.
 * But using 8 may be more robust to systems with slow malloc(). */
#define MAX_CHUNK_RECTANGLES 8

void
mtk_region_builder_init (MtkRegionBuilder *builder)
{
  int i;

  for (i = 0; i < MTK_REGION_BUILDER_MAX_LEVELS; i++)
    builder->levels[i] = NULL;
  builder->n_levels = 1;
}

void
mtk_region_builder_add_rectangle (MtkRegionBuilder *builder,
                                  int               x,
                                  int               y,
                                  int               width,
                                  int               height)
{
  MtkRectangle rect;
  int i;

  if (builder->levels[0] == NULL)
    builder->levels[0] = mtk_region_create ();

  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;

  mtk_region_union_rectangle (builder->levels[0], &rect);
  if (mtk_region_num_rectangles (builder->levels[0]) >= MAX_CHUNK_RECTANGLES)
    {
      for (i = 1; i < builder->n_levels + 1; i++)
        {
          if (builder->levels[i] == NULL)
            {
              if (i < MTK_REGION_BUILDER_MAX_LEVELS)
                {
                  builder->levels[i] = builder->levels[i - 1];
                  builder->levels[i - 1] = NULL;
                  if (i == builder->n_levels)
                    builder->n_levels++;
                }

              break;
            }
          else
            {
              mtk_region_union (builder->levels[i], builder->levels[i - 1]);
              mtk_region_unref (builder->levels[i - 1]);
              builder->levels[i - 1] = NULL;
            }
        }
    }
}

MtkRegion *
mtk_region_builder_finish (MtkRegionBuilder *builder)
{
  MtkRegion *result = NULL;
  int i;

  for (i = 0; i < builder->n_levels; i++)
    {
      if (builder->levels[i])
        {
          if (result == NULL)
            {
              result = builder->levels[i];
            }
          else
            {
              mtk_region_union (result, builder->levels[i]);
              mtk_region_unref (builder->levels[i]);
            }
        }
    }

  if (result == NULL)
    result = mtk_region_create ();

  return result;
}
