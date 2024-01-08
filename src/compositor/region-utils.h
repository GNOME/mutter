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
