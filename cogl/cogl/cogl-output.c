/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Red Hat, Inc.
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

#include "cogl/cogl-output-private.h"

#include <string.h>

G_DEFINE_TYPE (CoglOutput, cogl_output, G_TYPE_OBJECT);

static void
cogl_output_dispose (GObject *object)
{
  CoglOutput *output = COGL_OUTPUT (object);

  g_free (output->name);

  G_OBJECT_CLASS (cogl_output_parent_class)->dispose (object);
}

static void
cogl_output_init (CoglOutput *output)
{
}

static void
cogl_output_class_init (CoglOutputClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->dispose = cogl_output_dispose;
}

CoglOutput *
_cogl_output_new (const char *name)
{
  CoglOutput *output;

  output = g_object_new (COGL_TYPE_OUTPUT, NULL);
  output->name = g_strdup (name);

  return output;
}

gboolean
_cogl_output_values_equal (CoglOutput *output,
                           CoglOutput *other)
{
  return memcmp ((const char *)output + G_STRUCT_OFFSET (CoglOutput, x),
                 (const char *)other + G_STRUCT_OFFSET (CoglOutput, x),
                 sizeof (CoglOutput) - G_STRUCT_OFFSET (CoglOutput, x)) == 0;
}

int
cogl_output_get_x (CoglOutput *output)
{
  return output->x;
}

int
cogl_output_get_y (CoglOutput *output)
{
  return output->y;
}

int
cogl_output_get_width (CoglOutput *output)
{
  return output->width;
}

int
cogl_output_get_height (CoglOutput *output)
{
  return output->height;
}

int
cogl_output_get_mm_width (CoglOutput *output)
{
  return output->mm_width;
}

int
cogl_output_get_mm_height (CoglOutput *output)
{
  return output->mm_height;
}

CoglSubpixelOrder
cogl_output_get_subpixel_order (CoglOutput *output)
{
  return output->subpixel_order;
}

float
cogl_output_get_refresh_rate (CoglOutput *output)
{
  return output->refresh_rate;
}
