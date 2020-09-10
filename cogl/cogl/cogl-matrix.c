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

#include <glib.h>
#include <math.h>
#include <string.h>

#include <cogl-gtype-private.h>
COGL_GTYPE_DEFINE_BOXED (Matrix, matrix,
                         cogl_matrix_copy,
                         cogl_matrix_free);

/*
 * Symbolic names to some of the entries in the matrix
 *
 * These are handy for the viewport mapping, which is expressed as a matrix.
 */
#define MAT_SX 0
#define MAT_SY 5
#define MAT_SZ 10
#define MAT_TX 12
#define MAT_TY 13
#define MAT_TZ 14

/*
 * These identify different kinds of 4x4 transformation matrices and we use
 * this information to find fast-paths when available.
 */
enum CoglMatrixType {
   COGL_MATRIX_TYPE_GENERAL,	/**< general 4x4 matrix */
   COGL_MATRIX_TYPE_IDENTITY,	/**< identity matrix */
   COGL_MATRIX_TYPE_3D_NO_ROT,	/**< orthogonal projection and others... */
   COGL_MATRIX_TYPE_PERSPECTIVE,	/**< perspective projection matrix */
   COGL_MATRIX_TYPE_2D,		/**< 2-D transformation */
   COGL_MATRIX_TYPE_2D_NO_ROT,	/**< 2-D scale & translate only */
   COGL_MATRIX_TYPE_3D,		/**< 3-D transformation */
   COGL_MATRIX_N_TYPES
} ;

#define LEN_SQUARED_3FV( V ) ((V)[0]*(V)[0]+(V)[1]*(V)[1]+(V)[2]*(V)[2])

/*
 * \defgroup MatFlags MAT_FLAG_XXX-flags
 *
 * Bitmasks to indicate different kinds of 4x4 matrices in CoglMatrix::flags
 */
#define MAT_FLAG_IDENTITY       0     /*< is an identity matrix flag.
                                       *   (Not actually used - the identity
                                       *   matrix is identified by the absence
                                       *   of all other flags.)
                                       */
#define MAT_FLAG_GENERAL        0x1   /*< is a general matrix flag */
#define MAT_FLAG_ROTATION       0x2   /*< is a rotation matrix flag */
#define MAT_FLAG_TRANSLATION    0x4   /*< is a translation matrix flag */
#define MAT_FLAG_UNIFORM_SCALE  0x8   /*< is an uniform scaling matrix flag */
#define MAT_FLAG_GENERAL_SCALE  0x10  /*< is a general scaling matrix flag */
#define MAT_FLAG_GENERAL_3D     0x20  /*< general 3D matrix flag */
#define MAT_FLAG_PERSPECTIVE    0x40  /*< is a perspective proj matrix flag */
#define MAT_FLAG_SINGULAR       0x80  /*< is a singular matrix flag */
#define MAT_DIRTY_TYPE          0x100  /*< matrix type is dirty */
#define MAT_DIRTY_FLAGS         0x200  /*< matrix flags are dirty */
#define MAT_DIRTY_INVERSE       0x400  /*< matrix inverse is dirty */

/* angle preserving matrix flags mask */
#define MAT_FLAGS_ANGLE_PRESERVING (MAT_FLAG_ROTATION | \
				    MAT_FLAG_TRANSLATION | \
				    MAT_FLAG_UNIFORM_SCALE)

/* geometry related matrix flags mask */
#define MAT_FLAGS_GEOMETRY (MAT_FLAG_GENERAL | \
			    MAT_FLAG_ROTATION | \
			    MAT_FLAG_TRANSLATION | \
			    MAT_FLAG_UNIFORM_SCALE | \
			    MAT_FLAG_GENERAL_SCALE | \
			    MAT_FLAG_GENERAL_3D | \
			    MAT_FLAG_PERSPECTIVE | \
	                    MAT_FLAG_SINGULAR)

/* length preserving matrix flags mask */
#define MAT_FLAGS_LENGTH_PRESERVING (MAT_FLAG_ROTATION | \
				     MAT_FLAG_TRANSLATION)


/* 3D (non-perspective) matrix flags mask */
#define MAT_FLAGS_3D (MAT_FLAG_ROTATION | \
		      MAT_FLAG_TRANSLATION | \
		      MAT_FLAG_UNIFORM_SCALE | \
		      MAT_FLAG_GENERAL_SCALE | \
		      MAT_FLAG_GENERAL_3D)

/* dirty matrix flags mask */
#define MAT_DIRTY_ALL      (MAT_DIRTY_TYPE | \
			    MAT_DIRTY_FLAGS | \
			    MAT_DIRTY_INVERSE)


/*
 * Test geometry related matrix flags.
 *
 * @mat a pointer to a CoglMatrix structure.
 * @a flags mask.
 *
 * Returns: non-zero if all geometry related matrix flags are contained within
 * the mask, or zero otherwise.
 */
#define TEST_MAT_FLAGS(mat, a)  \
    ((MAT_FLAGS_GEOMETRY & (~(a)) & ((mat)->flags) ) == 0)



/*
 * Names of the corresponding CoglMatrixType values.
 */
static const char *types[] = {
   "COGL_MATRIX_TYPE_GENERAL",
   "COGL_MATRIX_TYPE_IDENTITY",
   "COGL_MATRIX_TYPE_3D_NO_ROT",
   "COGL_MATRIX_TYPE_PERSPECTIVE",
   "COGL_MATRIX_TYPE_2D",
   "COGL_MATRIX_TYPE_2D_NO_ROT",
   "COGL_MATRIX_TYPE_3D"
};


/*
 * Identity matrix.
 */
static float identity[16] = {
   1.0, 0.0, 0.0, 0.0,
   0.0, 1.0, 0.0, 0.0,
   0.0, 0.0, 1.0, 0.0,
   0.0, 0.0, 0.0, 1.0
};

static inline void
graphene_matrix_to_cogl_matrix (const graphene_matrix_t *m,
                                CoglMatrix              *matrix)
{
  float v[16] = { 0.f, };

  graphene_matrix_to_float (m, v);
  cogl_matrix_init_from_array (matrix, v);
}

static inline void
cogl_matrix_to_graphene_matrix (const CoglMatrix  *matrix,
                                graphene_matrix_t *m)
{
  graphene_matrix_init_from_float (m, (float*)matrix);
}

void
cogl_matrix_multiply (CoglMatrix *result,
		      const CoglMatrix *a,
		      const CoglMatrix *b)
{
  graphene_matrix_t res;
  graphene_matrix_t ma;
  graphene_matrix_t mb;

  cogl_matrix_to_graphene_matrix (a, &ma);
  cogl_matrix_to_graphene_matrix (b, &mb);
  graphene_matrix_multiply (&mb, &ma, &res);
  graphene_matrix_to_cogl_matrix (&res, result);

  result->flags = a->flags | b->flags | MAT_DIRTY_TYPE | MAT_DIRTY_INVERSE;

  _COGL_MATRIX_DEBUG_PRINT (result);
}

#if 0
/* Marks the matrix flags with general flag, and type and inverse dirty flags.
 * Calls matrix_multiply4x4() for the multiplication.
 */
static void
_cogl_matrix_multiply_array (CoglMatrix *result, const float *array)
{
  result->flags |= (MAT_FLAG_GENERAL |
                  MAT_DIRTY_TYPE |
                  MAT_DIRTY_INVERSE |
                  MAT_DIRTY_FLAGS);

  matrix_multiply4x4 ((float *)result, (float *)result, (float *)array);
}
#endif

/*
 * Print a matrix array.
 *
 * Called by _cogl_matrix_print() to print a matrix or its inverse.
 */
static void
print_matrix_floats (const char *prefix, const float m[16])
{
  int i;
  for (i = 0;i < 4; i++)
    g_print ("%s\t%f %f %f %f\n", prefix, m[i], m[4+i], m[8+i], m[12+i] );
}

void
_cogl_matrix_prefix_print (const char *prefix, const CoglMatrix *matrix)
{
  if (!(matrix->flags & MAT_DIRTY_TYPE))
    {
      g_return_if_fail (matrix->type < COGL_MATRIX_N_TYPES);
      g_print ("%sMatrix type: %s, flags: %x\n",
               prefix, types[matrix->type], (int)matrix->flags);
    }
  else
    g_print ("%sMatrix type: DIRTY, flags: %x\n",
             prefix, (int)matrix->flags);

  print_matrix_floats (prefix, (float *)matrix);
  g_print ("%sInverse: \n", prefix);
  if (!(matrix->flags & MAT_DIRTY_INVERSE))
    print_matrix_floats (prefix, matrix->inv);
  else
    g_print ("%s  - not available\n", prefix);
}

/*
 * Dumps the contents of a CoglMatrix structure.
 */
void
cogl_debug_matrix_print (const CoglMatrix *matrix)
{
  _cogl_matrix_prefix_print ("", matrix);
}

/*
 * References an element of 4x4 matrix.
 *
 * @m matrix array.
 * @c column of the desired element.
 * @r row of the desired element.
 *
 * Returns: value of the desired element.
 *
 * Calculate the linear storage index of the element and references it.
 */
#define MAT(m,r,c) (m)[(c)*4+(r)]

/*
 * Compute inverse of a transformation matrix.
 *
 * @mat pointer to a CoglMatrix structure. The matrix inverse will be
 * stored in the CoglMatrix::inv attribute.
 *
 * Returns: %TRUE for success, %FALSE for failure (\p singular matrix).
 *
 * Calls the matrix inversion function in inv_mat_tab corresponding to the
 * given matrix type.  In case of failure, updates the MAT_FLAG_SINGULAR flag,
 * and copies the identity matrix into CoglMatrix::inv.
 */

static inline gboolean
calculate_inverse (CoglMatrix *matrix)
{
  graphene_matrix_t inverse;
  graphene_matrix_t scaled;
  graphene_matrix_t m;
  gboolean invertible;
  float pivot = G_MAXFLOAT;
  float v[16];
  float scale;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_to_float (&m, v);

  pivot = MIN (pivot, v[0]);
  pivot = MIN (pivot, v[5]);
  pivot = MIN (pivot, v[10]);
  pivot = MIN (pivot, v[15]);
  scale = 1.f / pivot;

  graphene_matrix_init_scale (&scaled, scale, scale, scale);

  /* Float precision is a limiting factor */
  graphene_matrix_multiply (&m, &scaled, &m);

  invertible = graphene_matrix_inverse (&m, &inverse);

  if (invertible)
    graphene_matrix_multiply (&scaled, &inverse, &inverse);
  else
    graphene_matrix_init_identity (&inverse);

  graphene_matrix_to_float (&inverse, matrix->inv);

  return invertible;
}

static gboolean
_cogl_matrix_update_inverse (CoglMatrix *matrix)
{
  if (matrix->flags & MAT_DIRTY_FLAGS ||
      matrix->flags & MAT_DIRTY_INVERSE)
    {
      if (calculate_inverse (matrix))
        matrix->flags &= ~MAT_FLAG_SINGULAR;
      else
        matrix->flags |= MAT_FLAG_SINGULAR;

      matrix->flags &= ~(MAT_DIRTY_FLAGS |
                         MAT_DIRTY_TYPE |
                         MAT_DIRTY_INVERSE);
    }

  if (matrix->flags & MAT_FLAG_SINGULAR)
    return FALSE;
  else
    return TRUE;
}

gboolean
cogl_matrix_get_inverse (const CoglMatrix *matrix, CoglMatrix *inverse)
{
  if (_cogl_matrix_update_inverse ((CoglMatrix *)matrix))
    {
      cogl_matrix_init_from_array (inverse, matrix->inv);
      return TRUE;
    }
  else
    {
      cogl_matrix_init_identity (inverse);
      return FALSE;
    }
}

void
cogl_matrix_rotate (CoglMatrix *matrix,
		    float angle,
		    float x,
		    float y,
		    float z)
{
  graphene_matrix_t rotation;
  graphene_matrix_t m;
  graphene_vec3_t axis;
  unsigned long flags;

  flags = matrix->flags;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_vec3_init (&axis, x, y, z);
  graphene_matrix_init_rotate (&rotation, angle, &axis);
  graphene_matrix_multiply (&rotation, &m, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  flags |= MAT_FLAG_ROTATION | MAT_DIRTY_TYPE | MAT_DIRTY_INVERSE;
  matrix->flags = flags;

  _COGL_MATRIX_DEBUG_PRINT (matrix);
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
  graphene_matrix_t frustum;
  graphene_matrix_t m;
  unsigned long flags;

  flags = matrix->flags;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_init_frustum (&frustum,
                                left, right,
                                bottom, top,
                                z_near, z_far);
  graphene_matrix_multiply (&frustum, &m, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  flags |= MAT_FLAG_PERSPECTIVE | MAT_DIRTY_TYPE | MAT_DIRTY_INVERSE;
  matrix->flags = flags;

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_perspective (CoglMatrix *matrix,
                         float       fov_y,
                         float       aspect,
                         float       z_near,
                         float       z_far)
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
cogl_matrix_orthographic (CoglMatrix *matrix,
                          float left,
                          float bottom,
                          float right,
                          float top,
                          float near,
                          float far)
{
  graphene_matrix_t ortho;
  graphene_matrix_t m;
  unsigned long flags;

  flags = matrix->flags;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_init_ortho (&ortho,
                              left, right,
                              top, bottom,
                              near, far);
  graphene_matrix_multiply (&ortho, &m, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  matrix->flags = (flags |
                   MAT_FLAG_GENERAL_SCALE |
                   MAT_FLAG_TRANSLATION |
                   MAT_DIRTY_TYPE |
                   MAT_DIRTY_INVERSE);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_scale (CoglMatrix *matrix,
		   float sx,
		   float sy,
		   float sz)
{
  graphene_matrix_t scale;
  graphene_matrix_t m;
  unsigned long flags;

  flags = matrix->flags;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_init_scale (&scale, sx, sy, sz);
  graphene_matrix_multiply (&scale, &m, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  if (fabsf (sx - sy) < 1e-8 && fabsf (sx - sz) < 1e-8)
    flags |= MAT_FLAG_UNIFORM_SCALE;
  else
    flags |= MAT_FLAG_GENERAL_SCALE;

  flags |= (MAT_DIRTY_TYPE | MAT_DIRTY_INVERSE);
  matrix->flags = flags;

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_translate (CoglMatrix *matrix,
		       float x,
		       float y,
		       float z)
{
  graphene_matrix_t translation;
  graphene_matrix_t m;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_init_translate (&translation,
                                  &GRAPHENE_POINT3D_INIT (x, y, z));
  graphene_matrix_multiply (&translation, &m, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);
  matrix->flags |= MAT_FLAG_TRANSLATION | MAT_DIRTY_TYPE | MAT_DIRTY_INVERSE;

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

#if 0
/*
 * Set matrix to do viewport and depthrange mapping.
 * Transforms Normalized Device Coords to window/Z values.
 */
static void
_cogl_matrix_viewport (CoglMatrix *matrix,
                       float x, float y,
                       float width, float height,
                       float zNear, float zFar, float depthMax)
{
  float *m = (float *)matrix;
  m[MAT_SX] = width / 2.0f;
  m[MAT_TX] = m[MAT_SX] + x;
  m[MAT_SY] = height / 2.0f;
  m[MAT_TY] = m[MAT_SY] + y;
  m[MAT_SZ] = depthMax * ((zFar - zNear) / 2.0f);
  m[MAT_TZ] = depthMax * ((zFar - zNear) / 2.0f + zNear);
  matrix->flags = MAT_FLAG_GENERAL_SCALE | MAT_FLAG_TRANSLATION;
  matrix->type = COGL_MATRIX_TYPE_3D_NO_ROT;
}
#endif

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

  matrix->type = COGL_MATRIX_TYPE_IDENTITY;
  matrix->flags = MAT_DIRTY_INVERSE;
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

  matrix->type = COGL_MATRIX_TYPE_3D;
  matrix->flags = MAT_FLAG_TRANSLATION | MAT_DIRTY_INVERSE;

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

#if 0
/*
 * Test if the given matrix preserves vector lengths.
 */
static gboolean
_cogl_matrix_is_length_preserving (const CoglMatrix *m)
{
  return TEST_MAT_FLAGS (m, MAT_FLAGS_LENGTH_PRESERVING);
}

/*
 * Test if the given matrix does any rotation.
 * (or perhaps if the upper-left 3x3 is non-identity)
 */
static gboolean
_cogl_matrix_has_rotation (const CoglMatrix *matrix)
{
  if (matrix->flags & (MAT_FLAG_GENERAL |
                       MAT_FLAG_ROTATION |
                       MAT_FLAG_GENERAL_3D |
                       MAT_FLAG_PERSPECTIVE))
    return TRUE;
  else
    return FALSE;
}

static gboolean
_cogl_matrix_is_general_scale (const CoglMatrix *matrix)
{
  return (matrix->flags & MAT_FLAG_GENERAL_SCALE) ? TRUE : FALSE;
}

static gboolean
_cogl_matrix_is_dirty (const CoglMatrix *matrix)
{
  return (matrix->flags & MAT_DIRTY_ALL) ? TRUE : FALSE;
}
#endif

/*
 * Loads a matrix array into CoglMatrix.
 *
 * @m matrix array.
 * @mat matrix.
 *
 * Copies \p m into CoglMatrix::m and marks the MAT_FLAG_GENERAL and
 * MAT_DIRTY_ALL
 * flags.
 */
static void
_cogl_matrix_init_from_array (CoglMatrix *matrix, const float *array)
{
  memcpy (matrix, array, 16 * sizeof (float));
  matrix->flags = (MAT_FLAG_GENERAL | MAT_DIRTY_ALL);
}

void
cogl_matrix_init_from_array (CoglMatrix *matrix, const float *array)
{
  _cogl_matrix_init_from_array (matrix, array);
  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_init_from_matrix (CoglMatrix       *matrix,
                              const CoglMatrix *source)
{
  memcpy (matrix, source, sizeof (CoglMatrix));
}

void
_cogl_matrix_init_from_matrix_without_inverse (CoglMatrix *matrix,
                                               const CoglMatrix *src)
{
  memcpy (matrix, src, 16 * sizeof (float));
  matrix->type = src->type;
  matrix->flags = src->flags | MAT_DIRTY_INVERSE;
}

void
cogl_matrix_init_from_euler (CoglMatrix *matrix,
                             const graphene_euler_t *euler)
{
  graphene_matrix_t m;

  graphene_matrix_init_identity (&m);
  graphene_matrix_rotate_euler (&m, euler);
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
  graphene_matrix_t ma;
  graphene_matrix_t mb;
  const CoglMatrix *a = v1;
  const CoglMatrix *b = v2;

  g_return_val_if_fail (v1 != NULL, FALSE);
  g_return_val_if_fail (v2 != NULL, FALSE);

  cogl_matrix_to_graphene_matrix (a, &ma);
  cogl_matrix_to_graphene_matrix (b, &mb);

  return graphene_matrix_equal_fast (&ma, &mb);
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

const float *
cogl_matrix_get_array (const CoglMatrix *matrix)
{
  return (float *)matrix;
}

float
cogl_matrix_get_value (const CoglMatrix *matrix,
                       unsigned int      row,
                       unsigned int      column)
{
  return MAT ((float *)matrix, row, column);
}

void
cogl_matrix_transform_point (const CoglMatrix *matrix,
                             float *x,
                             float *y,
                             float *z,
                             float *w)
{
  graphene_matrix_t m;
  graphene_vec4_t p;

  graphene_vec4_init (&p, *x, *y, *z, *w);

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_transform_vec4 (&m, &p, &p);

  *x = graphene_vec4_get_x (&p);
  *y = graphene_vec4_get_y (&p);
  *z = graphene_vec4_get_z (&p);
  *w = graphene_vec4_get_w (&p);
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
init_matrix_rows (const CoglMatrix *matrix,
                  unsigned int      n_rows,
                  graphene_vec4_t  *rows)
{
  graphene_matrix_t m;
  unsigned int i;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_transpose (&m, &m);

  for (i = 0; i < n_rows; i++)
    graphene_matrix_get_row (&m, i, &rows[i]);
}

static void
_cogl_matrix_transform_points_f2 (const CoglMatrix *matrix,
                                  size_t stride_in,
                                  const void *points_in,
                                  size_t stride_out,
                                  void *points_out,
                                  int n_points)
{
  graphene_vec4_t rows[3];
  int i;

  init_matrix_rows (matrix, G_N_ELEMENTS (rows), rows);

  for (i = 0; i < n_points; i++)
    {
      Point2f p = *(Point2f *)((uint8_t *)points_in + i * stride_in);
      Point3f *o = (Point3f *)((uint8_t *)points_out + i * stride_out);
      graphene_vec4_t point;

      graphene_vec4_init (&point, p.x, p.y, 0.f, 1.f);

      o->x = graphene_vec4_dot (&rows[0], &point);
      o->y = graphene_vec4_dot (&rows[1], &point);
      o->z = graphene_vec4_dot (&rows[2], &point);
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
  graphene_vec4_t rows[4];
  int i;

  init_matrix_rows (matrix, G_N_ELEMENTS (rows), rows);

  for (i = 0; i < n_points; i++)
    {
      Point2f p = *(Point2f *)((uint8_t *)points_in + i * stride_in);
      Point4f *o = (Point4f *)((uint8_t *)points_out + i * stride_out);
      graphene_vec4_t point;

      graphene_vec4_init (&point, p.x, p.y, 0.f, 1.f);

      o->x = graphene_vec4_dot (&rows[0], &point);
      o->y = graphene_vec4_dot (&rows[1], &point);
      o->z = graphene_vec4_dot (&rows[2], &point);
      o->w = graphene_vec4_dot (&rows[3], &point);
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
  graphene_vec4_t rows[3];
  int i;

  init_matrix_rows (matrix, G_N_ELEMENTS (rows), rows);

  for (i = 0; i < n_points; i++)
    {
      Point3f p = *(Point3f *)((uint8_t *)points_in + i * stride_in);
      Point3f *o = (Point3f *)((uint8_t *)points_out + i * stride_out);
      graphene_vec4_t point;

      graphene_vec4_init (&point, p.x, p.y, p.z, 1.f);

      o->x = graphene_vec4_dot (&rows[0], &point);
      o->y = graphene_vec4_dot (&rows[1], &point);
      o->z = graphene_vec4_dot (&rows[2], &point);
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
  graphene_vec4_t rows[4];
  int i;

  init_matrix_rows (matrix, G_N_ELEMENTS (rows), rows);

  for (i = 0; i < n_points; i++)
    {
      Point3f p = *(Point3f *)((uint8_t *)points_in + i * stride_in);
      Point4f *o = (Point4f *)((uint8_t *)points_out + i * stride_out);
      graphene_vec4_t point;

      graphene_vec4_init (&point, p.x, p.y, p.z, 1.f);

      o->x = graphene_vec4_dot (&rows[0], &point);
      o->y = graphene_vec4_dot (&rows[1], &point);
      o->z = graphene_vec4_dot (&rows[2], &point);
      o->w = graphene_vec4_dot (&rows[3], &point);
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
  graphene_vec4_t rows[4];
  int i;

  init_matrix_rows (matrix, G_N_ELEMENTS (rows), rows);

  for (i = 0; i < n_points; i++)
    {
      Point4f p = *(Point4f *)((uint8_t *)points_in + i * stride_in);
      Point4f *o = (Point4f *)((uint8_t *)points_out + i * stride_out);
      graphene_vec4_t point;

      graphene_vec4_init (&point, p.x, p.y, p.z, p.w);

      o->x = graphene_vec4_dot (&rows[0], &point);
      o->y = graphene_vec4_dot (&rows[1], &point);
      o->z = graphene_vec4_dot (&rows[2], &point);
      o->w = graphene_vec4_dot (&rows[3], &point);
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
  g_return_if_fail (stride_out >= sizeof (Point3f));

  if (n_components == 2)
    _cogl_matrix_transform_points_f2 (matrix,
                                      stride_in, points_in,
                                      stride_out, points_out,
                                      n_points);
  else
    {
      g_return_if_fail (n_components == 3);

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
      g_return_if_fail (n_components == 4);

      _cogl_matrix_project_points_f4 (matrix,
                                      stride_in, points_in,
                                      stride_out, points_out,
                                      n_points);
    }
}

gboolean
cogl_matrix_is_identity (const CoglMatrix *matrix)
{
  if (!(matrix->flags & MAT_DIRTY_TYPE) &&
      matrix->type == COGL_MATRIX_TYPE_IDENTITY)
    return TRUE;
  else
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
  graphene_matrix_t m;
  graphene_vec3_t eye;
  graphene_vec3_t center;
  graphene_vec3_t up;
  CoglMatrix look_at;

  graphene_vec3_init (&eye, eye_position_x, eye_position_y, eye_position_z);
  graphene_vec3_init (&center, object_x, object_y, object_z);
  graphene_vec3_init (&up, world_up_x, world_up_y, world_up_z);

  graphene_matrix_init_look_at (&m, &eye, &center, &up);

  graphene_matrix_to_cogl_matrix (&m, &look_at);
  look_at.flags = MAT_FLAG_GENERAL_3D | MAT_DIRTY_TYPE | MAT_DIRTY_INVERSE;

  cogl_matrix_multiply (matrix, matrix, &look_at);
}

void
cogl_matrix_transpose (CoglMatrix *matrix)
{
  graphene_matrix_t m;

  cogl_matrix_to_graphene_matrix (matrix, &m);

  /* We don't need to do anything if the matrix is the identity matrix */
  if (graphene_matrix_is_identity (&m))
    return;

  graphene_matrix_transpose (&m, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);
}

GType
cogl_gtype_matrix_get_type (void)
{
  return cogl_matrix_get_gtype ();
}

void
cogl_matrix_skew_xy (CoglMatrix *matrix,
                     float       factor)
{
  graphene_matrix_t skew;
  graphene_matrix_t m;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_init_identity (&skew);
  graphene_matrix_skew_xy (&skew, factor);
  graphene_matrix_multiply (&skew, &m, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_skew_xz (CoglMatrix *matrix,
                     float       factor)
{
  graphene_matrix_t skew;
  graphene_matrix_t m;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_init_identity (&skew);
  graphene_matrix_skew_xz (&skew, factor);
  graphene_matrix_multiply (&skew, &m, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}

void
cogl_matrix_skew_yz (CoglMatrix *matrix,
                     float       factor)
{
  graphene_matrix_t skew;
  graphene_matrix_t m;

  cogl_matrix_to_graphene_matrix (matrix, &m);
  graphene_matrix_init_identity (&skew);
  graphene_matrix_skew_yz (&skew, factor);
  graphene_matrix_multiply (&skew, &m, &m);
  graphene_matrix_to_cogl_matrix (&m, matrix);

  _COGL_MATRIX_DEBUG_PRINT (matrix);
}
