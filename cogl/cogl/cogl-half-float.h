/*
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/* This source file is originally from Mesa (src/util/half_float.h). */

#ifndef COGL_HALF_FLOAT_H
#define COGL_HALF_FLOAT_H

#include "cogl/cogl-types.h"

#include <stdint.h>
#include <string.h>

#include "cogl/cogl-cpu-caps.h"
#include "cogl/cogl-soft-float.h"

#ifdef __x86_64
#include <immintrin.h>
#endif

#define FP16_ONE ((uint16_t) 0x3c00)
#define FP16_ZERO ((uint16_t) 0)

COGL_EXPORT
uint16_t cogl_float_to_half_slow (float val);

COGL_EXPORT
float cogl_half_to_float_slow (uint16_t val);

COGL_EXPORT
uint8_t cogl_half_to_unorm8 (uint16_t v);

COGL_EXPORT
uint16_t cogl_uint16_div_64k_to_half (uint16_t v);

COGL_EXPORT
uint16_t cogl_float_to_float16_rtz_slow (float val);

static inline uint16_t
cogl_float_to_half (float val)
{
#ifdef __x86_64
   if (cogl_cpu_has_cap (COGL_CPU_CAP_F16C))
     {
      __m128 in = {val};
      __m128i out;

      /* $0 = round to nearest */
      __asm volatile ("vcvtps2ph $0, %1, %0" : "=v" (out) : "v" (in));
      return out[0];
   }
#endif
   return cogl_float_to_half_slow (val);
}

static inline float
cogl_half_to_float (uint16_t val)
{
#ifdef __x86_64
   if (cogl_cpu_has_cap (COGL_CPU_CAP_F16C))
     {
      __m128i in = {val};
      __m128 out;

      __asm volatile ("vcvtph2ps %1, %0" : "=v" (out) : "v" (in));
      return out[0];
   }
#endif
   return cogl_half_to_float_slow (val);
}

static inline uint16_t
cogl_float_to_float16_rtz (float val)
{
#ifdef __x86_64
   if (cogl_cpu_has_cap (COGL_CPU_CAP_F16C))
     {
      __m128 in = {val};
      __m128i out;

      /* $3 = round towards zero (truncate) */
      __asm volatile ("vcvtps2ph $3, %1, %0" : "=v" (out) : "v" (in));
      return out[0];
   }
#endif
   return cogl_float_to_float16_rtz_slow (val);
}

static inline uint16_t
cogl_float_to_float16_rtne (float val)
{
   return cogl_float_to_half (val);
}

static inline gboolean
cogl_half_is_negative (uint16_t h)
{
   return !!(h & 0x8000);
}

#endif /* COGL_HALF_FLOAT_H */
