/* cogl-graphene.h
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

#ifndef COGL_GRAPHENE_H
#define COGL_GRAPHENE_H

#include <cogl/cogl-defines.h>
#include <cogl/cogl-macros.h>
#include <cogl/cogl-types.h>

#include <glib.h>

#include <graphene.h>

G_BEGIN_DECLS


/**
 * cogl_graphene_matrix_project_point:
 * @matrix: A 4x4 transformation matrix
 * @x: (inout): The X component of your points position
 * @y: (inout): The Y component of your points position
 * @z: (inout): The Z component of your points position
 * @w: (inout): The W component of your points position
 *
 * Transforms a point whose position is given and returned as four float
 * components.
 */
COGL_EXPORT void
cogl_graphene_matrix_project_point (const graphene_matrix_t *matrix,
                                    float                   *x,
                                    float                   *y,
                                    float                   *z,
                                    float                   *w);

/**
 * cogl_graphene_matrix_transform_points:
 * @matrix: A transformation matrix
 * @n_components: The number of position components for each input point.
 *                (either 2 or 3)
 * @stride_in: The stride in bytes between input points.
 * @points_in: A pointer to the first component of the first input point.
 * @stride_out: The stride in bytes between output points.
 * @points_out: A pointer to the first component of the first output point.
 * @n_points: The number of points to transform.
 *
 * Transforms an array of input points and writes the result to
 * another array of output points. The input points can either have 2
 * or 3 components each. The output points always have 3 components.
 * The output array can simply point to the input array to do the
 * transform in-place.
 *
 * If you need to transform 4 component points see
 * cogl_graphene_matrix_project_points().
 *
 * Here's an example with differing input/output strides:
 * |[
 * typedef struct {
 *   float x,y;
 *   uint8_t r,g,b,a;
 *   float s,t,p;
 * } MyInVertex;
 * typedef struct {
 *   uint8_t r,g,b,a;
 *   float x,y,z;
 * } MyOutVertex;
 * MyInVertex vertices[N_VERTICES];
 * MyOutVertex results[N_VERTICES];
 * graphene_matrix_t matrix;
 *
 * my_load_vertices (vertices);
 * my_get_matrix (&matrix);
 *
 * cogl_graphene_matrix_transform_points (&matrix,
 *                               2,
 *                               sizeof (MyInVertex),
 *                               &vertices[0].x,
 *                               sizeof (MyOutVertex),
 *                               &results[0].x,
 *                               N_VERTICES);
 * ]|
 *
 * Stability: unstable
 */
COGL_EXPORT void
cogl_graphene_matrix_transform_points (const graphene_matrix_t *matrix,
                                       int                      n_components,
                                       size_t                   stride_in,
                                       const void              *points_in,
                                       size_t                   stride_out,
                                       void                    *points_out,
                                       int                      n_points);

/**
 * cogl_graphene_matrix_project_points:
 * @matrix: A projection matrix
 * @n_components: The number of position components for each input point.
 *                (either 2, 3 or 4)
 * @stride_in: The stride in bytes between input points.
 * @points_in: A pointer to the first component of the first input point.
 * @stride_out: The stride in bytes between output points.
 * @points_out: A pointer to the first component of the first output point.
 * @n_points: The number of points to transform.
 *
 * Projects an array of input points and writes the result to another
 * array of output points. The input points can either have 2, 3 or 4
 * components each. The output points always have 4 components (known
 * as homogeneous coordinates). The output array can simply point to
 * the input array to do the transform in-place.
 *
 * Here's an example with differing input/output strides:
 * |[
 * typedef struct {
 *   float x,y;
 *   uint8_t r,g,b,a;
 *   float s,t,p;
 * } MyInVertex;
 * typedef struct {
 *   uint8_t r,g,b,a;
 *   float x,y,z;
 * } MyOutVertex;
 * MyInVertex vertices[N_VERTICES];
 * MyOutVertex results[N_VERTICES];
 * graphene_matrix_t matrix;
 *
 * my_load_vertices (vertices);
 * my_get_matrix (&matrix);
 *
 * cogl_graphene_matrix_project_points (&matrix,
 *                             2,
 *                             sizeof (MyInVertex),
 *                             &vertices[0].x,
 *                             sizeof (MyOutVertex),
 *                             &results[0].x,
 *                             N_VERTICES);
 * ]|
 *
 * Stability: unstable
 */
COGL_EXPORT void
cogl_graphene_matrix_project_points (const graphene_matrix_t *matrix,
                                     int                      n_components,
                                     size_t                   stride_in,
                                     const void              *points_in,
                                     size_t                   stride_out,
                                     void                    *points_out,
                                     int                      n_points);

G_END_DECLS

#endif /* COGL_GRAPHENE_H */
