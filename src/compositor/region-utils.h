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

#pragma once

#include <glib.h>

#include "backends/meta-backend-types.h"
#include "clutter/clutter.h"
#include "core/boxes-private.h"

/**
 * MetaRegionIterator:
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
 *  MetaRegionIterator iter;
 *  for (meta_region_iterator_init (&iter, region);
 *       !meta_region_iterator_at_end (&iter);
 *       meta_region_iterator_next (&iter))
 *  {
 *    [ Use iter.rectangle, iter.line_start, iter.line_end ]
 *  }
 */
typedef struct _MetaRegionIterator MetaRegionIterator;

struct _MetaRegionIterator {
  MtkRegion *region;
  MtkRectangle rectangle;
  gboolean line_start;
  gboolean line_end;
  int i;

  /*< private >*/
  int n_rectangles;
  MtkRectangle next_rectangle;
};

typedef struct _MetaRegionBuilder MetaRegionBuilder;

#define META_REGION_BUILDER_MAX_LEVELS 16
struct _MetaRegionBuilder {
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
  MtkRegion *levels[META_REGION_BUILDER_MAX_LEVELS];
  int n_levels;
};

void     meta_region_builder_init       (MetaRegionBuilder *builder);
void     meta_region_builder_add_rectangle (MetaRegionBuilder *builder,
                                            int                x,
                                            int                y,
                                            int                width,
                                            int                height);
MtkRegion * meta_region_builder_finish (MetaRegionBuilder *builder);

void     meta_region_iterator_init      (MetaRegionIterator *iter,
                                         MtkRegion          *region);
gboolean meta_region_iterator_at_end    (MetaRegionIterator *iter);
void     meta_region_iterator_next      (MetaRegionIterator *iter);

MtkRegion * meta_region_scale (MtkRegion *region,
                               int        scale);

MtkRegion * meta_make_border_region (MtkRegion *region,
                                     int        x_amount,
                                     int        y_amount,
                                     gboolean   flip);

MtkRegion * meta_region_transform (const MtkRegion      *region,
                                   MetaMonitorTransform  transform,
                                   int                   width,
                                   int                   height);

MtkRegion * meta_region_crop_and_scale (MtkRegion       *region,
                                        graphene_rect_t *src_rect,
                                        int              dst_width,
                                        int              dst_height);


MtkRegion *
meta_region_apply_matrix_transform_expand (const MtkRegion   *region,
                                           graphene_matrix_t *transform);
