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

#pragma once

#include <glib-object.h>

#include "mtk/mtk-rectangle.h"
#include "mtk/mtk-macros.h"

#define MTK_TYPE_REGION (mtk_region_get_type ())

typedef enum _MtkRegionOverlap
{
  MTK_REGION_OVERLAP_OUT,
  MTK_REGION_OVERLAP_IN,
  MTK_REGION_OVERLAP_PART,
} MtkRegionOverlap;

typedef struct _MtkRegion MtkRegion;

MTK_EXPORT
GType mtk_region_get_type (void);

MTK_EXPORT
MtkRegion * mtk_region_copy (const MtkRegion *region);

MTK_EXPORT
MtkRegion * mtk_region_ref (MtkRegion *region);

MTK_EXPORT
void mtk_region_unref (MtkRegion *region);

MTK_EXPORT
MtkRegion * mtk_region_create (void);

MTK_EXPORT
gboolean mtk_region_equal (const MtkRegion *region,
                           const MtkRegion *other);

MTK_EXPORT
gboolean mtk_region_is_empty (const MtkRegion *region);

MTK_EXPORT
MtkRectangle mtk_region_get_extents (const MtkRegion *region);

MTK_EXPORT
int mtk_region_num_rectangles (const MtkRegion *region);

MTK_EXPORT
void mtk_region_translate (MtkRegion *region,
                           int        dx,
                           int        dy);

MTK_EXPORT
gboolean mtk_region_contains_point (MtkRegion *region,
                                    int        x,
                                    int        y);

MTK_EXPORT
void mtk_region_union (MtkRegion       *region,
                       const MtkRegion *other);

MTK_EXPORT
void mtk_region_union_rectangle (MtkRegion          *region,
                                 const MtkRectangle *rect);

MTK_EXPORT
void mtk_region_subtract_rectangle (MtkRegion          *region,
                                    const MtkRectangle *rect);

MTK_EXPORT
void mtk_region_subtract (MtkRegion       *region,
                          const MtkRegion *other);

MTK_EXPORT
void mtk_region_intersect (MtkRegion       *region,
                           const MtkRegion *other);

MTK_EXPORT
void mtk_region_intersect_rectangle (MtkRegion          *region,
                                     const MtkRectangle *rect);

MTK_EXPORT
MtkRectangle mtk_region_get_rectangle (const MtkRegion *region,
                                       int              nth);

MTK_EXPORT
MtkRegion * mtk_region_create_rectangle (const MtkRectangle *rect);

MTK_EXPORT
MtkRegion * mtk_region_create_rectangles (const MtkRectangle *rects,
                                          int                 n_rects);

MTK_EXPORT
MtkRegionOverlap mtk_region_contains_rectangle (const MtkRegion    *region,
                                                const MtkRectangle *rect);

MTK_EXPORT
MtkRegion * mtk_region_scale (MtkRegion *region,
                              int        scale);

MTK_EXPORT
MtkRegion * mtk_region_crop_and_scale (MtkRegion       *region,
                                       graphene_rect_t *src_rect,
                                       int              dst_width,
                                       int              dst_height);

MTK_EXPORT
MtkRegion * mtk_region_apply_matrix_transform_expand (const MtkRegion   *region,
                                                      graphene_matrix_t *transform);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MtkRegion, mtk_region_unref)

/**
 * MtkRegionIterator:
 * @region: region being iterated
 * @rectangle: current rectangle
 * @line_start: whether the current rectangle starts a horizontal band
 * @line_end: whether the current rectangle ends a horizontal band
 *
 * MtkRegion is a yx banded region; sometimes its useful to iterate through
 * such a region treating the start and end of each horizontal band in a distinct
 * fashion.
 *
 * Usage:
 *
 * ```c
 *  MtkRegionIterator iter;
 *  for (mtk_region_iterator_init (&iter, region);
 *       !mtk_region_iterator_at_end (&iter);
 *       mtk_region_iterator_next (&iter))
 *  {
 *    [ Use iter.rectangle, iter.line_start, iter.line_end ]
 *  }
 *```
 */
typedef struct MtkaRegionIterator MtkRegionIterator;

struct MtkaRegionIterator {
  MtkRegion *region;
  MtkRectangle rectangle;
  gboolean line_start;
  gboolean line_end;
  int i;

  /*< private >*/
  int n_rectangles;
  MtkRectangle next_rectangle;
};

MTK_EXPORT
void mtk_region_iterator_init (MtkRegionIterator *iter,
                               MtkRegion          *region);

MTK_EXPORT
gboolean mtk_region_iterator_at_end (MtkRegionIterator *iter);

MTK_EXPORT
void mtk_region_iterator_next (MtkRegionIterator *iter);

typedef struct _MtkRegionBuilder MtkRegionBuilder;

#define MTK_REGION_BUILDER_MAX_LEVELS 16

struct _MtkRegionBuilder {
  /* To merge regions in binary tree order, we need to keep track of
   * the regions that we've already merged together at different
   * levels of the tree. We fill in an array in the pattern:
   *
   * |a  |
   * |b  |a  |
   * |c  |   |ab |
   * |d  |c  |ab |
   * |e  |   |   |abcd|
   */
  MtkRegion *levels[MTK_REGION_BUILDER_MAX_LEVELS];
  int n_levels;
};

MTK_EXPORT
void mtk_region_builder_init (MtkRegionBuilder *builder);

MTK_EXPORT
void mtk_region_builder_add_rectangle (MtkRegionBuilder *builder,
                                       int               x,
                                       int               y,
                                       int               width,
                                       int               height);

MTK_EXPORT
MtkRegion * mtk_region_builder_finish (MtkRegionBuilder *builder);
