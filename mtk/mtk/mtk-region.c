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
#include "mtk/mtk-region.h"
#include "mtk/mtk-region-private.h"

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

  g_atomic_ref_count_inc (&region->ref_count);
  return region;
}

void
mtk_region_unref (MtkRegion *region)
{
  g_return_if_fail (region != NULL);

  if (g_atomic_ref_count_dec (&region->ref_count))
    {
      pixman_region32_fini (&region->inner_region);
      g_free (region);
    }
}

G_DEFINE_BOXED_TYPE (MtkRegion, mtk_region,
                     mtk_region_ref, mtk_region_unref);

MtkRegion *
mtk_region_create (void)
{
  MtkRegion *region;

  region = g_new0 (MtkRegion, 1);

  g_atomic_ref_count_init (&region->ref_count);
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

  region = g_new0 (MtkRegion, 1);
  g_atomic_ref_count_init (&region->ref_count);
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

  region = g_new0 (MtkRegion, 1);
  g_atomic_ref_count_init (&region->ref_count);

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
