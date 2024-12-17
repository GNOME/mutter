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

#pragma once

#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include "cogl/cogl-context.h"

typedef struct _CoglXlibOutput
{
  char *name;
  int x;
  int y;
  int width;
  int height;
  int mm_width;
  int mm_height;
  float refresh_rate;
  SubpixelOrder subpixel_order;
} CoglXlibOutput;

typedef struct _CoglXlibRenderer
{
  int damage_base;
  int randr_base;

  Display *xdpy;

  GList *outputs;

  unsigned long outputs_update_serial;
} CoglXlibRenderer;

gboolean
_cogl_xlib_renderer_connect (CoglRenderer *renderer, GError **error);

void
_cogl_xlib_renderer_disconnect (CoglRenderer *renderer);

CoglXlibRenderer *
_cogl_xlib_renderer_get_data (CoglRenderer *renderer);

float
_cogl_xlib_renderer_refresh_rate_for_rectangle (CoglRenderer *renderer,
                                                int           x,
                                                int           y,
                                                int           width,
                                                int           height);

Display *
cogl_xlib_renderer_get_display (CoglRenderer *renderer);
