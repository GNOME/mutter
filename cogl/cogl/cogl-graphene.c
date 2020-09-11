/* cogl-graphene.c
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
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

gboolean
cogl_graphene_matrix_get_inverse (const graphene_matrix_t *matrix,
                                  graphene_matrix_t       *inverse)
{
  graphene_matrix_t scaled;
  graphene_matrix_t m;
  gboolean invertible;
  float pivot = G_MAXFLOAT;
  float v[16];
  float scale;

  graphene_matrix_init_from_matrix (&m, matrix);
  graphene_matrix_to_float (&m, v);

  pivot = MIN (pivot, v[0]);
  pivot = MIN (pivot, v[5]);
  pivot = MIN (pivot, v[10]);
  pivot = MIN (pivot, v[15]);
  scale = 1.f / pivot;

  graphene_matrix_init_scale (&scaled, scale, scale, scale);

  /* Float precision is a limiting factor */
  graphene_matrix_multiply (&m, &scaled, &m);

  invertible = graphene_matrix_inverse (&m, inverse);

  if (invertible)
    graphene_matrix_multiply (&scaled, inverse, inverse);
  else
    graphene_matrix_init_identity (inverse);

  return invertible;
}
