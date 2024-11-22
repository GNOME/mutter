/*
 * Mtk
 *
 * A low-level base library.
 *
 * Copyright (C) 2013 Red Hat Inc.
 * Copyright (C) 2018 Robert Mader
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

#include <glib-object.h>
#include <graphene.h>

#include "mtk/mtk-macros.h"


typedef enum _MtkMonitorTransform
{
  MTK_MONITOR_TRANSFORM_NORMAL,
  MTK_MONITOR_TRANSFORM_90,
  MTK_MONITOR_TRANSFORM_180,
  MTK_MONITOR_TRANSFORM_270,
  MTK_MONITOR_TRANSFORM_FLIPPED,
  MTK_MONITOR_TRANSFORM_FLIPPED_90,
  MTK_MONITOR_TRANSFORM_FLIPPED_180,
  MTK_MONITOR_TRANSFORM_FLIPPED_270,
} MtkMonitorTransform;

#define MTK_MONITOR_N_TRANSFORMS (MTK_MONITOR_TRANSFORM_FLIPPED_270 + 1)
#define MTK_MONITOR_ALL_TRANSFORMS ((1 << MTK_MONITOR_N_TRANSFORMS) - 1)

/* Returns true if transform causes width and height to be inverted
   This is true for the odd transforms in the enum */
static inline gboolean
mtk_monitor_transform_is_rotated (MtkMonitorTransform transform)
{
  return (transform % 2);
}

/* Returns true if transform involves flipping */
static inline gboolean
mtk_monitor_transform_is_flipped (MtkMonitorTransform transform)
{
  return (transform >= MTK_MONITOR_TRANSFORM_FLIPPED);
}

MTK_EXPORT
MtkMonitorTransform mtk_monitor_transform_invert (MtkMonitorTransform transform);

MTK_EXPORT
MtkMonitorTransform mtk_monitor_transform_transform (MtkMonitorTransform transform,
                                                     MtkMonitorTransform other);

MTK_EXPORT
void mtk_monitor_transform_transform_point (MtkMonitorTransform  transform,
                                            int                 *area_width,
                                            int                 *area_height,
                                            int                 *point_x,
                                            int                 *point_y);

MTK_EXPORT
void mtk_monitor_transform_transform_matrix (MtkMonitorTransform  transform,
                                             graphene_matrix_t   *matrix);

MTK_EXPORT
const char * mtk_monitor_transform_to_string (MtkMonitorTransform transform);

MTK_EXPORT
MtkMonitorTransform mtk_monitor_transform_from_string (const char *name);
