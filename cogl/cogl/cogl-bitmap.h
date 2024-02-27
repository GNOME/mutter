/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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

/* XXX: We forward declare CoglBitmap here to allow for circular
 * dependencies between some headers */
typedef struct _CoglBitmap CoglBitmap;

#include "cogl/cogl-types.h"
#include "cogl/cogl-buffer.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-pixel-buffer.h"
#include "cogl/cogl-pixel-format.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CoglBitmap:
 *
 * Functions for loading images
 *
 * Cogl allows loading image data into memory as CoglBitmaps without
 * loading them immediately into GPU textures.
 */

#define COGL_TYPE_BITMAP (cogl_bitmap_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglBitmap,
                      cogl_bitmap,
                      COGL,
                      BITMAP,
                      GObject)

/**
 * cogl_bitmap_new_from_buffer:
 * @buffer: A #CoglBuffer containing image data
 * @format: The #CoglPixelFormat defining the format of the image data
 *          in the given @buffer.
 * @width: The width of the image data in the given @buffer.
 * @height: The height of the image data in the given @buffer.
 * @rowstride: The rowstride in bytes of the image data in the given @buffer.
 * @offset: The offset into the given @buffer to the first pixel that
 *          should be considered part of the #CoglBitmap.
 *
 * Wraps some image data that has been uploaded into a #CoglBuffer as
 * a #CoglBitmap. The data is not copied in this process.
 *
 * Return value: (transfer full): a #CoglBitmap encapsulating the given @buffer.
 */
COGL_EXPORT CoglBitmap *
cogl_bitmap_new_from_buffer (CoglBuffer *buffer,
                             CoglPixelFormat format,
                             int width,
                             int height,
                             int rowstride,
                             int offset);

/**
 * cogl_bitmap_new_with_size:
 * @context: A #CoglContext
 * @width: width of the bitmap in pixels
 * @height: height of the bitmap in pixels
 * @format: the format of the pixels the array will store
 *
 * Creates a new #CoglBitmap with the given width, height and format.
 * The initial contents of the bitmap are undefined.
 *
 * The data for the bitmap will be stored in a newly created
 * #CoglPixelBuffer. You can get a pointer to the pixel buffer using
 * [method@Cogl.Bitmap.get_buffer]. The #CoglBuffer API can then be
 * used to fill the bitmap with data.
 *
 * Cogl will try its best to provide a hardware array you can
 * map, write into and effectively do a zero copy upload when creating
 * a texture from it with cogl_texture_new_from_bitmap(). For various
 * reasons, such arrays are likely to have a stride larger than width
 * * bytes_per_pixel. The user must take the stride into account when
 * writing into it. The stride can be retrieved with
 * [method@Cogl.Bitmap.get_rowstride].
 *
 * Return value: (transfer full): a #CoglPixelBuffer representing the
 *               newly created array or %NULL on failure
 */
COGL_EXPORT CoglBitmap *
cogl_bitmap_new_with_size (CoglContext *context,
                           unsigned int width,
                           unsigned int height,
                           CoglPixelFormat format);

/**
 * cogl_bitmap_new_for_data:
 * @context: A #CoglContext
 * @width: The width of the bitmap.
 * @height: The height of the bitmap.
 * @format: The format of the pixel data.
 * @rowstride: The rowstride of the bitmap (the number of bytes from
 *   the start of one row of the bitmap to the next).
 * @data: (array) (transfer full): A pointer to the data. The bitmap will take
 *   ownership of this data.
 *
 * Creates a bitmap using some existing data. The data is not copied
 * so the application must keep the buffer alive for the lifetime of
 * the #CoglBitmap. This can be used for example with
 * [method@Cogl.Framebuffer.read_pixels_into_bitmap] to read data directly
 * into an application buffer with the specified rowstride.
 *
 * Return value: (transfer full): A new #CoglBitmap.
 */
COGL_EXPORT CoglBitmap *
cogl_bitmap_new_for_data (CoglContext *context,
                          int width,
                          int height,
                          CoglPixelFormat format,
                          int rowstride,
                          uint8_t *data);

/**
 * cogl_bitmap_get_format:
 * @bitmap: A #CoglBitmap
 *
 * Return value: the #CoglPixelFormat that the data for the bitmap is in.
 */
COGL_EXPORT CoglPixelFormat
cogl_bitmap_get_format (CoglBitmap *bitmap);

/**
 * cogl_bitmap_get_width:
 * @bitmap: A #CoglBitmap
 *
 * Return value: the width of the bitmap
 */
COGL_EXPORT int
cogl_bitmap_get_width (CoglBitmap *bitmap);

/**
 * cogl_bitmap_get_height:
 * @bitmap: A #CoglBitmap
 *
 * Return value: the height of the bitmap
 */
COGL_EXPORT int
cogl_bitmap_get_height (CoglBitmap *bitmap);

/**
 * cogl_bitmap_get_rowstride:
 * @bitmap: A #CoglBitmap
 *
 * Return value: the rowstride of the bitmap. This is the number of
 *   bytes between the address of start of one row to the address of the
 *   next row in the image.
 */
COGL_EXPORT int
cogl_bitmap_get_rowstride (CoglBitmap *bitmap);

/**
 * cogl_bitmap_get_buffer:
 * @bitmap: A #CoglBitmap
 *
 * Return value: (transfer none): the #CoglPixelBuffer that this
 *   buffer uses for storage.
 */
COGL_EXPORT CoglPixelBuffer *
cogl_bitmap_get_buffer (CoglBitmap *bitmap);

/**
 * COGL_BITMAP_ERROR:
 *
 * #GError domain for bitmap errors.
 */
#define COGL_BITMAP_ERROR (cogl_bitmap_error_quark ())

/**
 * CoglBitmapError:
 * @COGL_BITMAP_ERROR_FAILED: Generic failure code, something went
 *   wrong.
 * @COGL_BITMAP_ERROR_UNKNOWN_TYPE: Unknown image type.
 * @COGL_BITMAP_ERROR_CORRUPT_IMAGE: An image file was broken somehow.
 *
 * Error codes that can be thrown when performing bitmap
 * operations.
 */
typedef enum
{
  COGL_BITMAP_ERROR_FAILED,
  COGL_BITMAP_ERROR_UNKNOWN_TYPE,
  COGL_BITMAP_ERROR_CORRUPT_IMAGE
} CoglBitmapError;

COGL_EXPORT
uint32_t cogl_bitmap_error_quark (void);

G_END_DECLS
