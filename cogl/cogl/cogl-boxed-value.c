/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2011 Intel Corporation.
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
 */

#include "config.h"

#include <string.h>

#include "cogl/cogl-boxed-value.h"
#include "cogl/cogl-context-private.h"

static void
_cogl_boxed_value_append_type_to_string (GString *buf,
                                         const CoglBoxedValue *bv)
{
  switch (bv->type)
    {
    case COGL_BOXED_INT:
      if (bv->size == 1)
        g_string_append (buf, "int");
      else
        g_string_append_printf (buf, "ivec%i", bv->size);
      return;
    case COGL_BOXED_FLOAT:
      if (bv->size == 1)
        g_string_append (buf, "float");
      else
        g_string_append_printf (buf, "vec%i", bv->size);
      return;
    case COGL_BOXED_MATRIX:
      g_string_append_printf (buf, "mat%i", bv->size);
      return;
    case COGL_BOXED_NONE:
      return;
    }
}

static void
_cogl_boxed_value_append_value_to_string (GString *buf,
                                          const CoglBoxedValue *bv,
                                          int count)
{
  int i, j;
  int offset;

  if (bv->size > 1)
    {
      _cogl_boxed_value_append_type_to_string (buf, bv);
      g_string_append (buf, "(");
    }

  switch (bv->type)
    {
    case COGL_BOXED_INT:
      for (i = 0; i < bv->size; i++)
        {
          if (bv->count > 1)
            g_string_append_printf (buf, "%i, ", bv->v.int_array[(count * bv->size) + i]);
          else
            g_string_append_printf (buf, "%i, ", bv->v.int_value[i]);
        }
      break;
    case COGL_BOXED_FLOAT:
      for (i = 0; i < bv->size; i++)
        {
          if (bv->count > 1)
            g_string_append_printf (buf, "%f, ", bv->v.float_array[(count * bv->size) + i]);
          else
            g_string_append_printf (buf, "%f, ", bv->v.float_value[i]);
        }
      break;
    case COGL_BOXED_MATRIX:
      offset = count * bv->size * bv->size;

      for (i = 0; i < bv->size; i++)
        {
          g_string_append (buf, "(");

          for (j = 0; j < bv->size; j++)
            {
              if (bv->count > 1)
                g_string_append_printf (buf, "%f, ", bv->v.float_array[
                                          offset + (i * bv->size) + j]);
              else
                g_string_append_printf (buf, "%f, ", bv->v.matrix[(i * bv->size) + j]);
            }

          g_string_erase (buf, buf->len - 2, 2);
          g_string_append (buf, "), ");
        }
      break;
    case COGL_BOXED_NONE:
      return;
    }

  g_string_erase (buf, buf->len - 2, 2);

  if (bv->size > 1)
    g_string_append (buf, ")");
}

char *
_cogl_boxed_value_to_string (const CoglBoxedValue *bv,
                             const char *name)
{
  GString *buf;
  int i;

  buf = g_string_new (NULL);
  for (i = 0; i < bv->count; i++)
    {
      _cogl_boxed_value_append_type_to_string (buf, bv);

      g_string_append_printf (buf, " %s", name);

      if (bv->count > 1)
        g_string_append_printf (buf, "[%i] = ", i);
      else
        g_string_append (buf, " = ");

      _cogl_boxed_value_append_value_to_string (buf, bv, i);
    }

  return g_string_free_and_steal (buf);
}

gboolean
_cogl_boxed_value_equal (const CoglBoxedValue *bva,
                         const CoglBoxedValue *bvb)
{
  const void *pa, *pb;

  if (bva == NULL || bvb == NULL)
    return bva == bvb;

  if (bva->type != bvb->type)
    return FALSE;

  switch (bva->type)
    {
    case COGL_BOXED_NONE:
      return TRUE;

    case COGL_BOXED_INT:
      if (bva->size != bvb->size || bva->count != bvb->count)
        return FALSE;

      if (bva->count == 1)
        {
          pa = bva->v.int_value;
          pb = bvb->v.int_value;
        }
      else
        {
          pa = bva->v.int_array;
          pb = bvb->v.int_array;
        }

      return !memcmp (pa, pb, sizeof (int) * bva->size * bva->count);

    case COGL_BOXED_FLOAT:
      if (bva->size != bvb->size || bva->count != bvb->count)
        return FALSE;

      if (bva->count == 1)
        {
          pa = bva->v.float_value;
          pb = bvb->v.float_value;
        }
      else
        {
          pa = bva->v.float_array;
          pb = bvb->v.float_array;
        }

      return !memcmp (pa, pb, sizeof (float) * bva->size * bva->count);

    case COGL_BOXED_MATRIX:
      if (bva->size != bvb->size ||
          bva->count != bvb->count)
        return FALSE;

      if (bva->count == 1)
        {
          pa = bva->v.matrix;
          pb = bvb->v.matrix;
        }
      else
        {
          pa = bva->v.float_array;
          pb = bvb->v.float_array;
        }

      return !memcmp (pa, pb,
                      sizeof (float) * bva->size * bva->size * bva->count);
    }

  g_warn_if_reached ();

  return FALSE;
}

static void
_cogl_boxed_value_array_alloc (CoglBoxedValue *bv,
                               size_t value_size,
                               int count,
                               CoglBoxedType type)
{
  if (count > 1)
    {
      switch (type)
        {
        case COGL_BOXED_INT:
          bv->v.int_array = g_malloc (count * value_size);
          return;

        case COGL_BOXED_FLOAT:
        case COGL_BOXED_MATRIX:
          bv->v.float_array = g_malloc (count * value_size);
          return;

        case COGL_BOXED_NONE:
          return;
        }
    }
}

static void
_cogl_boxed_value_copy_transposed_value (CoglBoxedValue *bv,
                                         int size,
                                         int count,
                                         const float *src)
{
  int value_num;
  int y, x;
  float *dst;
  const float *src_start = src;

  if (count > 1)
    dst = bv->v.float_array;
  else
    dst = bv->v.matrix;

  /* If the value is transposed we'll just transpose it now as it
   * is copied into the boxed value instead of passing TRUE to
   * glUniformMatrix because that is not supported on GLES and it
   * doesn't seem like the GL driver would be able to do anything
   * much smarter than this anyway */

  for (value_num = 0; value_num < count; value_num++)
    {
      src = src_start + value_num * size * size;
      for (y = 0; y < size; y++)
        for (x = 0; x < size; x++)
          *(dst++) = src[y + x * size];
    }
}

static void
_cogl_boxed_value_copy_value (CoglBoxedValue *bv,
                              size_t value_size,
                              int count,
                              const void *value,
                              CoglBoxedType type)
{
  switch (type)
    {
    case COGL_BOXED_INT:
      if (count > 1)
        memcpy (bv->v.int_array, value, count * value_size);
      else
        memcpy (bv->v.int_value, value, value_size);
      return;

    case COGL_BOXED_FLOAT:
      if (count > 1)
        memcpy (bv->v.float_array, value, count * value_size);
      else
        memcpy (bv->v.float_value, value, value_size);
      return;

    case COGL_BOXED_MATRIX:
      if (count > 1)
        memcpy (bv->v.float_array, value, count * value_size);
      else
        memcpy (bv->v.matrix, value, value_size);
      return;

    case COGL_BOXED_NONE:
      return;
    }
}

static void
_cogl_boxed_value_set_x (CoglBoxedValue *bv,
                         int size,
                         int count,
                         CoglBoxedType type,
                         size_t value_size,
                         const void *value,
                         gboolean transpose)
{
  if (bv->count != count ||
      bv->size != size ||
      bv->type != type)
    {
      _cogl_boxed_value_destroy (bv);
      _cogl_boxed_value_array_alloc (bv, value_size, count, type);
    }

  if (transpose)
    _cogl_boxed_value_copy_transposed_value (bv, size, count, value);
  else
    _cogl_boxed_value_copy_value (bv, value_size, count, value, type);

  bv->type = type;
  bv->size = size;
  bv->count = count;
}

void
_cogl_boxed_value_set_1f (CoglBoxedValue *bv,
                          float value)
{
  _cogl_boxed_value_set_x (bv,
                           1, 1, COGL_BOXED_FLOAT,
                           sizeof (float), &value, FALSE);
}

void
_cogl_boxed_value_set_1i (CoglBoxedValue *bv,
                          int value)
{
  _cogl_boxed_value_set_x (bv,
                           1, 1, COGL_BOXED_INT,
                           sizeof (int), &value, FALSE);
}

void
_cogl_boxed_value_set_float (CoglBoxedValue *bv,
                             int n_components,
                             int count,
                             const float *value)
{
  _cogl_boxed_value_set_x (bv,
                           n_components, count,
                           COGL_BOXED_FLOAT,
                           sizeof (float) * n_components, value, FALSE);
}

void
_cogl_boxed_value_set_int (CoglBoxedValue *bv,
                           int n_components,
                           int count,
                           const int *value)
{
  _cogl_boxed_value_set_x (bv,
                           n_components, count,
                           COGL_BOXED_INT,
                           sizeof (int) * n_components, value, FALSE);
}

void
_cogl_boxed_value_set_matrix (CoglBoxedValue *bv,
                              int dimensions,
                              int count,
                              gboolean transpose,
                              const float *value)
{
  _cogl_boxed_value_set_x (bv,
                           dimensions, count,
                           COGL_BOXED_MATRIX,
                           sizeof (float) * dimensions * dimensions,
                           value,
                           transpose);
}

void
_cogl_boxed_value_copy (CoglBoxedValue *dst,
                        const CoglBoxedValue *src)
{
  *dst = *src;

  if (src->count > 1)
    {
      switch (src->type)
        {
        case COGL_BOXED_NONE:
          break;

        case COGL_BOXED_INT:
          dst->v.int_array = g_memdup2 (src->v.int_array,
                                        src->size * src->count * sizeof (int));
          break;

        case COGL_BOXED_FLOAT:
          dst->v.float_array = g_memdup2 (src->v.float_array,
                                          src->size *
                                          src->count *
                                          sizeof (float));
          break;

        case COGL_BOXED_MATRIX:
          dst->v.float_array = g_memdup2 (src->v.float_array,
                                          src->size * src->size *
                                          src->count * sizeof (float));
          break;
        }
    }
}

void
_cogl_boxed_value_destroy (CoglBoxedValue *bv)
{
  if (bv->count > 1)
    {
      switch (bv->type)
        {
        case COGL_BOXED_INT:
          g_free (bv->v.int_array);
          return;

        case COGL_BOXED_FLOAT:
        case COGL_BOXED_MATRIX:
          g_free (bv->v.float_array);
          return;

        case COGL_BOXED_NONE:
          return;
        }
    }
}

void
_cogl_boxed_value_set_uniform (CoglContext *ctx,
                               GLint location,
                               const CoglBoxedValue *value)
{
  CoglDriver *driver = cogl_context_get_driver (ctx);
  CoglDriverClass *driver_klass = COGL_DRIVER_GET_CLASS (driver);

  driver_klass->set_uniform (driver, ctx, location, value);
}
