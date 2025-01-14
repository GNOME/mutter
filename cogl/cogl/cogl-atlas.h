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

#include "cogl/cogl-texture.h"

typedef void
(* CoglAtlasUpdatePositionCallback) (void               *user_data,
                                     CoglTexture        *new_texture,
                                     const MtkRectangle *rect);

typedef enum
{
  COGL_ATLAS_CLEAR_TEXTURE     = (1 << 0),
  COGL_ATLAS_DISABLE_MIGRATION = (1 << 1)
} CoglAtlasFlags;

#define COGL_TYPE_ATLAS (cogl_atlas_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglAtlas,
                      cogl_atlas,
                      COGL,
                      ATLAS,
                      GObject)


/**
 * cogl_atlas_new: (skip)
 */
COGL_EXPORT CoglAtlas *
cogl_atlas_new (CoglContext                    *context,
                CoglPixelFormat                 texture_format,
                CoglAtlasFlags                  flags,
                CoglAtlasUpdatePositionCallback update_position_cb);

COGL_EXPORT gboolean
cogl_atlas_reserve_space (CoglAtlas             *atlas,
                          unsigned int           width,
                          unsigned int           height,
                          void                  *user_data);

/**
 * cogl_atlas_add_reorganize_callback: (skip)
 */
COGL_EXPORT void
cogl_atlas_add_reorganize_callback (CoglAtlas            *atlas,
                                    GHookFunc             pre_callback,
                                    GHookFunc             post_callback,
                                    void                 *user_data);
