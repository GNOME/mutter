/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
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

/**
 * SECTION:clutter-geometric-types
 * @Title: Base geometric types
 * @Short_Description: Common geometric data types used by Clutter
 *
 * Clutter defines a set of geometric data structures that are commonly used
 * across the whole API.
 */

#include "clutter-build-config.h"

#include "clutter-types.h"
#include "clutter-private.h"

#include <math.h>

#define FLOAT_EPSILON   (1e-15)



/*
 * ClutterMargin
 */

/**
 * clutter_margin_new:
 *
 * Creates a new #ClutterMargin.
 *
 * Return value: (transfer full): a newly allocated #ClutterMargin. Use
 *   clutter_margin_free() to free the resources associated with it when
 *   done.
 *
 * Since: 1.10
 */
ClutterMargin *
clutter_margin_new (void)
{
  return g_slice_new0 (ClutterMargin);
}

/**
 * clutter_margin_copy:
 * @margin_: a #ClutterMargin
 *
 * Creates a new #ClutterMargin and copies the contents of @margin_ into
 * the newly created structure.
 *
 * Return value: (transfer full): a copy of the #ClutterMargin.
 *
 * Since: 1.10
 */
ClutterMargin *
clutter_margin_copy (const ClutterMargin *margin_)
{
  if (G_LIKELY (margin_ != NULL))
    return g_slice_dup (ClutterMargin, margin_);

  return NULL;
}

/**
 * clutter_margin_free:
 * @margin_: a #ClutterMargin
 *
 * Frees the resources allocated by clutter_margin_new() and
 * clutter_margin_copy().
 *
 * Since: 1.10
 */
void
clutter_margin_free (ClutterMargin *margin_)
{
  if (G_LIKELY (margin_ != NULL))
    g_slice_free (ClutterMargin, margin_);
}

G_DEFINE_BOXED_TYPE (ClutterMargin, clutter_margin,
                     clutter_margin_copy,
                     clutter_margin_free)

/**
 * ClutterMatrix:
 *
 * A type representing a 4x4 matrix.
 *
 * It is identicaly to #CoglMatrix.
 *
 * Since: 1.12
 */

static gpointer
clutter_matrix_copy (gpointer data)
{
  return cogl_matrix_copy (data);
}

static gboolean
clutter_matrix_progress (const GValue *a,
                         const GValue *b,
                         gdouble       progress,
                         GValue       *retval)
{
  const ClutterMatrix *matrix1 = g_value_get_boxed (a);
  const ClutterMatrix *matrix2 = g_value_get_boxed (b);
  graphene_matrix_t m1, m2, m;
  ClutterMatrix res;

  cogl_matrix_to_graphene_matrix (matrix1, &m1);
  cogl_matrix_to_graphene_matrix (matrix2, &m2);

  graphene_matrix_interpolate (&m1, &m2, progress, &m);

  graphene_matrix_to_cogl_matrix (&m, &res);

  g_value_set_boxed (retval, &res);

  return TRUE;
}

G_DEFINE_BOXED_TYPE_WITH_CODE (ClutterMatrix, clutter_matrix,
                               clutter_matrix_copy,
                               clutter_matrix_free,
                               CLUTTER_REGISTER_INTERVAL_PROGRESS (clutter_matrix_progress))

/**
 * clutter_matrix_alloc:
 *
 * Allocates enough memory to hold a #ClutterMatrix.
 *
 * Return value: (transfer full): the newly allocated #ClutterMatrix
 *
 * Since: 1.12
 */
ClutterMatrix *
clutter_matrix_alloc (void)
{
  return g_new0 (ClutterMatrix, 1);
}

/**
 * clutter_matrix_free:
 * @matrix: (allow-none): a #ClutterMatrix
 *
 * Frees the memory allocated by clutter_matrix_alloc().
 *
 * Since: 1.12
 */
void
clutter_matrix_free (ClutterMatrix *matrix)
{
  cogl_matrix_free (matrix);
}

/**
 * clutter_matrix_init_identity:
 * @matrix: a #ClutterMatrix
 *
 * Initializes @matrix with the identity matrix, i.e.:
 *
 * |[
 *   .xx = 1.0, .xy = 0.0, .xz = 0.0, .xw = 0.0
 *   .yx = 0.0, .yy = 1.0, .yz = 0.0, .yw = 0.0
 *   .zx = 0.0, .zy = 0.0, .zz = 1.0, .zw = 0.0
 *   .wx = 0.0, .wy = 0.0, .wz = 0.0, .ww = 1.0
 * ]|
 *
 * Return value: (transfer none): the initialized #ClutterMatrix
 *
 * Since: 1.12
 */
ClutterMatrix *
clutter_matrix_init_identity (ClutterMatrix *matrix)
{
  cogl_matrix_init_identity (matrix);

  return matrix;
}

/**
 * clutter_matrix_init_from_array:
 * @matrix: a #ClutterMatrix
 * @values: (array fixed-size=16): a C array of 16 floating point values,
 *   representing a 4x4 matrix, with column-major order
 *
 * Initializes @matrix with the contents of a C array of floating point
 * values.
 *
 * Return value: (transfer none): the initialzed #ClutterMatrix
 *
 * Since: 1.12
 */
ClutterMatrix *
clutter_matrix_init_from_array (ClutterMatrix *matrix,
                                const float    values[16])
{
  cogl_matrix_init_from_array (matrix, values);

  return matrix;
}

/**
 * clutter_matrix_init_from_matrix:
 * @a: the #ClutterMatrix to initialize
 * @b: the #ClutterMatrix to copy
 *
 * Initializes the #ClutterMatrix @a with the contents of the
 * #ClutterMatrix @b.
 *
 * Return value: (transfer none): the initialized #ClutterMatrix
 *
 * Since: 1.12
 */
ClutterMatrix *
clutter_matrix_init_from_matrix (ClutterMatrix       *a,
                                 const ClutterMatrix *b)
{
  return memcpy (a, b, sizeof (ClutterMatrix));
}
