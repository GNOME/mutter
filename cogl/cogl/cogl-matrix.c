/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009,2010,2011 Intel Corporation.
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 *   Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 */

#include "cogl-config.h"

#include <cogl-util.h>
#include <cogl-debug.h>
#include <cogl-matrix.h>
#include <cogl-matrix-private.h>

#include <glib.h>
#include <math.h>
#include <string.h>

void
cogl_matrix_multiply (graphene_matrix_t       *result,
                      const graphene_matrix_t *a,
                      const graphene_matrix_t *b)
{
  graphene_matrix_multiply (b, a, result);
  _COGL_MATRIX_DEBUG_PRINT (result);
}

void
_cogl_matrix_prefix_print (const char              *prefix,
                           const graphene_matrix_t *matrix)
{
  graphene_matrix_print (matrix);
}

/*
 * Dumps the contents of a graphene_matrix_t structure.
 */
void
cogl_debug_matrix_print (const graphene_matrix_t *matrix)
{
  _cogl_matrix_prefix_print ("", matrix);
}

void
cogl_matrix_rotate (graphene_matrix_t *matrix,
                    float              angle,
                    float              x,
                    float              y,
                    float              z)
{
  graphene_matrix_t rotation;
  graphene_vec3_t axis;

  graphene_vec3_init (&axis, x, y, z);
  graphene_matrix_init_rotate (&rotation, angle, &axis);
  graphene_matrix_multiply (&rotation, matrix, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_rotate_euler (graphene_matrix_t      *matrix,
                          const graphene_euler_t *euler)
{
  graphene_matrix_t rotation_transform;

  cogl_matrix_init_from_euler (&rotation_transform, euler);
  cogl_matrix_multiply (matrix, matrix, &rotation_transform);
}

void
cogl_matrix_frustum (graphene_matrix_t *matrix,
                     float              left,
                     float              right,
                     float              bottom,
                     float              top,
                     float              z_near,
                     float              z_far)
{
  graphene_matrix_t frustum;

  graphene_matrix_init_frustum (&frustum,
                                left, right,
                                bottom, top,
                                z_near, z_far);
  graphene_matrix_multiply (&frustum, matrix, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_perspective (graphene_matrix_t *matrix,
                         float              fov_y,
                         float              aspect,
                         float              z_near,
                         float              z_far)
{
  float ymax = z_near * tan (fov_y * G_PI / 360.0);

  cogl_matrix_frustum (matrix,
                       -ymax * aspect,  /* left */
                       ymax * aspect,   /* right */
                       -ymax,           /* bottom */
                       ymax,            /* top */
                       z_near,
                       z_far);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_orthographic (graphene_matrix_t *matrix,
                          float              left,
                          float              bottom,
                          float              right,
                          float              top,
                          float              near,
                          float              far)
{
  graphene_matrix_t ortho;

  graphene_matrix_init_ortho (&ortho,
                              left, right,
                              top, bottom,
                              near, far);
  graphene_matrix_multiply (&ortho, matrix, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_scale (graphene_matrix_t *matrix,
                   float              sx,
                   float              sy,
                   float              sz)
{
  graphene_matrix_t scale;

  graphene_matrix_init_scale (&scale, sx, sy, sz);
  graphene_matrix_multiply (&scale, matrix, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_translate (graphene_matrix_t *matrix,
                       float              x,
                       float              y,
                       float              z)
{
  graphene_matrix_t translation;

  graphene_matrix_init_translate (&translation,
                                  &GRAPHENE_POINT3D_INIT (x, y, z));
  graphene_matrix_multiply (&translation, matrix, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_init_identity (graphene_matrix_t *matrix)
{
  graphene_matrix_init_identity (matrix);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_init_translation (graphene_matrix_t *matrix,
                              float              tx,
                              float              ty,
                              float              tz)
{
  graphene_matrix_init_translate (matrix, &GRAPHENE_POINT3D_INIT (tx, ty, tz));
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

/*
 * Loads a matrix array into graphene_matrix_t.
 *
 * @m matrix array.
 * @mat matrix.
 *
 * Copies \p m into graphene_matrix_t::m and marks the MAT_FLAG_GENERAL and
 * MAT_DIRTY_ALL
 * flags.
 */
static void
_cogl_matrix_init_from_array (graphene_matrix_t *matrix,
                              const float       *array)
{
  graphene_matrix_init_from_float (matrix, array);
}

void
cogl_matrix_init_from_array (graphene_matrix_t *matrix,
                             const float       *array)
{
  _cogl_matrix_init_from_array (matrix, array);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_init_from_matrix (graphene_matrix_t       *matrix,
                              const graphene_matrix_t *source)
{
  memcpy (matrix, source, sizeof (graphene_matrix_t));
}

void
_cogl_matrix_init_from_matrix_without_inverse (graphene_matrix_t       *matrix,
                                               const graphene_matrix_t *src)
{
  graphene_matrix_init_from_matrix (matrix, src);
}

void
cogl_matrix_init_from_euler (graphene_matrix_t      *matrix,
                             const graphene_euler_t *euler)
{
  graphene_matrix_init_identity (matrix);
  graphene_matrix_rotate_euler (matrix, euler);
}

void
cogl_matrix_view_2d_in_frustum (graphene_matrix_t *matrix,
                                float              left,
                                float              right,
                                float              bottom,
                                float              top,
                                float              z_near,
                                float              z_2d,
                                float              width_2d,
                                float              height_2d)
{
  float left_2d_plane = left / z_near * z_2d;
  float right_2d_plane = right / z_near * z_2d;
  float bottom_2d_plane = bottom / z_near * z_2d;
  float top_2d_plane = top / z_near * z_2d;

  float width_2d_start = right_2d_plane - left_2d_plane;
  float height_2d_start = top_2d_plane - bottom_2d_plane;

  /* Factors to scale from framebuffer geometry to frustum
   * cross-section geometry. */
  float width_scale = width_2d_start / width_2d;
  float height_scale = height_2d_start / height_2d;

  cogl_matrix_translate (matrix,
                         left_2d_plane, top_2d_plane, -z_2d);

  cogl_matrix_scale (matrix, width_scale, -height_scale, width_scale);
}

/* Assuming a symmetric perspective matrix is being used for your
 * projective transform this convenience function lets you compose a
 * view transform such that geometry on the z=0 plane will map to
 * screen coordinates with a top left origin of (0,0) and with the
 * given width and height.
 */
void
cogl_matrix_view_2d_in_perspective (graphene_matrix_t *matrix,
                                    float              fov_y,
                                    float              aspect,
                                    float              z_near,
                                    float              z_2d,
                                    float              width_2d,
                                    float              height_2d)
{
  float top = z_near * tan (fov_y * G_PI / 360.0);
  cogl_matrix_view_2d_in_frustum (matrix,
                                  -top * aspect,
                                  top * aspect,
                                  -top,
                                  top,
                                  z_near,
                                  z_2d,
                                  width_2d,
                                  height_2d);
}

gboolean
cogl_matrix_equal (const void *v1, const void *v2)
{
  const graphene_matrix_t *a = v1;
  const graphene_matrix_t *b = v2;

  g_return_val_if_fail (v1 != NULL, FALSE);
  g_return_val_if_fail (v2 != NULL, FALSE);

  return graphene_matrix_equal_fast (a, b);
}

graphene_matrix_t *
cogl_matrix_copy (const graphene_matrix_t *matrix)
{
  if (G_LIKELY (matrix))
    return g_slice_dup (graphene_matrix_t, matrix);

  return NULL;
}

void
cogl_matrix_free (graphene_matrix_t *matrix)
{
  g_slice_free (graphene_matrix_t, matrix);
}

void
cogl_matrix_to_float (const graphene_matrix_t *matrix,
                      float                   *out_array)
{
  graphene_matrix_to_float (matrix, out_array);
}

float
cogl_matrix_get_value (const graphene_matrix_t *matrix,
                       unsigned int             row,
                       unsigned int             column)
{
  return graphene_matrix_get_value (matrix, column, row);
}

gboolean
cogl_matrix_is_identity (const graphene_matrix_t *matrix)
{
  return graphene_matrix_is_identity (matrix);
}

void
cogl_matrix_look_at (graphene_matrix_t *matrix,
                     float              eye_position_x,
                     float              eye_position_y,
                     float              eye_position_z,
                     float              object_x,
                     float              object_y,
                     float              object_z,
                     float              world_up_x,
                     float              world_up_y,
                     float              world_up_z)
{
  graphene_vec3_t eye;
  graphene_vec3_t center;
  graphene_vec3_t up;
  graphene_matrix_t look_at;

  graphene_vec3_init (&eye, eye_position_x, eye_position_y, eye_position_z);
  graphene_vec3_init (&center, object_x, object_y, object_z);
  graphene_vec3_init (&up, world_up_x, world_up_y, world_up_z);

  graphene_matrix_init_look_at (&look_at, &eye, &center, &up);

  cogl_matrix_multiply (matrix, matrix, &look_at);
}

void
cogl_matrix_transpose (graphene_matrix_t *matrix)
{
  /* We don't need to do anything if the matrix is the identity matrix */
  if (graphene_matrix_is_identity (matrix))
    return;

  graphene_matrix_transpose (matrix, matrix);
}

void
cogl_matrix_skew_xy (graphene_matrix_t *matrix,
                     float              factor)
{
  graphene_matrix_t skew;

  graphene_matrix_init_identity (&skew);
  graphene_matrix_skew_xy (&skew, factor);
  graphene_matrix_multiply (&skew, matrix, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_skew_xz (graphene_matrix_t *matrix,
                     float              factor)
{
  graphene_matrix_t skew;

  graphene_matrix_init_identity (&skew);
  graphene_matrix_skew_xz (&skew, factor);
  graphene_matrix_multiply (&skew, matrix, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_skew_yz (graphene_matrix_t *matrix,
                     float              factor)
{
  graphene_matrix_t skew;

  graphene_matrix_init_identity (&skew);
  graphene_matrix_skew_yz (&skew, factor);
  graphene_matrix_multiply (&skew, matrix, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}
