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

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-pipeline-layer-state.h"

G_BEGIN_DECLS

/**
 * SECTION:meta-texture
 * Interface for high-level textures built from
 * low-level textures like #CoglTexture2D.
 *
 * Cogl helps to make it easy to deal with high level textures such
 * as `CoglAtlasTexture`s, `CoglSubTexture`s,
 * #CoglTexturePixmapX11 textures and #CoglTexture2DSliced textures
 * consistently.
 *
 * A #CoglTexture is a texture that might internally be
 * represented by one or more low-level `CoglTexture`s
 * such as #CoglTexture2D. These low-level textures are the only ones
 * that a GPU really understands but because applications often want
 * more high-level texture abstractions (such as storing multiple
 * textures inside one larger "atlas" texture) it's desirable to be
 * able to deal with these using a common interface.
 *
 * For example the GPU is not able to automatically handle repeating a
 * texture that is part of a larger atlas texture but if you use
 * %COGL_PIPELINE_WRAP_MODE_REPEAT with an atlas texture when drawing
 * with cogl_rectangle() you should see that it "Just Works™" - at
 * least if you don't use multi-texturing. The reason this works is
 * because cogl_rectangle() internally understands the #CoglTexture
 * interface and is able to manually resolve the low-level textures
 * using this interface and by making multiple draw calls it can
 * emulate the texture repeat modes.
 *
 * Cogl doesn't aim to pretend that meta-textures are just like real
 * textures because it would get extremely complex to try and emulate
 * low-level GPU semantics transparently for these textures.  The low
 * level drawing APIs of Cogl, such as cogl_primitive_draw() don't
 * actually know anything about the #CoglTexture interface and its
 * the developer's responsibility to resolve all textures referenced
 * by a #CoglPipeline to low-level textures before drawing.
 *
 * If you want to develop custom primitive APIs like
 * cogl_framebuffer_draw_rectangle() and you want to support drawing
 * with `CoglAtlasTexture`s or `CoglSubTexture`s for
 * example, then you will need to use this #CoglTexture interface
 * to be able to resolve high-level textures into low-level textures
 * before drawing with Cogl's low-level drawing APIs such as
 * cogl_primitive_draw().
 *
 * Most developers won't need to use this interface directly
 * but still it is worth understanding the distinction between
 * low-level and meta textures because you may find other references
 * in the documentation that detail limitations of using
 * meta-textures.
 */

/**
 * CoglTextureForeachCallback:
 * @sub_texture: A low-level #CoglTexture making up part of a
 *               #CoglTexture.
 * @sub_texture_coords: A float 4-tuple ordered like
 *                      (tx1,ty1,tx2,ty2) defining what region of the
 *                      current @sub_texture maps to a sub-region of a
 *                      #CoglTexture. (tx1,ty1) is the top-left
 *                      sub-region coordinate and (tx2,ty2) is the
 *                      bottom-right. These are low-level texture
 *                      coordinates.
 * @meta_coords: A float 4-tuple ordered like (tx1,ty1,tx2,ty2)
 *               defining what sub-region of a #CoglTexture this
 *               low-level @sub_texture maps too. (tx1,ty1) is
 *               the top-left sub-region coordinate and (tx2,ty2) is
 *               the bottom-right. These are high-level meta-texture
 *               coordinates.
 * @user_data: A private pointer passed to
 *             cogl_texture_foreach_in_region().
 *
 * A callback used with cogl_texture_foreach_in_region() to
 * retrieve details of all the low-level `CoglTexture`s that
 * make up a given #CoglTexture.
 */
typedef void (*CoglTextureForeachCallback) (CoglTexture *sub_texture,
                                            const float *sub_texture_coords,
                                            const float *meta_coords,
                                            void        *user_data);

/**
 * cogl_texture_foreach_in_region:
 * @texture: An object implementing the #CoglTexture interface.
 * @tx_1: The top-left x coordinate of the region to iterate
 * @ty_1: The top-left y coordinate of the region to iterate
 * @tx_2: The bottom-right x coordinate of the region to iterate
 * @ty_2: The bottom-right y coordinate of the region to iterate
 * @wrap_s: The wrap mode for the x-axis
 * @wrap_t: The wrap mode for the y-axis
 * @callback: (scope call): A #CoglTextureForeachCallback pointer to be called
 *            for each low-level texture within the specified region.
 * @user_data: A private pointer that is passed to @callback.
 *
 * Allows you to manually iterate the low-level textures that define a
 * given region of a high-level #CoglTexture.
 *
 * For example cogl_texture_2d_sliced_new_with_size() can be used to
 * create a meta texture that may slice a large image into multiple,
 * smaller power-of-two sized textures. These high level textures are
 * not directly understood by a GPU and so this API must be used to
 * manually resolve the underlying textures for drawing.
 *
 * All high level textures (#CoglAtlasTexture, #CoglSubTexture,
 * #CoglTexturePixmapX11, and #CoglTexture2DSliced) can be handled
 * consistently using this interface which greately simplifies
 * implementing primitives that support all texture types.
 *
 * For example if you use the cogl_rectangle() API then Cogl will
 * internally use this API to resolve the low level textures of any
 * meta textures you have associated with CoglPipeline layers.
 *
 * The low level drawing APIs such as cogl_primitive_draw()
 * don't understand the #CoglTexture interface and so it is your
 * responsibility to use this API to resolve all CoglPipeline textures
 * into low-level textures before drawing.
 *
 * For each low-level texture that makes up part of the given region
 * of the @meta_texture, @callback is called specifying how the
 * low-level texture maps to the original region.
 */
COGL_EXPORT void
cogl_texture_foreach_in_region (CoglTexture                *texture,
                                float                       tx_1,
                                float                       ty_1,
                                float                       tx_2,
                                float                       ty_2,
                                CoglPipelineWrapMode        wrap_s,
                                CoglPipelineWrapMode        wrap_t,
                                CoglTextureForeachCallback  callback,
                                void                       *user_data);

G_END_DECLS
