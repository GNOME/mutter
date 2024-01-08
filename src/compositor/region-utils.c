/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for region manipulation
 *
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "backends/meta-monitor-transform.h"
#include "compositor/region-utils.h"
#include "core/boxes-private.h"

#include <math.h>

/* MetaRegionBuilder */

/* Various algorithms in this file require unioning together a set of rectangles
 * that are unsorted or overlap; unioning such a set of rectangles 1-by-1
 * using mtk_region_union_rectangle() produces O(N^2) behavior (if the union
 * adds or removes rectangles in the middle of the region, then it has to
 * move all the rectangles after that.) To avoid this behavior, MetaRegionBuilder
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
meta_region_builder_init (MetaRegionBuilder *builder)
{
  int i;
  for (i = 0; i < META_REGION_BUILDER_MAX_LEVELS; i++)
    builder->levels[i] = NULL;
  builder->n_levels = 1;
}

void
meta_region_builder_add_rectangle (MetaRegionBuilder *builder,
                                   int                x,
                                   int                y,
                                   int                width,
                                   int                height)
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
              if (i < META_REGION_BUILDER_MAX_LEVELS)
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
meta_region_builder_finish (MetaRegionBuilder *builder)
{
  MtkRegion *result = NULL;
  int i;

  for (i = 0; i < builder->n_levels; i++)
    {
      if (builder->levels[i])
        {
          if (result == NULL)
            result = builder->levels[i];
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

MtkRegion *
meta_region_transform (const MtkRegion      *region,
                       MetaMonitorTransform  transform,
                       int                   width,
                       int                   height)
{
  int n_rects, i;
  MtkRectangle *rects;
  MtkRegion *transformed_region;

  if (transform == META_MONITOR_TRANSFORM_NORMAL)
    return mtk_region_copy (region);

  n_rects = mtk_region_num_rectangles (region);
  MTK_RECTANGLE_CREATE_ARRAY_SCOPED (n_rects, rects);
  for (i = 0; i < n_rects; i++)
    {
      rects[i] = mtk_region_get_rectangle (region, i);

      meta_rectangle_transform (&rects[i],
                                transform,
                                width,
                                height,
                                &rects[i]);
    }

  transformed_region = mtk_region_create_rectangles (rects, n_rects);

  return transformed_region;
}
