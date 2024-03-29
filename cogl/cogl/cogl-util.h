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

unsigned int
_cogl_util_one_at_a_time_mix (unsigned int hash);


#define _cogl_util_ffsl __builtin_ffsl

static inline unsigned int
_cogl_util_fls (unsigned int n)
{
   return n == 0 ? 0 : sizeof (unsigned int) * 8 - __builtin_clz (n);
}

#define _cogl_util_popcountl __builtin_popcountl

/* Match a CoglPixelFormat according to channel masks, color depth,
 * bits per pixel and byte order. These information are provided by
 * the Visual and XImage structures.
 *
 * If no specific pixel format could be found, COGL_PIXEL_FORMAT_ANY
 * is returned.
 */
CoglPixelFormat
_cogl_util_pixel_format_from_masks (unsigned long r_mask,
                                    unsigned long g_mask,
                                    unsigned long b_mask,
                                    int depth, int bpp,
                                    int byte_order);

/* _COGL_STATIC_ASSERT:
 * @expression: An expression to assert evaluates to true at compile
 *              time.
 * @message: A message to print to the console if the assertion fails
 *           at compile time.
 *
 * Allows you to assert that an expression evaluates to true at
 * compile time and aborts compilation if not. If possible message
 * will also be printed if the assertion fails.
 */
#define _COGL_STATIC_ASSERT(EXPRESSION, MESSAGE) \
  _Static_assert (EXPRESSION, MESSAGE);

static inline void
_cogl_util_scissor_intersect (int rect_x0,
                              int rect_y0,
                              int rect_x1,
                              int rect_y1,
                              int *scissor_x0,
                              int *scissor_y0,
                              int *scissor_x1,
                              int *scissor_y1)
{
  *scissor_x0 = MAX (*scissor_x0, rect_x0);
  *scissor_y0 = MAX (*scissor_y0, rect_y0);
  *scissor_x1 = MIN (*scissor_x1, rect_x1);
  *scissor_y1 = MIN (*scissor_y1, rect_y1);
}
