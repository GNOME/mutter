/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009,2010 Intel Corporation.
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
#include <math.h>

#include "cogl/cogl-pixel-format.h"
#include "cogl/cogl-types.h"
#include "mtk/mtk.h"

#include <stdio.h>

/* This is a replacement for the nearbyint function which always
   rounds to the nearest integer. nearbyint is apparently a C99
   function so it might not always be available but also it seems in
   glibc it is defined as a function call so this macro could end up
   faster anyway. We can't just add 0.5f because it will break for
   negative numbers. */
#define COGL_UTIL_NEARBYINT(x) ((int) ((x) < 0.0f ? (x) - 0.5f : (x) + 0.5f))


/* Split Bob Jenkins' One-at-a-Time hash
 *
 * This uses the One-at-a-Time hash algorithm designed by Bob Jenkins
 * but the mixing step is split out so the function can be used in a
 * more incremental fashion.
 */
static inline unsigned int
_cogl_util_one_at_a_time_hash (unsigned int hash,
                               const void *key,
                               size_t bytes)
{
  const unsigned char *p = key;
  size_t i;

  for (i = 0; i < bytes; i++)
    {
      hash += p[i];
      hash += (hash << 10);
      hash ^= (hash >> 6);
    }

  return hash;
}

static inline unsigned int
_cogl_util_one_at_a_time_mix (unsigned int hash)
{
    hash += ( hash << 3 );
    hash ^= ( hash >> 11 );
    hash += ( hash << 15 );

    return hash;
}

static inline void
cogl_region_to_flipped_array (const MtkRegion *region,
                              int              height,
                              int             *rectangles)
{
  int n_rectangles = mtk_region_num_rectangles (region);
  int i;

  for (i = 0; i < n_rectangles; i++)
    {
      MtkRectangle rect = mtk_region_get_rectangle (region, i);
      int *flip_rect = rectangles + 4 * i;

      flip_rect[0] = rect.x;
      flip_rect[1] = height - rect.y - rect.height;
      flip_rect[2] = rect.width;
      flip_rect[3] = rect.height;
    }
}
