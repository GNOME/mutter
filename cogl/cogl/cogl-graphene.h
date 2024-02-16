/* cogl-graphene.h
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

#pragma once

#include "cogl/cogl-macros.h"
#include "cogl/cogl-types.h"

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
 * ```c
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
 * ```
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
 * ```c
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
 * ```
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
