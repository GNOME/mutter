/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * Copyright (C) 2019 Endless, Inc
 * Copyright (C) 2009, 2010 Intel Corp
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

#include "clutter/clutter-private.h"
#include "clutter/clutter-types.h"

static gboolean
graphene_matrix_progress (const GValue *a,
                          const GValue *b,
                          double        progress,
                          GValue       *retval)
{
  const graphene_matrix_t *am = g_value_get_boxed (a);
  const graphene_matrix_t *bm = g_value_get_boxed (b);
  graphene_matrix_t res;

  graphene_matrix_interpolate (am, bm, progress, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

static gboolean
graphene_point_progress (const GValue *a,
                         const GValue *b,
                         double        progress,
                         GValue       *retval)
{
  const graphene_point_t *ap = g_value_get_boxed (a);
  const graphene_point_t *bp = g_value_get_boxed (b);
  graphene_point_t res;

  graphene_point_interpolate (ap, bp, progress, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

static gboolean
graphene_point3d_progress (const GValue *a,
                           const GValue *b,
                           double        progress,
                           GValue       *retval)
{
  const graphene_point3d_t *av = g_value_get_boxed (a);
  const graphene_point3d_t *bv = g_value_get_boxed (b);
  graphene_point3d_t res;

  graphene_point3d_interpolate (av, bv, progress, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

static gboolean
graphene_rect_progress (const GValue *a,
                        const GValue *b,
                        double        progress,
                        GValue       *retval)
{
  const graphene_rect_t *rect_a = g_value_get_boxed (a);
  const graphene_rect_t *rect_b = g_value_get_boxed (b);
  graphene_rect_t res;

  graphene_rect_interpolate (rect_a, rect_b, progress, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

static gboolean
graphene_size_progress (const GValue *a,
                        const GValue *b,
                        double        progress,
                        GValue       *retval)
{
  const graphene_size_t *as = g_value_get_boxed (a);
  const graphene_size_t *bs = g_value_get_boxed (b);
  graphene_size_t res;

  graphene_size_interpolate (as, bs, progress, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

static void
cogl_color_interpolate (const CoglColor *initial,
                        const CoglColor *final,
                        gdouble          progress,
                        CoglColor       *result)
{
  g_return_if_fail (initial != NULL);
  g_return_if_fail (final != NULL);
  g_return_if_fail (result != NULL);

  result->red = (uint8_t) (initial->red +
                           (final->red - initial->red) * progress);
  result->green = (uint8_t) (initial->green +
                             (final->green - initial->green) * progress);
  result->blue = (uint8_t) (initial->blue +
                            (final->blue - initial->blue) * progress);
  result->alpha = (uint8_t) (initial->alpha +
                             (final->alpha - initial->alpha) * progress);
}

static gboolean
cogl_color_progress (const GValue *a,
                     const GValue *b,
                     gdouble       progress,
                     GValue       *retval)
{
  const CoglColor *a_color = cogl_value_get_color (a);
  const CoglColor *b_color = cogl_value_get_color (b);
  CoglColor res = { 0, };

  cogl_color_interpolate (a_color, b_color, progress, &res);
  cogl_value_set_color (retval, &res);

  return TRUE;
}

void
clutter_interval_register_progress_funcs (void)
{
  clutter_interval_register_progress_func (GRAPHENE_TYPE_MATRIX,
                                           graphene_matrix_progress);
  clutter_interval_register_progress_func (GRAPHENE_TYPE_POINT,
                                           graphene_point_progress);
  clutter_interval_register_progress_func (GRAPHENE_TYPE_POINT3D,
                                           graphene_point3d_progress);
  clutter_interval_register_progress_func (GRAPHENE_TYPE_RECT,
                                           graphene_rect_progress);
  clutter_interval_register_progress_func (GRAPHENE_TYPE_SIZE,
                                           graphene_size_progress);
  clutter_interval_register_progress_func (COGL_TYPE_COLOR,
                                           cogl_color_progress);
}
