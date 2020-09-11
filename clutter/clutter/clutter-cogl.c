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
  graphene_matrix_t interpolated;
  CoglMatrix res;
  float v[16];

  graphene_matrix_interpolate (cogl_matrix_get_graphene_matrix (matrix1),
                               cogl_matrix_get_graphene_matrix (matrix2),
                               progress,
                               &interpolated);
  graphene_matrix_to_float (&interpolated, v);

  cogl_matrix_init_from_array (&res, v);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

void
clutter_cogl_init (void)
{
  clutter_interval_register_progress_func (COGL_GTYPE_TYPE_MATRIX,
                                           cogl_matrix_progress);
}
