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
 * Authors:
 *   Robert Bragg <robert@linux.intel.com>
 */

#pragma once

#include "cogl/cogl-context.h"
#include "cogl/cogl-types.h"

/**
 * CoglTexture2DSliced:
 *
 * Functions for creating and manipulating 2D meta textures
 * that may internally be comprised of multiple 2D textures
 * with power-of-two sizes.
 *
 * These functions allow high-level meta textures to be allocated
 * that may internally be comprised of multiple 2D texture
 * "slices" with power-of-two sizes.
 *
 * This API can be useful when working with GPUs that don't have
 * native support for non-power-of-two textures or if you want to load
 * a texture that is larger than the GPUs maximum texture size limits.
 *
 * The algorithm for slicing works by first trying to map a virtual
 * size to the next larger power-of-two size and then seeing how many
 * wasted pixels that would result in. For example if you have a
 * virtual texture that's 259 texels wide, the next pot size = 512 and
 * the amount of waste would be 253 texels. If the amount of waste is
 * above a max-waste threshold then we would next slice that texture
 * into one that's 256 texels and then looking at how many more texels
 * remain unallocated after that we choose the next power-of-two size.
 * For the example of a 259 texel image that would mean having a 256
 * texel wide texture, leaving 3 texels unallocated so we'd then
 * create a 4 texel wide texture - now there is only one texel of
 * waste. The algorithm continues to slice the right most textures
 * until the amount of waste is less than or equal to a specified
 * max-waste threshold. The same logic for slicing from left to right
 * is also applied from top to bottom.
 */
#define COGL_TYPE_TEXTURE_2D_SLICED            (cogl_texture_2d_sliced_get_type ())
#define COGL_TEXTURE_2D_SLICED(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_TEXTURE_2D_SLICED, CoglTexture2DSliced))
#define COGL_TEXTURE_2D_SLICED_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_TEXTURE_2D_SLICED, CoglTexture2DSliced const))
#define COGL_TEXTURE_2D_SLICED_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COGL_TYPE_TEXTURE_2D_SLICED, CoglTexture2DSlicedClass))
#define COGL_IS_TEXTURE_2D_SLICED(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COGL_TYPE_TEXTURE_2D_SLICED))
#define COGL_IS_TEXTURE_2D_SLICED_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COGL_TYPE_TEXTURE_2D_SLICED))
#define COGL_TEXTURE_2D_SLICED_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COGL_TYPE_TEXTURE_2D_SLICED, CoglTexture2DSlicedClass))

typedef struct _CoglTexture2DSlicedClass CoglTexture2DSlicedClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglTexture2DSliced, g_object_unref)

COGL_EXPORT
GType               cogl_texture_2d_sliced_get_type       (void) G_GNUC_CONST;

/**
 * cogl_texture_2d_sliced_new_with_size:
 * @ctx: A #CoglContext
 * @width: The virtual width of your sliced texture.
 * @height: The virtual height of your sliced texture.
 * @max_waste: The threshold of how wide a strip of wasted texels
 *             are allowed along the right and bottom textures before
 *             they must be sliced to reduce the amount of waste. A
 *             negative can be passed to disable slicing.
 *
 * Creates a #CoglTexture2DSliced that may internally be comprised of
 * 1 or more #CoglTexture2D textures depending on GPU limitations.
 * For example if the GPU only supports power-of-two sized textures
 * then a sliced texture will turn a non-power-of-two size into a
 * combination of smaller power-of-two sized textures. If the
 * requested texture size is larger than is supported by the hardware
 * then the texture will be sliced into smaller textures that can be
 * accessed by the hardware.
 *
 * @max_waste is used as a threshold for recursively slicing the
 * right-most or bottom-most slices into smaller sizes until the
 * wasted padding at the bottom and right of the textures is less than
 * specified. A negative @max_waste will disable slicing.
 *
 * The storage for the texture is not allocated before this function
 * returns. You can call cogl_texture_allocate() to explicitly
 * allocate the underlying storage or let Cogl automatically allocate
 * storage lazily.
 *
 * It's possible for the allocation of a sliced texture to fail
 * later due to impossible slicing constraints if a negative
 * @max_waste value is given. If the given virtual texture size size
 * is larger than is supported by the hardware but slicing is disabled
 * the texture size would be too large to handle.
 *
 * Returns: (transfer full): A new #CoglTexture2DSliced object with no storage
 *          allocated yet.
 */
COGL_EXPORT CoglTexture *
cogl_texture_2d_sliced_new_with_size (CoglContext *ctx,
                                      int width,
                                      int height,
                                      int max_waste);


/**
 * cogl_texture_2d_sliced_new_from_bitmap:
 * @bmp: A #CoglBitmap
 * @max_waste: The threshold of how wide a strip of wasted texels
 *             are allowed along the right and bottom textures before
 *             they must be sliced to reduce the amount of waste. A
 *             negative can be passed to disable slicing.
 *
 * Creates a new #CoglTexture2DSliced texture based on data residing
 * in a bitmap.
 *
 * A #CoglTexture2DSliced may internally be comprised of 1 or more
 * #CoglTexture2D textures depending on GPU limitations.  For example
 * if the GPU only supports power-of-two sized textures then a sliced
 * texture will turn a non-power-of-two size into a combination of
 * smaller power-of-two sized textures. If the requested texture size
 * is larger than is supported by the hardware then the texture will
 * be sliced into smaller textures that can be accessed by the
 * hardware.
 *
 * @max_waste is used as a threshold for recursively slicing the
 * right-most or bottom-most slices into smaller sizes until the
 * wasted padding at the bottom and right of the textures is less than
 * specified. A negative @max_waste will disable slicing.
 *
 * The storage for the texture is not allocated before this function
 * returns. You can call cogl_texture_allocate() to explicitly
 * allocate the underlying storage or let Cogl automatically allocate
 * storage lazily.
 *
 * It's possible for the allocation of a sliced texture to fail
 * later due to impossible slicing constraints if a negative
 * @max_waste value is given. If the given virtual texture size is
 * larger than is supported by the hardware but slicing is disabled
 * the texture size would be too large to handle.
 *
 * Return value: (transfer full): A newly created #CoglTexture2DSliced
 *               or %NULL on failure and @error will be updated.
 */
COGL_EXPORT CoglTexture *
cogl_texture_2d_sliced_new_from_bitmap (CoglBitmap *bmp,
                                        int         max_waste);
