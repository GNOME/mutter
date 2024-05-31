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

#pragma once

#include "cogl/cogl-types.h"

#include <glib-object.h>

#define COGL_TYPE_OUTPUT (cogl_output_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglOutput,
                      cogl_output,
                      COGL,
                      OUTPUT,
                      GObject)

typedef enum
{
  COGL_SUBPIXEL_ORDER_UNKNOWN,
  COGL_SUBPIXEL_ORDER_NONE,
  COGL_SUBPIXEL_ORDER_HORIZONTAL_RGB,
  COGL_SUBPIXEL_ORDER_HORIZONTAL_BGR,
  COGL_SUBPIXEL_ORDER_VERTICAL_RGB,
  COGL_SUBPIXEL_ORDER_VERTICAL_BGR
} CoglSubpixelOrder;

struct _CoglOutput
{
  GObject parent_instance;

  char *name;

  int x; /* Must be first field for _cogl_output_values_equal() */
  int y;
  int width;
  int height;
  int mm_width;
  int mm_height;
  float refresh_rate;
  CoglSubpixelOrder subpixel_order;
};

CoglOutput *_cogl_output_new (const char *name);
gboolean _cogl_output_values_equal (CoglOutput *output,
                                    CoglOutput *other);
