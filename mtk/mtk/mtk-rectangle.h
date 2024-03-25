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
#include <graphene.h>

#include "mtk/mtk-macros.h"

#define MTK_TYPE_RECTANGLE            (mtk_rectangle_get_type ())

typedef enum _MtkRoundingStrategy
{
  MTK_ROUNDING_STRATEGY_SHRINK,
  MTK_ROUNDING_STRATEGY_GROW,
  MTK_ROUNDING_STRATEGY_ROUND,
} MtkRoundingStrategy;


#define MTK_RECTANGLE_MAX_STACK_RECTS 256

#define MTK_RECTANGLE_CREATE_ARRAY_SCOPED(n_rects, rects) \
  g_autofree MtkRectangle *G_PASTE(__n, __LINE__) = NULL; \
  if (n_rects < MTK_RECTANGLE_MAX_STACK_RECTS) \
    rects = g_newa (MtkRectangle, n_rects); \
  else \
    rects = G_PASTE(__n, __LINE__) = g_new (MtkRectangle, n_rects);

/**
 * MtkRectangle:
 * @x: X coordinate of the top-left corner
 * @y: Y coordinate of the top-left corner
 * @width: Width of the rectangle
 * @height: Height of the rectangle
 */
struct _MtkRectangle
{
  int x;
  int y;
  int width;
  int height;
};

typedef struct _MtkRectangle MtkRectangle;

#define MTK_RECTANGLE_INIT(_x, _y, _width, _height) \
        (MtkRectangle) { \
          .x = (_x), \
          .y = (_y), \
          .width = (_width), \
          .height = (_height) \
        }

MTK_EXPORT
GType mtk_rectangle_get_type (void);

MTK_EXPORT
MtkRectangle * mtk_rectangle_copy (const MtkRectangle *rect);

MTK_EXPORT
void mtk_rectangle_free (MtkRectangle *rect);

/* Function to make initializing a rect with a single line of code easy */
MTK_EXPORT
MtkRectangle * mtk_rectangle_new (int x,
                                  int y,
                                  int width,
                                  int height);

MTK_EXPORT
MtkRectangle * mtk_rectangle_new_empty (void);

/* Basic comparison functions */
MTK_EXPORT
int mtk_rectangle_area (const MtkRectangle *rect);

MTK_EXPORT
gboolean mtk_rectangle_equal (const MtkRectangle *src1,
                              const MtkRectangle *src2);

/* Find the bounding box of the union of two rectangles */
MTK_EXPORT
void mtk_rectangle_union (const MtkRectangle *rect1,
                          const MtkRectangle *rect2,
                          MtkRectangle       *dest);

MTK_EXPORT
gboolean mtk_rectangle_intersect (const MtkRectangle *src1,
                                  const MtkRectangle *src2,
                                  MtkRectangle       *dest);

MTK_EXPORT
gboolean mtk_rectangle_overlap (const MtkRectangle *rect1,
                                const MtkRectangle *rect2);

MTK_EXPORT
gboolean mtk_rectangle_vert_overlap (const MtkRectangle *rect1,
                                     const MtkRectangle *rect2);

MTK_EXPORT
gboolean mtk_rectangle_horiz_overlap (const MtkRectangle *rect1,
                                      const MtkRectangle *rect2);

MTK_EXPORT
gboolean mtk_rectangle_could_fit_rect (const MtkRectangle *outer_rect,
                                       const MtkRectangle *inner_rect);

MTK_EXPORT
gboolean mtk_rectangle_contains_rect (const MtkRectangle *outer_rect,
                                      const MtkRectangle *inner_rect);

MTK_EXPORT
graphene_rect_t mtk_rectangle_to_graphene_rect (const MtkRectangle *rect);

MTK_EXPORT
void mtk_rectangle_from_graphene_rect (const graphene_rect_t *rect,
                                       MtkRoundingStrategy    rounding_strategy,
                                       MtkRectangle          *dest);

MTK_EXPORT
void mtk_rectangle_crop_and_scale (const MtkRectangle    *rect,
                                   const graphene_rect_t *src_rect,
                                   int                    dst_width,
                                   int                    dst_height,
                                   MtkRectangle          *dest);

MTK_EXPORT
void mtk_rectangle_scale_double (const MtkRectangle  *rect,
                                 double               scale,
                                 MtkRoundingStrategy  rounding_strategy,
                                 MtkRectangle        *dest);

MTK_EXPORT
gboolean mtk_rectangle_is_adjacent_to (const MtkRectangle *rect,
                                       const MtkRectangle *other);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MtkRectangle, mtk_rectangle_free)
