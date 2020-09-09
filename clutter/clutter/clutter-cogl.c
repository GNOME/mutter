/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * Copyright (C) 2009, 2010 Intel Corp
 * Copyright (C) 2020 Endless OS Foundation LLC
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

#include "clutter-build-config.h"

#include "clutter-cogl.h"

#include "clutter-private.h"
#include "clutter-types.h"

static gboolean
cogl_matrix_progress (const GValue *a,
                      const GValue *b,
                      gdouble       progress,
                      GValue       *retval)
{
  const CoglMatrix *matrix1 = g_value_get_boxed (a);
  const CoglMatrix *matrix2 = g_value_get_boxed (b);
  graphene_point3d_t scale1 = GRAPHENE_POINT3D_INIT (1.f, 1.f, 1.f);
  float shear1[3] = { 0.f, 0.f, 0.f };
  graphene_point3d_t rotate1 = GRAPHENE_POINT3D_INIT_ZERO;
  graphene_point3d_t translate1 = GRAPHENE_POINT3D_INIT_ZERO;
  graphene_vec4_t perspective1;
  graphene_point3d_t scale2 = GRAPHENE_POINT3D_INIT (1.f, 1.f, 1.f);
  float shear2[3] = { 0.f, 0.f, 0.f };
  graphene_point3d_t rotate2 = GRAPHENE_POINT3D_INIT_ZERO;
  graphene_point3d_t translate2 = GRAPHENE_POINT3D_INIT_ZERO;
  graphene_vec4_t perspective2;
  graphene_point3d_t scale_res = GRAPHENE_POINT3D_INIT (1.f, 1.f, 1.f);
  float shear_res = 0.f;
  graphene_point3d_t rotate_res = GRAPHENE_POINT3D_INIT_ZERO;
  graphene_point3d_t translate_res = GRAPHENE_POINT3D_INIT_ZERO;
  graphene_vec4_t perspective_res;
  CoglMatrix res;

  cogl_matrix_init_identity (&res);

  _clutter_util_matrix_decompose (matrix1,
                                  &scale1, shear1, &rotate1, &translate1,
                                  &perspective1);
  _clutter_util_matrix_decompose (matrix2,
                                  &scale2, shear2, &rotate2, &translate2,
                                  &perspective2);

  /* perspective */
  graphene_vec4_interpolate (&perspective1, &perspective2, progress, &perspective_res);
  res.wx = graphene_vec4_get_x (&perspective_res);
  res.wy = graphene_vec4_get_y (&perspective_res);
  res.wz = graphene_vec4_get_z (&perspective_res);
  res.ww = graphene_vec4_get_w (&perspective_res);

  /* translation */
  graphene_point3d_interpolate (&translate1, &translate2, progress, &translate_res);
  cogl_matrix_translate (&res, translate_res.x, translate_res.y, translate_res.z);

  /* rotation */
  graphene_point3d_interpolate (&rotate1, &rotate2, progress, &rotate_res);
  cogl_matrix_rotate (&res, rotate_res.x, 1.0f, 0.0f, 0.0f);
  cogl_matrix_rotate (&res, rotate_res.y, 0.0f, 1.0f, 0.0f);
  cogl_matrix_rotate (&res, rotate_res.z, 0.0f, 0.0f, 1.0f);

  /* skew */
  shear_res = shear1[2] + (shear2[2] - shear1[2]) * progress; /* YZ */
  if (shear_res != 0.f)
    cogl_matrix_skew_yz (&res, shear_res);

  shear_res = shear1[1] + (shear2[1] - shear1[1]) * progress; /* XZ */
  if (shear_res != 0.f)
    cogl_matrix_skew_xz (&res, shear_res);

  shear_res = shear1[0] + (shear2[0] - shear1[0]) * progress; /* XY */
  if (shear_res != 0.f)
    cogl_matrix_skew_xy (&res, shear_res);

  /* scale */
  graphene_point3d_interpolate (&scale1, &scale2, progress, &scale_res);
  cogl_matrix_scale (&res, scale_res.x, scale_res.y, scale_res.z);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

void
clutter_cogl_init (void)
{
  clutter_interval_register_progress_func (COGL_GTYPE_TYPE_MATRIX,
                                           cogl_matrix_progress);
}
