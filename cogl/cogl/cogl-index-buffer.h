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

#include "cogl/cogl-context.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CoglIndexBuffer:
 *
 *Functions for creating and manipulating vertex indices.
 */
#define COGL_TYPE_INDEX_BUFFER            (cogl_index_buffer_get_type ())
#define COGL_INDEX_BUFFER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_INDEX_BUFFER, CoglIndexBuffer))
#define COGL_INDEX_BUFFER_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_INDEX_BUFFER, CoglIndexBuffer const))
#define COGL_INDEX_BUFFER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COGL_TYPE_INDEX_BUFFER, CoglIndexBufferClass))
#define COGL_IS_INDEX_BUFFER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COGL_TYPE_INDEX_BUFFER))
#define COGL_IS_INDEX_BUFFER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COGL_TYPE_INDEX_BUFFER))
#define COGL_INDEX_BUFFER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COGL_TYPE_INDEX_BUFFER, CoglIndexBufferClass))

typedef struct _CoglIndexBufferClass CoglIndexBufferClass;
typedef struct _CoglIndexBuffer CoglIndexBuffer;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglIndexBuffer, g_object_unref)

COGL_EXPORT
GType               cogl_index_buffer_get_type       (void) G_GNUC_CONST;

/**
 * cogl_index_buffer_new:
 * @context: A #CoglContext
 * @bytes: The number of bytes to allocate for vertex attribute data.
 *
 * Declares a new #CoglIndexBuffer of @size bytes to contain vertex
 * indices. Once declared, data can be set using
 * cogl_buffer_set_data() or by mapping it into the application's
 * address space using cogl_buffer_map().
 *
 * Return value: (transfer full): A newly allocated #CoglIndexBuffer
 */
COGL_EXPORT CoglIndexBuffer *
cogl_index_buffer_new (CoglContext *context,
                       size_t       bytes);

G_END_DECLS
