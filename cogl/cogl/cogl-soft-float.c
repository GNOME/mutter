/*
 * License for Berkeley SoftFloat Release 3e
 *
 * John R. Hauser
 * 2018 January 20
 *
 * The following applies to the whole of SoftFloat Release 3e as well as to
 * each source file individually.
 *
 * Copyright 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018 The Regents of the
 * University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions, and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions, and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the University nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS", AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * The functions listed in this file are modified versions of the ones
 * from the Berkeley SoftFloat 3e Library.
 *
 * Their implementation correctness has been checked with the Berkeley
 * TestFloat Release 3e tool for x86_64.
 */

#include "config.h"

#include "cogl/cogl-soft-float.h"

#if G_BYTE_ORDER == G_BIG_ENDIAN
#define word_incr -1
#define index_word(total, n) ((total) - 1 - (n))
#define index_word_hi(total) 0
#define index_word_lo(total) ((total) - 1)
#define index_multiword_hi(total, n) 0
#define index_multiword_lo(total, n) ((total) - (n))
#define index_multiword_hi_but(total, n) 0
#define index_multiword_lo_but(total, n) (n)
#else
#define word_incr 1
#define index_word(total, n) (n)
#define index_word_hi(total) ((total) - 1)
#define index_word_lo(total) 0
#define index_multiword_hi(total, n) ((total) - (n))
#define index_multiword_lo(total, n) 0
#define index_multiword_hi_but(total, n) (n)
#define index_multiword_lo_but(total, n) 0
#endif

typedef union { double f; int64_t i; uint64_t u; } di_type;
typedef union { float f; int32_t i; uint32_t u; } fi_type;

const uint8_t count_leading_zeros8[256] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/**
 * \brief Shifts 'a' right by the number of bits given in 'dist', which must not
 * be zero.  If any nonzero bits are shifted off, they are "jammed" into the
 * least-significant bit of the shifted value by setting the least-significant
 * bit to 1.  This shifted-and-jammed value is returned.
 * The value of 'dist' can be arbitrarily large.  In particular, if 'dist' is
 * greater than 64, the result will be either 0 or 1, depending on whether 'a'
 * is zero or nonzero.
 *
 * From softfloat_shiftRightJam64 ()
 */
static inline uint64_t
cogl_shift_right_jam64 (uint64_t a,
                        uint32_t dist)
{
  return
    (dist < 63) ? a >> dist | ((uint64_t) (a << (-dist & 63)) != 0) : (a != 0);
}

/**
 * \brief Shifts 'a' right by the number of bits given in 'dist', which must not be
 * zero.  If any nonzero bits are shifted off, they are "jammed" into the
 * least-significant bit of the shifted value by setting the least-significant
 * bit to 1.  This shifted-and-jammed value is returned.
 * The value of 'dist' can be arbitrarily large.  In particular, if 'dist' is
 * greater than 32, the result will be either 0 or 1, depending on whether 'a'
 * is zero or nonzero.
 *
 * From softfloat_shiftRightJam32 ()
 */
static inline uint32_t
cogl_shift_right_jam32 (uint32_t a,
                        uint16_t dist)
{
  return
    (dist < 31) ? a >> dist | ((uint32_t) (a << (-dist & 31)) != 0) : (a != 0);
}

/**
 * \brief Extracted from softfloat_roundPackToF64 ()
 */
static inline double
cogl_roundtozero_f64 (int64_t s,
                      int64_t e,
                      int64_t m)
{
  di_type result;

  if ((uint64_t) e >= 0x7fd)
    {
      if (e < 0)
        {
          m = cogl_shift_right_jam64 (m, -e);
          e = 0;
        }
      else if ((e > 0x7fd) || (0x8000000000000000 <= m))
        {
          e = 0x7ff;
          m = 0;
          result.u = (s << 63) + (e << 52) + m;
          result.u -= 1;
          return result.f;
        }
    }

  m >>= 10;
  if (m == 0)
    e = 0;

  result.u = (s << 63) + (e << 52) + m;
  return result.f;
}

/**
 * \brief Extracted from softfloat_roundPackToF16 ()
 */
static inline uint16_t
cogl_roundtozero_f16 (int16_t s,
                      int16_t e,
                      int16_t m)
{
  if ((uint16_t) e >= 0x1d)
    {
      if (e < 0)
        {
          m = cogl_shift_right_jam32 (m, -e);
          e = 0;
        }
      else if (e > 0x1d)
        {
          e = 0x1f;
          m = 0;
          return (s << 15) + (e << 10) + m - 1;
        }
    }

  m >>= 4;
  if (m == 0)
    e = 0;

  return (s << 15) + (e << 10) + m;
}

/**
 * \brief Calculate a + b but rounding to zero.
 *
 * Notice that this mainly differs from the original Berkeley SoftFloat 3e
 * implementation in that we don't really treat NaNs, Zeroes nor the
 * signalling flags. Any NaN is good for us and the sign of the Zero is not
 * important.
 *
 * From f64_add ()
 */
double
cogl_double_add_rtz (double a,
                     double b)
{
  const di_type a_di = {a};
  uint64_t a_flt_m = a_di.u & 0x0fffffffffffff;
  uint64_t a_flt_e = (a_di.u >> 52) & 0x7ff;
  uint64_t a_flt_s = (a_di.u >> 63) & 0x1;
  const di_type b_di = {b};
  uint64_t b_flt_m = b_di.u & 0x0fffffffffffff;
  uint64_t b_flt_e = (b_di.u >> 52) & 0x7ff;
  uint64_t b_flt_s = (b_di.u >> 63) & 0x1;
  int64_t s, e, m = 0;

  s = a_flt_s;

  const int64_t exp_diff = a_flt_e - b_flt_e;

  /* Handle special cases */

  if (a_flt_s != b_flt_s)
    {
      return cogl_double_sub_rtz (a, -b);
    }
  else if ((a_flt_e == 0) && (a_flt_m == 0))
    {
      /* 'a' is zero, return 'b' */
      return b;
    }
  else if ((b_flt_e == 0) && (b_flt_m == 0))
    {
      /* 'b' is zero, return 'a' */
      return a;
    }
  else if (a_flt_e == 0x7ff && a_flt_m != 0)
    {
      /* 'a' is a NaN, return NaN */
      return a;
    }
  else if (b_flt_e == 0x7ff && b_flt_m != 0)
    {
      /* 'b' is a NaN, return NaN */
      return b;
    }
  else if (a_flt_e == 0x7ff && a_flt_m == 0)
    {
      /* Inf + x = Inf */
      return a;
    }
  else if (b_flt_e == 0x7ff && b_flt_m == 0)
    {
      /* x + Inf = Inf */
      return b;
    }
  else if (exp_diff == 0 && a_flt_e == 0)
    {
      di_type result_di;
      result_di.u = a_di.u + b_flt_m;
      return result_di.f;
    }
  else if (exp_diff == 0)
    {
      e = a_flt_e;
      m = 0x0020000000000000 + a_flt_m + b_flt_m;
      m <<= 9;
    }
  else if (exp_diff < 0)
    {
      a_flt_m <<= 9;
      b_flt_m <<= 9;
      e = b_flt_e;

      if (a_flt_e != 0)
        a_flt_m += 0x2000000000000000;
      else
        a_flt_m <<= 1;

      a_flt_m = cogl_shift_right_jam64 (a_flt_m, -exp_diff);
      m = 0x2000000000000000 + a_flt_m + b_flt_m;
      if (m < 0x4000000000000000)
        {
          --e;
          m <<= 1;
        }
    }
  else
    {
      a_flt_m <<= 9;
      b_flt_m <<= 9;
      e = a_flt_e;

      if (b_flt_e != 0)
        b_flt_m += 0x2000000000000000;
      else
        b_flt_m <<= 1;

      b_flt_m = cogl_shift_right_jam64 (b_flt_m, exp_diff);
      m = 0x2000000000000000 + a_flt_m + b_flt_m;
      if (m < 0x4000000000000000)
        {
          --e;
          m <<= 1;
        }
    }

  return cogl_roundtozero_f64 (s, e, m);
}

/**
 * \brief Returns the number of leading 0 bits before the most-significant 1 bit of
 * 'a'.  If 'a' is zero, 64 is returned.
 */
static inline unsigned
cogl_count_leading_zeros64 (uint64_t a)
{
  return __builtin_clzll (a);
}

static inline double
cogl_norm_round_pack_f64 (int64_t s,
                          int64_t e,
                          int64_t m)
{
  int8_t shift_dist;

  shift_dist = cogl_count_leading_zeros64 (m) - 1;
  e -= shift_dist;
  if ((10 <= shift_dist) && ((unsigned) e < 0x7fd))
    {
      di_type result;
      result.u = (s << 63) + ((m ? e : 0) << 52) + (m << (shift_dist - 10));
      return result.f;
    }
  else
    {
      return cogl_roundtozero_f64 (s, e, m << shift_dist);
    }
}

/* Calculate a - b but rounding to zero.
 *
 * Notice that this mainly differs from the original Berkeley SoftFloat 3e
 * implementation in that we don't really treat NaNs, Zeroes nor the
 * signalling flags. Any NaN is good for us and the sign of the Zero is not
 * important.
 *
 * From f64_sub ()
 */
double
cogl_double_sub_rtz (double a,
                     double b)
{
  const di_type a_di = {a};
  uint64_t a_flt_m = a_di.u & 0x0fffffffffffff;
  uint64_t a_flt_e = (a_di.u >> 52) & 0x7ff;
  uint64_t a_flt_s = (a_di.u >> 63) & 0x1;
  const di_type b_di = {b};
  uint64_t b_flt_m = b_di.u & 0x0fffffffffffff;
  uint64_t b_flt_e = (b_di.u >> 52) & 0x7ff;
  uint64_t b_flt_s = (b_di.u >> 63) & 0x1;
  int64_t s, e, m = 0;
  int64_t m_diff = 0;
  unsigned shift_dist = 0;

  s = a_flt_s;

  const int64_t exp_diff = a_flt_e - b_flt_e;

  /* Handle special cases */

  if (a_flt_s != b_flt_s)
    {
      return cogl_double_add_rtz (a, -b);
    }
  else if ((a_flt_e == 0) && (a_flt_m == 0))
    {
      /* 'a' is zero, return '-b' */
      return -b;
    }
  else if ((b_flt_e == 0) && (b_flt_m == 0))
    {
      /* 'b' is zero, return 'a' */
      return a;
    }
  else if (a_flt_e == 0x7ff && a_flt_m != 0)
    {
      /* 'a' is a NaN, return NaN */
      return a;
    }
  else if (b_flt_e == 0x7ff && b_flt_m != 0)
    {
      /* 'b' is a NaN, return NaN */
      return b;
    }
  else if (a_flt_e == 0x7ff && a_flt_m == 0)
    {
      if (b_flt_e == 0x7ff && b_flt_m == 0)
        {
          /* Inf - Inf =  NaN */
          di_type result;
          e = 0x7ff;
          result.u = (s << 63) + (e << 52) + 0x1;
          return result.f;
        }
      /* Inf - x = Inf */
      return a;
    }
  else if (b_flt_e == 0x7ff && b_flt_m == 0)
    {
      /* x - Inf = -Inf */
      return -b;
    }
  else if (exp_diff == 0)
    {
      m_diff = a_flt_m - b_flt_m;

      if (m_diff == 0)
        return 0;
      if (a_flt_e)
        --a_flt_e;
      if (m_diff < 0)
        {
          s = !s;
          m_diff = -m_diff;
        }

      shift_dist = cogl_count_leading_zeros64 (m_diff) - 11;
      e = a_flt_e - shift_dist;
      if (e < 0)
        {
          shift_dist = a_flt_e;
          e = 0;
        }

      di_type result;
      result.u = (s << 63) + (e << 52) + (m_diff << shift_dist);
      return result.f;
    }
  else if (exp_diff < 0)
    {
      a_flt_m <<= 10;
      b_flt_m <<= 10;
      s = !s;

      a_flt_m += (a_flt_e) ? 0x4000000000000000 : a_flt_m;
      a_flt_m = cogl_shift_right_jam64 (a_flt_m, -exp_diff);
      b_flt_m |= 0x4000000000000000;
      e = b_flt_e;
      m = b_flt_m - a_flt_m;
    }
  else
    {
      a_flt_m <<= 10;
      b_flt_m <<= 10;

      b_flt_m += (b_flt_e) ? 0x4000000000000000 : b_flt_m;
      b_flt_m = cogl_shift_right_jam64 (b_flt_m, exp_diff);
      a_flt_m |= 0x4000000000000000;
      e = a_flt_e;
      m = a_flt_m - b_flt_m;
    }

  return cogl_norm_round_pack_f64 (s, e - 1, m);
}

/**
 * \brief Converts from 32bits to 16bits float and rounds the result to zero.
 *
 * From f32_to_f16 ()
 */
uint16_t
cogl_float_to_half_rtz_slow (float val)
{
  const fi_type fi = {val};
  const uint32_t flt_m = fi.u & 0x7fffff;
  const uint32_t flt_e = (fi.u >> 23) & 0xff;
  const uint32_t flt_s = (fi.u >> 31) & 0x1;
  int16_t s, e, m = 0;

  s = flt_s;

  if (flt_e == 0xff)
    {
      if (flt_m != 0)
        {
          /* 'val' is a NaN, return NaN */
          e = 0x1f;
          /* Retain the top bits of a NaN to make sure that the quiet/signaling
           * status stays the same.
           */
          m = flt_m >> 13;
          if (!m)
            m = 1;
          return (s << 15) + (e << 10) + m;
        }

      /* 'val' is Inf, return Inf */
      e = 0x1f;
      return (s << 15) + (e << 10) + m;
    }

  if (!(flt_e | flt_m))
    {
      /* 'val' is zero, return zero */
      e = 0;
      return (s << 15) + (e << 10) + m;
    }

  m = flt_m >> 9 | ((flt_m & 0x1ff) != 0);
  if (!(flt_e | m))
    {
      /* 'val' is denorm, return zero */
      e = 0;
      return (s << 15) + (e << 10) + m;
    }

  return cogl_roundtozero_f16 (s, flt_e - 0x71, m | 0x4000);
}
