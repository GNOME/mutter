/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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
 *   Robert Bragg <robert@linux.intel.com>
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-index-buffer.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CoglIndices:
 *
 * Describe vertex indices stored in a #CoglIndexBuffer.
 *
 * Indices allow you to avoid duplicating vertices in your vertex data
 * by virtualizing your data and instead providing a sequence of index
 * values that tell the GPU which data should be used for each vertex.
 *
 * If the GPU is given a sequence of indices it doesn't simply walk
 * through each vertex of your data in order it will instead walk
 * through the indices which can provide random access to the
 * underlying data.
 *
 * Since it's very common to have duplicate vertices when describing a
 * shape as a list of triangles it can often be a significant space
 * saving to describe geometry using indices. Reducing the size of
 * your models can make it cheaper to map them into the GPU by
 * reducing the demand on memory bandwidth and may help to make better
 * use of your GPUs internal vertex caching.
 *
 * For example, to describe a quadrilateral as 2 triangles for the GPU
 * you could either provide data with 6 vertices or instead with
 * indices you can provide vertex data for just 4 vertices and an
 * index buffer that specifies the 6 vertices by indexing the shared
 * vertices multiple times.
 *
 * ```c
 *   CoglVertexP2 quad_vertices[] = {
 *     {x0, y0}, //0 = top left
 *     {x1, y1}, //1 = bottom left
 *     {x2, y2}, //2 = bottom right
 *     {x3, y3}, //3 = top right
 *   };
 *   //tell the gpu how to interpret the quad as 2 triangles...
 *   unsigned char indices[] = {0, 1, 2, 0, 2, 3};
 * ```
 *
 * Even in the above illustration we see a saving of 10bytes for one
 * quad compared to having data for 6 vertices and no indices but if
 * you need to draw 100s or 1000s of quads then its really quite
 * significant.
 *
 * Something else to consider is that often indices can be defined
 * once and remain static while the vertex data may change for
 * animations perhaps. That means you may be able to ignore the
 * negligible cost of mapping your indices into the GPU if they don't
 * ever change.
 *
 * The above illustration is actually a good example of static indices
 * because it's really common that developers have quad mesh data that
 * they need to display and we know exactly what that indices array
 * needs to look like depending on the number of quads that need to be
 * drawn. It doesn't matter how the quads might be animated and
 * changed the indices will remain the same. Cogl even has a utility
 * ([method@Cogl.Context.get_rectangle_indices]) to get access to re-useable indices
 * for drawing quads as above.
 */

#define COGL_TYPE_INDICES (cogl_indices_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglIndices, cogl_indices,
                      COGL, INDICES, GObject)

COGL_EXPORT CoglIndices *
cogl_indices_new (CoglContext *context,
                  CoglIndicesType type,
                  const void *indices_data,
                  int n_indices);

/**
 * cogl_indices_get_buffer:
 *
 * Returns: (transfer none): a #CoglIndexBuffer
 */
COGL_EXPORT CoglIndexBuffer *
cogl_indices_get_buffer (CoglIndices *indices);

COGL_EXPORT CoglIndicesType
cogl_indices_get_indices_type (CoglIndices *indices);

G_END_DECLS
