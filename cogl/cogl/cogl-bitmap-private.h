/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007 OpenedHand
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

#include <glib.h>

#include "cogl/cogl-buffer.h"
#include "cogl/cogl-bitmap.h"

struct _CoglBitmap
{
  GObject parent_instance;

  /* Pointer back to the context that this bitmap was created with */
  CoglContext *context;

  CoglPixelFormat format;
  int width;
  int height;
  int rowstride;

  uint8_t *data;

  gboolean mapped;
  gboolean bound;

  /* If this is non-null then 'data' is ignored and instead it is
     fetched from this shared bitmap. */
  CoglBitmap *shared_bmp;

  /* If this is non-null then 'data' is treated as an offset into the
     buffer and map will divert to mapping the buffer */
  CoglBuffer *buffer;
};

/* The idea of this function is that it will create a bitmap that
   shares the actual data with another bitmap. This is needed for the
   atlas texture backend because it needs upload a bitmap to a sub
   texture but override the format so that it ignores the premult
   flag. */
CoglBitmap *
_cogl_bitmap_new_shared (CoglBitmap      *shared_bmp,
                         CoglPixelFormat  format,
                         int              width,
                         int              height,
                         int              rowstride);

CoglBitmap *
_cogl_bitmap_convert (CoglBitmap *bmp,
                      CoglPixelFormat dst_format,
                      GError **error);

CoglBitmap *
_cogl_bitmap_convert_for_upload (CoglBitmap *src_bmp,
                                 CoglPixelFormat internal_format,
                                 GError **error);

gboolean
_cogl_bitmap_convert_into_bitmap (CoglBitmap *src_bmp,
                                  CoglBitmap *dst_bmp,
                                  GError **error);

gboolean
_cogl_bitmap_unpremult (CoglBitmap *dst_bmp,
                        GError **error);

gboolean
_cogl_bitmap_premult (CoglBitmap *dst_bmp,
                      GError **error);

gboolean
_cogl_bitmap_convert_premult_status (CoglBitmap *bmp,
                                     CoglPixelFormat dst_format,
                                     GError **error);

gboolean
_cogl_bitmap_copy_subregion (CoglBitmap *src,
                             CoglBitmap *dst,
                             int src_x,
                             int src_y,
                             int dst_x,
                             int dst_y,
                             int width,
                             int height,
                             GError **error);

/* Creates a deep copy of the source bitmap */
CoglBitmap *
_cogl_bitmap_copy (CoglBitmap *src_bmp,
                   GError **error);

void
_cogl_bitmap_set_format (CoglBitmap *bitmap,
                         CoglPixelFormat format);


CoglContext *
_cogl_bitmap_get_context (CoglBitmap *bitmap);
