/* cogl-graphene.c
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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
 * SPDX-License-Identifier: MIT
 *
 */

#include "cogl/cogl-graphene.h"

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
init_matrix_rows (const graphene_matrix_t *matrix,
                  unsigned int             n_rows,
                  graphene_vec4_t         *rows)
{
  graphene_matrix_t m;
  unsigned int i;

  graphene_matrix_transpose (matrix, &m);

  for (i = 0; i < n_rows; i++)
    graphene_matrix_get_row (&m, i, &rows[i]);
}

static void
transform_points_f2 (const graphene_matrix_t *matrix,
                     size_t                   stride_in,
                     const void              *points_in,
                     size_t                   stride_out,
                     void                    *points_out,
                     int                      n_points)
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
project_points_f2 (const graphene_matrix_t *matrix,
                   size_t                   stride_in,
                   const void              *points_in,
                   size_t                   stride_out,
                   void                    *points_out,
                   int                      n_points)
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
transform_points_f3 (const graphene_matrix_t *matrix,
                     size_t                   stride_in,
                     const void              *points_in,
                     size_t                   stride_out,
                     void                    *points_out,
                     int                      n_points)
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
project_points_f3 (const graphene_matrix_t *matrix,
                   size_t                   stride_in,
                   const void              *points_in,
                   size_t                   stride_out,
                   void                    *points_out,
                   int                      n_points)
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
project_points_f4 (const graphene_matrix_t *matrix,
                   size_t                   stride_in,
                   const void              *points_in,
                   size_t                   stride_out,
                   void                    *points_out,
                   int                      n_points)
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
cogl_graphene_matrix_project_point (const graphene_matrix_t *matrix,
                                    float                   *x,
                                    float                   *y,
                                    float                   *z,
                                    float                   *w)
{
  graphene_vec4_t p;

  graphene_vec4_init (&p, *x, *y, *z, *w);
  graphene_matrix_transform_vec4 (matrix, &p, &p);

  *x = graphene_vec4_get_x (&p);
  *y = graphene_vec4_get_y (&p);
  *z = graphene_vec4_get_z (&p);
  *w = graphene_vec4_get_w (&p);
}

void
cogl_graphene_matrix_transform_points (const graphene_matrix_t *matrix,
                                       int                      n_components,
                                       size_t                   stride_in,
                                       const void              *points_in,
                                       size_t                   stride_out,
                                       void                    *points_out,
                                       int                      n_points)
{
  /* The results of transforming always have three components... */
  g_return_if_fail (stride_out >= sizeof (Point3f));

  if (n_components == 2)
    {
      transform_points_f2 (matrix,
                           stride_in, points_in,
                           stride_out, points_out,
                           n_points);
    }
  else
    {
      g_return_if_fail (n_components == 3);

      transform_points_f3 (matrix,
                           stride_in, points_in,
                           stride_out, points_out,
                           n_points);
    }
}

void
cogl_graphene_matrix_project_points (const graphene_matrix_t *matrix,
                                     int                      n_components,
                                     size_t                   stride_in,
                                     const void              *points_in,
                                     size_t                   stride_out,
                                     void                    *points_out,
                                     int                      n_points)
{
  if (n_components == 2)
    {
      project_points_f2 (matrix,
                         stride_in, points_in,
                         stride_out, points_out,
                         n_points);
    }
  else if (n_components == 3)
    {
      project_points_f3 (matrix,
                         stride_in, points_in,
                         stride_out, points_out,
                         n_points);
    }
  else
    {
      g_return_if_fail (n_components == 4);

      project_points_f4 (matrix,
                         stride_in, points_in,
                         stride_out, points_out,
                         n_points);
    }
}
