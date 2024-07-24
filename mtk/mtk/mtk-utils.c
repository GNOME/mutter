/*
 * Copyright (C) 2021-2024 Robert Mader <robert.mader@posteo.de>
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

#include "mtk/mtk-utils.h"

void
mtk_compute_viewport_matrix (graphene_matrix_t     *matrix,
                             int                    width,
                             int                    height,
                             float                  scale,
                             MtkMonitorTransform    transform,
                             const graphene_rect_t *src_rect)
{
  if (src_rect)
    {
      float scaled_width;
      float scaled_height;
      graphene_point3d_t p;

      scaled_width = width / scale;
      scaled_height = height / scale;

      graphene_point3d_init (&p,
                             src_rect->origin.x / src_rect->size.width,
                             src_rect->origin.y / src_rect->size.height,
                             0);
      graphene_matrix_translate (matrix, &p);

      if (mtk_monitor_transform_is_rotated (transform))
        {
          graphene_matrix_scale (matrix,
                                 src_rect->size.width / scaled_height,
                                 src_rect->size.height / scaled_width,
                                 1);
        }
      else
        {
          graphene_matrix_scale (matrix,
                                 src_rect->size.width / scaled_width,
                                 src_rect->size.height / scaled_height,
                                 1);
        }
    }

  mtk_monitor_transform_transform_matrix (transform, matrix);
}
