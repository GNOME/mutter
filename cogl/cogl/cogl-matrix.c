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
 */
/*
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Note: a lot of this code is based on code that was taken from Mesa.
 *
 * Changes compared to the original code from Mesa:
 *
 * - instead of allocating matrix->m and matrix->inv using malloc, our
 *   public CoglMatrix typedef is large enough to directly contain the
 *   matrix, its inverse, a type and a set of flags.
 * - instead of having a _cogl_matrix_analyse which updates the type,
 *   flags and inverse, we have _cogl_matrix_update_inverse which
 *   essentially does the same thing (internally making use of
 *   _cogl_matrix_update_type_and_flags()) but with additional guards in
 *   place to bail out when the inverse matrix is still valid.
 * - when initializing a matrix with the identity matrix we don't
 *   immediately initialize the inverse matrix; rather we just set the
 *   dirty flag for the inverse (since it's likely the user won't request
 *   the inverse of the identity matrix)
 */

#include "cogl-config.h"

#include <cogl-util.h>
#include <cogl-debug.h>
#include <cogl-matrix.h>
#include <cogl-matrix-private.h>
#include <cogl-graphene-utils.h>

#include <glib.h>
#include <math.h>
#include <string.h>

#include <cogl-gtype-private.h>
COGL_GTYPE_DEFINE_BOXED (Matrix, matrix,
                         cogl_matrix_copy,
                         cogl_matrix_free);

#define DEG2RAD (G_PI/180.0)

/*
 * Identity matrix.
 */
static float identity[16] = {
   1.0, 0.0, 0.0, 0.0,
   0.0, 1.0, 0.0, 0.0,
   0.0, 0.0, 1.0, 0.0,
   0.0, 0.0, 0.0, 1.0
};

void
cogl_matrix_multiply (CoglMatrix *result,
		      const CoglMatrix *a,
		      const CoglMatrix *b)
{
  graphene_matrix_t m1, m2, res;

  cogl_matrix_to_graphene_matrix (a, &m1);
  cogl_matrix_to_graphene_matrix (b, &m2);

  graphene_matrix_multiply (&m2, &m1, &res);
  graphene_matrix_to_cogl_matrix (&res, result);

  _COGL_MATRIX_DEBUG_PRINT (result);
}

void
_cogl_matrix_prefix_print (const char *prefix, const CoglMatrix *matrix)
{
  float *m = (float *) matrix;
  int i;

  for (i = 0;i < 4; i++)
    g_print ("%s\t%f %f %f %f\n", prefix, m[i*4], m[i*4+1], m[i*4+2], m[i*4+3] );
}

/*
 * Dumps the contents of a CoglMatrix structure.
 */
void
cogl_debug_matrix_print (const CoglMatrix *matrix)
{
  _cogl_matrix_prefix_print ("", matrix);
}

gboolean
cogl_matrix_get_inverse (const CoglMatrix *matrix, CoglMatrix *inverse)
{
  graphene_matrix_t m;
  gboolean success;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  success = graphene_matrix_inverse (&m, &m);

  if (!success)
    graphene_matrix_init_identity (&m);

  graphene_matrix_to_cogl_matrix (&m, inverse);
  return success;
}

void
cogl_matrix_rotate (CoglMatrix *matrix,
		    float angle,
		    float x,
		    float y,
		    float z)
{
  graphene_matrix_t m;
  graphene_vec3_t r;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_rotate (&m, angle, graphene_vec3_init (&r, x, y, z));
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_rotate_quaternion (CoglMatrix *matrix,
                               const graphene_quaternion_t *quaternion)
{
  CoglMatrix rotation_transform;

  cogl_matrix_init_from_quaternion (&rotation_transform, quaternion);
  cogl_matrix_multiply (matrix, matrix, &rotation_transform);
}

void
cogl_matrix_rotate_euler (CoglMatrix *matrix,
                          const graphene_euler_t *euler)
{
  CoglMatrix rotation_transform;

  cogl_matrix_init_from_euler (&rotation_transform, euler);
  cogl_matrix_multiply (matrix, matrix, &rotation_transform);
}

void
cogl_matrix_frustum (CoglMatrix *matrix,
                     float       left,
                     float       right,
                     float       bottom,
                     float       top,
                     float       z_near,
                     float       z_far)
{
  graphene_matrix_t frustum, m;

  graphene_matrix_init_frustum (&frustum, left, right, bottom, top, z_near, z_far);

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_multiply (&m, &frustum, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_perspective (CoglMatrix *matrix,
                         float       fov_y,
                         float       aspect,
                         float       z_near,
                         float       z_far)
{
  graphene_matrix_t perspective, m;

  graphene_matrix_init_perspective (&perspective, fov_y, aspect, z_near, z_far);

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_multiply (&m, &perspective, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_orthographic (CoglMatrix *matrix,
                          float x_1,
                          float y_1,
                          float x_2,
                          float y_2,
                          float near,
                          float far)
{
  graphene_matrix_t ortho, m;

  graphene_matrix_init_ortho (&ortho, x_1, x_2, y_2, y_1, near, far);

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_multiply (&m, &ortho, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_scale (CoglMatrix *matrix,
		   float sx,
		   float sy,
		   float sz)
{
  graphene_matrix_t m;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_scale (&m, sx, sy, sz);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_translate (CoglMatrix *matrix,
		       float x,
		       float y,
		       float z)
{
  graphene_matrix_t m;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_translate (&m, &GRAPHENE_POINT3D_INIT (x, y, z));
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

/*
 * Set a matrix to the identity matrix.
 *
 * @mat matrix.
 *
 * Copies ::identity into \p CoglMatrix::m, and into CoglMatrix::inv if
 * not NULL. Sets the matrix type to identity, resets the flags. It
 * doesn't initialize the inverse matrix, it just marks it dirty.
 */
static void
_cogl_matrix_init_identity (CoglMatrix *matrix)
{
  memcpy (matrix, identity, 16 * sizeof (float));
}

void
cogl_matrix_init_identity (CoglMatrix *matrix)
{
  _cogl_matrix_init_identity (matrix);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_init_translation (CoglMatrix *matrix,
                              float       tx,
                              float       ty,
                              float       tz)
{
  graphene_matrix_t m;

  graphene_matrix_init_translate (&m, &GRAPHENE_POINT3D_INIT (tx, ty, tz));
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

/*
 * Loads a matrix array into CoglMatrix.
 *
 * @m matrix array.
 * @mat matrix.
 *
 * Copies \p m into CoglMatrix::m.
 */
static void
_cogl_matrix_init_from_array (CoglMatrix *matrix, const float *array)
{
  memcpy (matrix, array, 16 * sizeof (float));
}

void
cogl_matrix_init_from_array (CoglMatrix *matrix, const float *array)
{
  _cogl_matrix_init_from_array (matrix, array);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
_cogl_matrix_init_from_matrix_without_inverse (CoglMatrix *matrix,
                                               const CoglMatrix *src)
{
  memcpy (matrix, src, 16 * sizeof (float));
}

void
cogl_matrix_init_from_quaternion (CoglMatrix *matrix,
                                  const graphene_quaternion_t *quaternion)
{
  graphene_matrix_t m;

  graphene_quaternion_to_matrix (quaternion, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);
}

void
cogl_matrix_init_from_euler (CoglMatrix *matrix,
                             const graphene_euler_t *euler)
{
  graphene_matrix_t m;

  graphene_euler_to_matrix (euler, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);
}

void
cogl_matrix_view_2d_in_frustum (CoglMatrix *matrix,
                                float left,
                                float right,
                                float bottom,
                                float top,
                                float z_near,
                                float z_2d,
                                float width_2d,
                                float height_2d)
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

  cogl_matrix_scale (matrix, width_scale, -height_scale, width_scale);
  cogl_matrix_translate (matrix,
                         left_2d_plane, top_2d_plane, -z_2d);
}

/* Assuming a symmetric perspective matrix is being used for your
 * projective transform this convenience function lets you compose a
 * view transform such that geometry on the z=0 plane will map to
 * screen coordinates with a top left origin of (0,0) and with the
 * given width and height.
 */
void
cogl_matrix_view_2d_in_perspective (CoglMatrix *matrix,
                                    float fov_y,
                                    float aspect,
                                    float z_near,
                                    float z_2d,
                                    float width_2d,
                                    float height_2d)
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
  const CoglMatrix *a = v1;
  const CoglMatrix *b = v2;
  graphene_matrix_t ga, gb;

  _COGL_RETURN_VAL_IF_FAIL (v1 != NULL, FALSE);
  _COGL_RETURN_VAL_IF_FAIL (v2 != NULL, FALSE);

  cogl_matrix_to_graphene_matrix (a, &ga);
  cogl_matrix_to_graphene_matrix (b, &gb);

  return cogl_graphene_matrix_equal (&ga, &gb);
}

CoglMatrix *
cogl_matrix_copy (const CoglMatrix *matrix)
{
  if (G_LIKELY (matrix))
    return g_slice_dup (CoglMatrix, matrix);

  return NULL;
}

void
cogl_matrix_free (CoglMatrix *matrix)
{
  g_slice_free (CoglMatrix, matrix);
}

void
cogl_matrix_get_array (const CoglMatrix *matrix,
                       float *array)
{
  memcpy (array, matrix, sizeof (float) * 16);
}

void
cogl_matrix_transform_point (const CoglMatrix *matrix,
                             float *x,
                             float *y,
                             float *z,
                             float *w)
{
  float _x = *x, _y = *y, _z = *z, _w = *w;

  *x = matrix->xx * _x + matrix->yx * _y + matrix->zx * _z + matrix->wx * _w;
  *y = matrix->xy * _x + matrix->yy * _y + matrix->zy * _z + matrix->wy * _w;
  *z = matrix->xz * _x + matrix->yz * _y + matrix->zz * _z + matrix->wz * _w;
  *w = matrix->xw * _x + matrix->yw * _y + matrix->zw * _z + matrix->ww * _w;
}

typedef struct _Point2f
{
  float x;
  float y;
} Point2f;

typedef struct _Point3f
{
  float x;
  float y;
  float z;
} Point3f;

typedef struct _Point4f
{
  float x;
  float y;
  float z;
  float w;
} Point4f;

static void
_cogl_matrix_transform_points_f2 (const CoglMatrix *matrix,
                                  size_t stride_in,
                                  const void *points_in,
                                  size_t stride_out,
                                  void *points_out,
                                  int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point2f p = *(Point2f *)((uint8_t *)points_in + i * stride_in);
      Point3f *o = (Point3f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->yx * p.y + matrix->wx;
      o->y = matrix->xy * p.x + matrix->yy * p.y + matrix->wy;
      o->z = matrix->xz * p.x + matrix->yz * p.y + matrix->wz;
    }
}

static void
_cogl_matrix_project_points_f2 (const CoglMatrix *matrix,
                                size_t stride_in,
                                const void *points_in,
                                size_t stride_out,
                                void *points_out,
                                int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point2f p = *(Point2f *)((uint8_t *)points_in + i * stride_in);
      Point4f *o = (Point4f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->yx * p.y + matrix->wx;
      o->y = matrix->xy * p.x + matrix->yy * p.y + matrix->wy;
      o->z = matrix->xz * p.x + matrix->yz * p.y + matrix->wz;
      o->w = matrix->xw * p.x + matrix->yw * p.y + matrix->ww;
    }
}

static void
_cogl_matrix_transform_points_f3 (const CoglMatrix *matrix,
                                  size_t stride_in,
                                  const void *points_in,
                                  size_t stride_out,
                                  void *points_out,
                                  int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point3f p = *(Point3f *)((uint8_t *)points_in + i * stride_in);
      Point3f *o = (Point3f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->yx * p.y +
             matrix->zx * p.z + matrix->wx;
      o->y = matrix->xy * p.x + matrix->yy * p.y +
             matrix->zy * p.z + matrix->wy;
      o->z = matrix->xz * p.x + matrix->yz * p.y +
             matrix->zz * p.z + matrix->wz;
    }
}

static void
_cogl_matrix_project_points_f3 (const CoglMatrix *matrix,
                                size_t stride_in,
                                const void *points_in,
                                size_t stride_out,
                                void *points_out,
                                int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point3f p = *(Point3f *)((uint8_t *)points_in + i * stride_in);
      Point4f *o = (Point4f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->yx * p.y +
             matrix->zx * p.z + matrix->wx;
      o->y = matrix->xy * p.x + matrix->yy * p.y +
             matrix->zy * p.z + matrix->wy;
      o->z = matrix->xz * p.x + matrix->yz * p.y +
             matrix->zz * p.z + matrix->wz;
      o->w = matrix->xw * p.x + matrix->yw * p.y +
             matrix->zw * p.z + matrix->ww;
    }
}

static void
_cogl_matrix_project_points_f4 (const CoglMatrix *matrix,
                                size_t stride_in,
                                const void *points_in,
                                size_t stride_out,
                                void *points_out,
                                int n_points)
{
  int i;

  for (i = 0; i < n_points; i++)
    {
      Point4f p = *(Point4f *)((uint8_t *)points_in + i * stride_in);
      Point4f *o = (Point4f *)((uint8_t *)points_out + i * stride_out);

      o->x = matrix->xx * p.x + matrix->yx * p.y +
             matrix->zx * p.z + matrix->wx * p.w;
      o->y = matrix->xy * p.x + matrix->yy * p.y +
             matrix->zy * p.z + matrix->wy * p.w;
      o->z = matrix->xz * p.x + matrix->yz * p.y +
             matrix->zz * p.z + matrix->wz * p.w;
      o->w = matrix->xw * p.x + matrix->yw * p.y +
             matrix->zw * p.z + matrix->ww * p.w;
    }
}

void
cogl_matrix_transform_points (const CoglMatrix *matrix,
                              int n_components,
                              size_t stride_in,
                              const void *points_in,
                              size_t stride_out,
                              void *points_out,
                              int n_points)
{
  /* The results of transforming always have three components... */
  _COGL_RETURN_IF_FAIL (stride_out >= sizeof (Point3f));

  if (n_components == 2)
    _cogl_matrix_transform_points_f2 (matrix,
                                      stride_in, points_in,
                                      stride_out, points_out,
                                      n_points);
  else
    {
      _COGL_RETURN_IF_FAIL (n_components == 3);

      _cogl_matrix_transform_points_f3 (matrix,
                                        stride_in, points_in,
                                        stride_out, points_out,
                                        n_points);
    }
}

void
cogl_matrix_project_points (const CoglMatrix *matrix,
                            int n_components,
                            size_t stride_in,
                            const void *points_in,
                            size_t stride_out,
                            void *points_out,
                            int n_points)
{
  if (n_components == 2)
    _cogl_matrix_project_points_f2 (matrix,
                                    stride_in, points_in,
                                    stride_out, points_out,
                                    n_points);
  else if (n_components == 3)
    _cogl_matrix_project_points_f3 (matrix,
                                    stride_in, points_in,
                                    stride_out, points_out,
                                    n_points);
  else
    {
      _COGL_RETURN_IF_FAIL (n_components == 4);

      _cogl_matrix_project_points_f4 (matrix,
                                      stride_in, points_in,
                                      stride_out, points_out,
                                      n_points);
    }
}

gboolean
cogl_matrix_is_identity (const CoglMatrix *matrix)
{
  return memcmp (matrix, identity, sizeof (float) * 16) == 0;
}

void
cogl_matrix_look_at (CoglMatrix *matrix,
                     float eye_position_x,
                     float eye_position_y,
                     float eye_position_z,
                     float object_x,
                     float object_y,
                     float object_z,
                     float world_up_x,
                     float world_up_y,
                     float world_up_z)
{
  graphene_matrix_t look_at, m;
  graphene_vec3_t eye, center, up;

  graphene_vec3_init (&eye, eye_position_x, eye_position_y, eye_position_z);
  graphene_vec3_init (&center, object_x, object_y, object_z);
  graphene_vec3_init (&up, world_up_x, world_up_y, world_up_z);
  graphene_matrix_init_look_at (&look_at, &eye, &center, &up);

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_translate (&m, &GRAPHENE_POINT3D_INIT (-eye_position_x,
                                                         -eye_position_y,
                                                         -eye_position_z));
  graphene_matrix_multiply (&m, &look_at, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);
}

void
cogl_matrix_transpose (CoglMatrix *matrix)
{
  graphene_matrix_t transposed;

  cogl_matrix_to_graphene_matrix (matrix, &transposed);
  graphene_matrix_transpose (&transposed, &transposed);
  graphene_matrix_to_cogl_matrix (&transposed, matrix);
}

GType
cogl_gtype_matrix_get_type (void)
{
  return cogl_matrix_get_gtype ();
}
