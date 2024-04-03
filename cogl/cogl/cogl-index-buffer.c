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

#include "config.h"

#include "cogl/cogl-indices.h"
#include "cogl/cogl-indices-private.h"
#include "cogl/cogl-context-private.h"

G_DEFINE_FINAL_TYPE (CoglIndexBuffer, cogl_index_buffer, COGL_TYPE_BUFFER)

static void
cogl_index_buffer_class_init (CoglIndexBufferClass *klass)
{
}

static void
cogl_index_buffer_init (CoglIndexBuffer *buffer)
{
}

/* XXX: Unlike the wiki design this just takes a size. A single
 * indices buffer should be able to contain multiple ranges of indices
 * which the wiki design doesn't currently consider. */
CoglIndexBuffer *
cogl_index_buffer_new (CoglContext *context,
                       size_t       bytes)
{
  CoglIndexBuffer *indices;

  indices = g_object_new (COGL_TYPE_INDEX_BUFFER,
                          "context", context,
                          "size", (uint64_t) bytes,
                          "default-target", COGL_BUFFER_BIND_TARGET_INDEX_BUFFER,
                          "update-hint", COGL_BUFFER_UPDATE_HINT_STATIC,
                          NULL);
  return indices;
}
