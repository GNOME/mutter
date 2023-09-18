/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2009 Intel Corporation.
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

#include "cogl/cogl-texture-private.h"
#include "cogl/cogl-rectangle-map.h"
#include "cogl/cogl-atlas.h"
#include "cogl/cogl-atlas-texture.h"

struct _CoglAtlasTexture
{
  CoglTexture parent_instance;

  /* The format that the texture is in. This isn't necessarily the
     same format as the atlas texture because we can store
     pre-multiplied and non-pre-multiplied textures together */
  CoglPixelFormat       internal_format;

  /* The rectangle that was used to add this texture to the
     atlas. This includes the 1-pixel border */
  CoglRectangleMapEntry rectangle;

  /* The atlas that this texture is in. If the texture is no longer in
     an atlas then this will be NULL. A reference is taken on the
     atlas by the texture (but not vice versa so there is no cycle) */
  CoglAtlas            *atlas;

  /* Either a CoglSubTexture representing the atlas region for easy
   * rendering or if the texture has been migrated out of the atlas it
   * may be some other texture type such as CoglTexture2D */
  CoglTexture          *sub_texture;
};

struct _CoglAtlasTextureClass
{
   CoglTextureClass parent_class;
};

COGL_EXPORT void
_cogl_atlas_texture_add_reorganize_callback (CoglContext *ctx,
                                             GHookFunc callback,
                                             void *user_data);

COGL_EXPORT void
_cogl_atlas_texture_remove_reorganize_callback (CoglContext *ctx,
                                                GHookFunc callback,
                                                void *user_data);
