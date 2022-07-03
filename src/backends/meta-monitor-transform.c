/*
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

#include "config.h"

#include "backends/meta-monitor-transform.h"

MetaMonitorTransform
meta_monitor_transform_from_orientation (MetaOrientation orientation)
{
  switch (orientation)
    {
    case META_ORIENTATION_BOTTOM_UP:
      return META_MONITOR_TRANSFORM_180;
    case META_ORIENTATION_LEFT_UP:
      return META_MONITOR_TRANSFORM_90;
    case META_ORIENTATION_RIGHT_UP:
      return META_MONITOR_TRANSFORM_270;
    case META_ORIENTATION_UNDEFINED:
    case META_ORIENTATION_NORMAL:
    default:
      return META_MONITOR_TRANSFORM_NORMAL;
    }
}

MetaMonitorTransform
meta_monitor_transform_invert (MetaMonitorTransform transform)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_90:
      return META_MONITOR_TRANSFORM_270;
    case META_MONITOR_TRANSFORM_270:
      return META_MONITOR_TRANSFORM_90;
    case META_MONITOR_TRANSFORM_NORMAL:
    case META_MONITOR_TRANSFORM_180:
    case META_MONITOR_TRANSFORM_FLIPPED:
    case META_MONITOR_TRANSFORM_FLIPPED_90:
    case META_MONITOR_TRANSFORM_FLIPPED_180:
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      return transform;
    }
  g_assert_not_reached ();
  return 0;
}

static MetaMonitorTransform
meta_monitor_transform_flip (MetaMonitorTransform transform)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_NORMAL:
      return META_MONITOR_TRANSFORM_FLIPPED;
    case META_MONITOR_TRANSFORM_90:
      return META_MONITOR_TRANSFORM_FLIPPED_270;
    case META_MONITOR_TRANSFORM_180:
      return META_MONITOR_TRANSFORM_FLIPPED_180;
    case META_MONITOR_TRANSFORM_270:
      return META_MONITOR_TRANSFORM_FLIPPED_90;
    case META_MONITOR_TRANSFORM_FLIPPED:
      return META_MONITOR_TRANSFORM_NORMAL;
    case META_MONITOR_TRANSFORM_FLIPPED_90:
      return META_MONITOR_TRANSFORM_270;
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      return META_MONITOR_TRANSFORM_180;
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      return META_MONITOR_TRANSFORM_90;
    }
  g_assert_not_reached ();
  return 0;
}

MetaMonitorTransform
meta_monitor_transform_transform (MetaMonitorTransform transform,
                                  MetaMonitorTransform other)
{
  MetaMonitorTransform new_transform;
  gboolean needs_flip = FALSE;

  if (meta_monitor_transform_is_flipped (other))
    new_transform = meta_monitor_transform_flip (transform);
  else
    new_transform = transform;

  if (meta_monitor_transform_is_flipped (new_transform))
    needs_flip = TRUE;

  new_transform += other;
  new_transform %= META_MONITOR_TRANSFORM_FLIPPED;

  if (needs_flip)
    new_transform += META_MONITOR_TRANSFORM_FLIPPED;

  return new_transform;
}

void
meta_monitor_transform_transform_point (MetaMonitorTransform  transform,
                                        int                   area_width,
                                        int                   area_height,
                                        int                   x,
                                        int                   y,
                                        int                  *out_x,
                                        int                  *out_y)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_NORMAL:
      *out_x = x;
      *out_y = y;
      break;
    case META_MONITOR_TRANSFORM_90:
      *out_x = area_width - y;
      *out_y = x;
      break;
    case META_MONITOR_TRANSFORM_180:
      *out_x = area_width - x;
      *out_y = area_height - y;
      break;
    case META_MONITOR_TRANSFORM_270:
      *out_x = y,
      *out_y = area_height - x;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED:
      *out_x = area_width - x;
      *out_y = y;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_90:
      *out_x = area_width - y;
      *out_y = area_height - x;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      *out_x = x;
      *out_y = area_height - y;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      *out_x = y;
      *out_y = x;
      break;
    }
}

void
meta_monitor_transform_transform_matrix (MetaMonitorTransform  transform,
                                         graphene_matrix_t    *matrix)
{
  graphene_euler_t euler;

  if (transform == META_MONITOR_TRANSFORM_NORMAL)
    return;

  graphene_matrix_translate (matrix,
                             &GRAPHENE_POINT3D_INIT (-0.5, -0.5, 0.0));
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_90:
      graphene_euler_init_with_order (&euler, 0.0, 0.0, 270.0,
                                      GRAPHENE_EULER_ORDER_SYXZ);
      break;
    case META_MONITOR_TRANSFORM_180:
      graphene_euler_init_with_order (&euler, 0.0, 0.0, 180.0,
                                      GRAPHENE_EULER_ORDER_SYXZ);
      break;
    case META_MONITOR_TRANSFORM_270:
      graphene_euler_init_with_order (&euler, 0.0, 0.0, 90.0,
                                      GRAPHENE_EULER_ORDER_SYXZ);
      break;
    case META_MONITOR_TRANSFORM_FLIPPED:
      graphene_euler_init_with_order (&euler, 0.0, 180.0, 0.0,
                                      GRAPHENE_EULER_ORDER_SYXZ);
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_90:
      graphene_euler_init_with_order (&euler, 0.0, 180.0, 90.0,
                                      GRAPHENE_EULER_ORDER_SYXZ);
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      graphene_euler_init_with_order (&euler, 0.0, 180.0, 180.0,
                                      GRAPHENE_EULER_ORDER_SYXZ);
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      graphene_euler_init_with_order (&euler, 0.0, 180.0, 270.0,
                                      GRAPHENE_EULER_ORDER_SYXZ);
      break;
    case META_MONITOR_TRANSFORM_NORMAL:
      g_assert_not_reached ();
    }
  graphene_matrix_rotate_euler (matrix, &euler);
  graphene_matrix_translate (matrix,
                             &GRAPHENE_POINT3D_INIT (0.5, 0.5, 0.0));
}
