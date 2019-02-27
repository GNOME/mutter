/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2019 Endless, Inc.
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
 *
 *
 * Authors:
 *   Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#ifndef COGL_GRAPHENE_UTILS_H
#define COGL_GRAPHENE_UTILS_H

#include "cogl-matrix.h"

#include <graphene.h>
#include <glib-object.h>

gboolean
cogl_graphene_matrix_equal (const graphene_matrix_t *matrix_a,
                            const graphene_matrix_t *matrix_b);

void
cogl_matrix_to_graphene_matrix (const CoglMatrix  *matrix,
                                graphene_matrix_t *res);

void
graphene_matrix_to_cogl_matrix (const graphene_matrix_t *matrix,
                                CoglMatrix              *res);

#endif /* COGL_GRAPHENE_UTILS_H */
