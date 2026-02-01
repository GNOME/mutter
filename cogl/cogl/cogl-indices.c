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
 *   Neil Roberts <neil@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-util.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-indices.h"
#include "cogl/cogl-indices-private.h"
#include "cogl/cogl-index-buffer.h"

#include <stdarg.h>

G_DEFINE_FINAL_TYPE (CoglIndices, cogl_indices, G_TYPE_OBJECT);

static void
cogl_indices_dispose (GObject *object)
{
  CoglIndices *indices = COGL_INDICES (object);

  g_object_unref (indices->buffer);

  G_OBJECT_CLASS (cogl_indices_parent_class)->dispose (object);
}

static void
cogl_indices_init (CoglIndices *indices)
{
}

static void
cogl_indices_class_init (CoglIndicesClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_indices_dispose;
}

size_t
cogl_indices_type_get_size (CoglIndicesType type)
{
  switch (type)
    {
    case COGL_INDICES_TYPE_UNSIGNED_BYTE:
      return 1;
    case COGL_INDICES_TYPE_UNSIGNED_SHORT:
      return 2;
    case COGL_INDICES_TYPE_UNSIGNED_INT:
      return 4;
    }
  g_return_val_if_reached (0);
}

CoglIndices *
cogl_indices_new (CoglContext *context,
                  CoglIndicesType type,
                  const void *indices_data,
                  int n_indices)
{
  size_t buffer_bytes = cogl_indices_type_get_size (type) * n_indices;
  g_autoptr (CoglIndexBuffer) index_buffer =
    cogl_index_buffer_new (context, buffer_bytes);
  CoglIndices *indices;

  if (!cogl_buffer_set_data (COGL_BUFFER (index_buffer),
                             0,
                             indices_data,
                             buffer_bytes))
    return NULL;

  indices = g_object_new (COGL_TYPE_INDICES, NULL);
  indices->buffer = g_steal_pointer (&index_buffer);
  indices->type = type;

  return indices;
}

CoglIndexBuffer *
cogl_indices_get_buffer (CoglIndices *indices)
{
  return indices->buffer;
}

CoglIndicesType
cogl_indices_get_indices_type (CoglIndices *indices)
{
  g_return_val_if_fail (COGL_IS_INDICES (indices),
                        COGL_INDICES_TYPE_UNSIGNED_BYTE);
  return indices->type;
}

CoglIndices *
cogl_context_get_rectangle_indices (CoglContext *ctx,
                                    int          n_rectangles)
{
  int n_indices = n_rectangles * 6;

  /* Check if the largest index required will fit in a byte array... */
  if (n_indices <= 256 / 4 * 6)
    {
      CoglIndices *rectangle_byte_indices =
        cogl_context_get_rectangle_byte_indices (ctx);

      /* Generate the byte array if we haven't already */
      if (rectangle_byte_indices == NULL)
        {
          uint8_t *byte_array = g_malloc (256 / 4 * 6 * sizeof (uint8_t));
          uint8_t *p = byte_array;
          int i, vert_num = 0;

          for (i = 0; i < 256 / 4; i++)
            {
              *(p++) = vert_num + 0;
              *(p++) = vert_num + 1;
              *(p++) = vert_num + 2;
              *(p++) = vert_num + 0;
              *(p++) = vert_num + 2;
              *(p++) = vert_num + 3;
              vert_num += 4;
            }

          rectangle_byte_indices
            = cogl_indices_new (ctx,
                                COGL_INDICES_TYPE_UNSIGNED_BYTE,
                                byte_array,
                                256 / 4 * 6);

          cogl_context_set_rectangle_byte_indices (ctx, rectangle_byte_indices);

          g_free (byte_array);
        }

      return rectangle_byte_indices;
    }
  else
    {
      int rectangle_short_indices_len =
        cogl_context_get_rectangle_short_indices_len (ctx);

      if (rectangle_short_indices_len < n_indices)
        {
          uint16_t *short_array;
          uint16_t *p;
          int i, vert_num = 0;
          CoglIndices *old_indices = cogl_context_get_rectangle_short_indices (ctx);

          if (old_indices != NULL)
            g_object_unref (old_indices);
          /* Pick a power of two >= MAX (512, n_indices) */
          if (rectangle_short_indices_len == 0)
            rectangle_short_indices_len = 512;
          while (rectangle_short_indices_len < n_indices)
            rectangle_short_indices_len *= 2;

          /* Over-allocate to generate a whole number of quads */
          p = short_array = g_malloc ((rectangle_short_indices_len
                                       + 5) / 6 * 6
                                      * sizeof (uint16_t));

          /* Fill in the complete quads */
          for (i = 0; i < rectangle_short_indices_len; i += 6)
            {
              *(p++) = vert_num + 0;
              *(p++) = vert_num + 1;
              *(p++) = vert_num + 2;
              *(p++) = vert_num + 0;
              *(p++) = vert_num + 2;
              *(p++) = vert_num + 3;
              vert_num += 4;
            }

          CoglIndices *new_indices = cogl_indices_new (ctx,
                                                       COGL_INDICES_TYPE_UNSIGNED_SHORT,
                                                       short_array,
                                                       rectangle_short_indices_len);
          cogl_context_set_rectangle_short_indices (ctx, new_indices);

          g_free (short_array);
        }

      cogl_context_set_rectangle_short_indices_len (ctx,
                                                    rectangle_short_indices_len);

      return cogl_context_get_rectangle_short_indices (ctx);
    }
}
