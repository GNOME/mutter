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

#include "cogl-graphene-utils.h"
#include "cogl-matrix-private.h"

#include <stdint.h>

gboolean
cogl_graphene_matrix_equal (const graphene_matrix_t *matrix_a,
                            const graphene_matrix_t *matrix_b)
{
  graphene_vec4_t row_a, row_b;

#define COMPARE_MATRIX_ROW(row) \
  G_STMT_START { \
    graphene_matrix_get_row (matrix_a, row, &row_a); \
    graphene_matrix_get_row (matrix_b, row, &row_b); \
    if (!graphene_vec4_equal (&row_a, &row_b)) \
      return FALSE; \
  } G_STMT_END

  COMPARE_MATRIX_ROW (0);
  COMPARE_MATRIX_ROW (1);
  COMPARE_MATRIX_ROW (2);
  COMPARE_MATRIX_ROW (3);

#undef COMPARE_MATRIX_ROW

  return TRUE;
}

void
cogl_matrix_to_graphene_matrix (const CoglMatrix  *matrix,
                                graphene_matrix_t *res)
{
  graphene_matrix_init_from_float (res, (float *)matrix);
}

void
graphene_matrix_to_cogl_matrix (const graphene_matrix_t *matrix,
                                CoglMatrix              *res)
{
  graphene_matrix_to_float (matrix, (float *)res);
}
