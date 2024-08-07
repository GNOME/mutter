/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010,2011 Intel Corporation.
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
 */

#pragma once

#include "cogl/cogl-atlas.h"
#include "cogl/cogl-rectangle-map.h"

struct _CoglAtlas
{
  GObject parent_instance;

  CoglRectangleMap *map;
  CoglContext *context;

  CoglTexture *texture;
  CoglPixelFormat texture_format;
  CoglAtlasFlags flags;

  CoglAtlasUpdatePositionCallback update_position_cb;

  GHookList pre_reorganize_callbacks;
  GHookList post_reorganize_callbacks;
};

void
_cogl_atlas_remove (CoglAtlas          *atlas,
                    const MtkRectangle *rectangle);

CoglTexture *
_cogl_atlas_copy_rectangle (CoglAtlas       *atlas,
                            int              x,
                            int              y,
                            int              width,
                            int              height,
                            CoglPixelFormat  format);
