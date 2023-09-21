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

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MtkRegion, mtk_region_unref)
