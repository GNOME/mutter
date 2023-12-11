/*
 * Authored By Tomas Frydrych  <tf@openedhand.com>
 *
 * Copyright (C) 2007 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib.h>
#include <string.h>

#include "meta-bezier.h"

/****************************************************************************
 * MetaBezier -- representation of a cubic bezier curve                   *
 * (private; a building block for the public bspline object)                *
 ****************************************************************************/

/* This is used in meta_bezier_advance to represent the full
   length of the bezier curve. Anything less than that represents a
   fraction of the length */
#define META_BEZIER_MAX_LENGTH (1 << 18)

/*
 * The t parameter of the bezier is from interval <0,1>, so we can use
 * 14.18 format and special multiplication functions that preserve
 * more of the least significant bits but would overflow if the value
 * is > 1
 */
#define CBZ_T_Q 18
#define CBZ_T_ONE (1 << CBZ_T_Q)
#define CBZ_T_MUL(x,y) ((((x) >> 3) * ((y) >> 3)) >> 12)
#define CBZ_T_POW2(x) CBZ_T_MUL (x, x)
#define CBZ_T_POW3(x) CBZ_T_MUL (CBZ_T_POW2 (x), x)
#define CBZ_T_DIV(x,y) ((((x) << 9) / (y)) << 9)

/*
 * Constants for sampling of the bezier
 */
#define CBZ_T_SAMPLES 128
#define CBZ_T_STEP (CBZ_T_ONE / CBZ_T_SAMPLES)
#define CBZ_L_STEP (CBZ_T_ONE / CBZ_T_SAMPLES)

#define FIXED_BITS (32)
#define FIXED_Q (FIXED_BITS - 16)
#define FIXED_FROM_INT(x) ((x) << FIXED_Q)

typedef gint32 _FixedT;

typedef struct _MetaBezierKnot MetaBezierKnot;

struct _MetaBezierKnot
{
  int x;
  int y;
};

/*
 * This is a private type representing a single cubic bezier
 */
struct _MetaBezier
{
  /* The precision, i.e. the number of possible points between 0.0 and 1.0.
   * We require a normalized bezier curve but then later sample to a given
   * precision. The bezier coefficients are scaled up by this factor and later
   * scaled down again during sampling.
   */
  unsigned int precision;

  /*
   * bezier coefficients -- these are calculated using multiplication and
   * addition from integer input, so these are also integers
   */
  int ax;
  int bx;
  int cx;
  int dx;

  int ay;
  int by;
  int cy;
  int dy;

  /* length of the bezier */
  unsigned int length;

  /* the sampled points on the curve */
  double *points;
};

/**
 * meta_bezier_new:
 * @precision: the number of points we can sample between 0.0 and 1.0
 *
 * Create a new bezier curve with the given precision. This precision defines
 * the maximum number of points we can sample and thus thus how much linear
 * interpolation needs to be done when looking up a point on the curve.
 */
MetaBezier *
meta_bezier_new (unsigned int precision)
{
  MetaBezier *b;

  g_return_val_if_fail (precision > 0, NULL);

  b = g_new0 (MetaBezier, 1);
  b->precision = precision;
  b->points = g_new0 (double, precision);
  return b;
}

void
meta_bezier_free (MetaBezier *b)
{
  if (G_LIKELY (b))
    {
      g_free (b->points);
      g_free (b);
    }
}

static int
meta_bezier_t2x (const MetaBezier *b,
                 _FixedT           t)
{
  /*
   * NB -- the int coefficients can be at most 8192 for the multiplication
   * to work in this fashion due to the limits of the 14.18 fixed.
   */
  return ((b->ax * CBZ_T_POW3 (t) + b->bx * CBZ_T_POW2 (t) + b->cx * t) >> CBZ_T_Q)
         + b->dx;
}

static int
meta_bezier_t2y (const MetaBezier *b,
                 _FixedT           t)
{
  /*
   * NB -- the int coefficients can be at most 8192 for the multiplication
   * to work in this fashion due to the limits of the 14.18 fixed.
   */
  return ((b->ay * CBZ_T_POW3 (t) + b->by * CBZ_T_POW2 (t) + b->cy * t) >> CBZ_T_Q)
         + b->dy;
}

/*
 * Advances along the bezier to relative length L and returns the coordinances
 * in knot
 */
static void
meta_bezier_advance (const MetaBezier *b,
                     int               L,
                     MetaBezierKnot   *knot)
{
  _FixedT t = L;

  knot->x = meta_bezier_t2x (b, t);
  knot->y = meta_bezier_t2y (b, t);

#if 0
  g_debug ("advancing to relative pt %f: t %f, {%d,%d}",
           (double) L / (double) CBZ_T_ONE,
           (double) t / (double) CBZ_T_ONE,
           knot->x, knot->y);
#endif
}

static void
meta_bezier_sample (MetaBezier *b)
{
  const size_t N = b->precision;
  const double MIN_EPSILON = 0.00001;
  MetaBezierKnot pos;
  int i;
  double epsilon;
  double t;
  double ts[N]; /* maps pos.x -> t */
  double *points = b->points;

  for (i = 0; i < N; i++)
    {
      points[i] = -1.0;
      ts[i] = -1.0;
    }

  ts[0] = 0;
  ts[N - 1] = 1;
  points[0] = 0;
  /* Fill in the last point so linear interpolation (see below) is
   * guaranteed. This should always yield 1.0 anyway. */
  meta_bezier_advance (b, META_BEZIER_MAX_LENGTH, &pos);
  points[N - 1] = pos.y / N;

  epsilon = 1.0 / N;
  t = epsilon;

  /* We walk forward from t=0 to t=1.0, calculating every bezier
   * point on the curve and for all x values we remember our matching t.
   * If any x coordinate is missing, we reduce the epsilon and restart
   * with this higher granularity from the last t that gave us a value
   * below the missing x.
   */
  do
    {
      for (i = 1; i < N; i++)
        {
          if (ts[i] == -1.0)
            {
              t = ts[i - 1];
              epsilon /= 2.0;
              break;
            }
        }

      while (t < 1.0)
        {
          meta_bezier_advance (b, t * META_BEZIER_MAX_LENGTH, &pos);
          if (pos.x < N)
            {
              if (points[MAX (pos.x - 1, 0)] == -1)
                {
                  /* Skipped over at least one x coordinate, let's restart
                   * as long as we have have a sensible epsilon. Some curves
                   * may never find all points. */
                  if (epsilon > MIN_EPSILON)
                    break;
                }

              if (points[pos.x] == -1)
                {
                  points[pos.x] = (double)pos.y / N;
                  ts[pos.x] = t;
                }
            }

          t += epsilon;
        }
    }
  while (t < 1.0 && epsilon > MIN_EPSILON);

  /* Do linear interpolation of the remaining points, if any */
  i = 0;
  while (i < N - 1)
    {
      double *current = &points[++i];
      int prev = points[i - 1];

      if (*current == -1.0)
        {
          int next_idx = i;
          int next;
          double delta;

          do
            {
              next = points[++next_idx];
            } while (next == -1.0 && next_idx < N); /* N-1 is guaranteed valid */

          delta = (next - prev) / (next_idx - i + 1);

          while (i < next_idx)
            {
              *current = prev + delta;
              prev = *current;
              current++;
              i++;
            }
        }
    }

  for (i = 0; i < N; i++)
    g_warn_if_fail (points[i] != -1.0);
}

static int
sqrti (int number)
{
#if defined __SSE2__
  /* The GCC built-in with SSE2 (sqrtsd) is up to twice as fast as
   * the pure integer code below. It is also more accurate.
   */
  return __builtin_sqrt (number);
#else
  /* This is a fixed point implementation of the Quake III sqrt algorithm,
   * described, for example, at
   *   http://www.codemaestro.com/reviews/review00000105.html
   *
   * While the original QIII is extremely fast, the use of floating division
   * and multiplication makes it perform very on arm processors without FPU.
   *
   * The key to successfully replacing the floating point operations with
   * fixed point is in the choice of the fixed point format. The QIII
   * algorithm does not calculate the square root, but its reciprocal ('y'
   * below), which is only at the end turned to the inverse value. In order
   * for the algorithm to produce satisfactory results, the reciprocal value
   * must be represented with sufficient precision; the 16.16 we use
   * elsewhere in clutter is not good enough, and 10.22 is used instead.
   */
  _FixedT x;
  uint32_t y_1;        /* 10.22 fixed point */
  uint32_t f = 0x600000; /* '1.5' as 10.22 fixed */

  union
  {
    float f;
    uint32_t i;
  } flt, flt2;

  flt.f = number;

  x = FIXED_FROM_INT (number) / 2;

  /* The QIII initial estimate */
  flt.i = 0x5f3759df - ( flt.i >> 1 );

  /* Now, we convert the float to 10.22 fixed. We exploit the mechanism
   * described at http://www.d6.com/users/checker/pdfs/gdmfp.pdf.
   *
   * We want 22 bit fraction; a single precision float uses 23 bit
   * mantisa, so we only need to add 2^(23-22) (no need for the 1.5
   * multiplier as we are only dealing with positive numbers).
   *
   * Note: we have to use two separate variables here -- for some reason,
   * if we try to use just the flt variable, gcc on ARM optimises the whole
   * addition out, and it all goes pear shape, since without it, the bits
   * in the float will not be correctly aligned.
   */
  flt2.f = flt.f + 2.0;
  flt2.i &= 0x7FFFFF;

  /* Now we correct the estimate */
  y_1 = (flt2.i >> 11) * (flt2.i >> 11);
  y_1 = (y_1 >> 8) * (x >> 8);

  y_1 = f - y_1;
  flt2.i = (flt2.i >> 11) * (y_1 >> 11);

  /* If the original argument is less than 342, we do another
   * iteration to improve precision (for arguments >= 342, the single
   * iteration produces generally better results).
   */
  if (x < 171)
    {
      y_1 = (flt2.i >> 11) * (flt2.i >> 11);
      y_1 = (y_1 >> 8) * (x >> 8);

      y_1 = f - y_1;
      flt2.i = (flt2.i >> 11) * (y_1 >> 11);
    }

  /* Invert, round and convert from 10.22 to an integer
   * 0x1e3c68 is a magical rounding constant that produces slightly
   * better results than 0x200000.
   */
  return (number * flt2.i + 0x1e3c68) >> 22;
#endif
}

void
meta_bezier_init (MetaBezier *b,
                  double      x_1,
                  double      y_1,
                  double      x_2,
                  double      y_2)
{
  double x_0 = 0.0,
         y_0 = 0.0,
         x_3 = 1.0,
         y_3 = 1.0;
  _FixedT t;
  int i;
  int xp = x_0;
  int yp = y_0;
  _FixedT length[CBZ_T_SAMPLES + 1];

  g_warn_if_fail (x_1 >= 0.0 && x_1 <= 1.0);
  g_warn_if_fail (x_2 >= 0.0 && x_2 <= 1.0);
  g_warn_if_fail (y_1 >= 0.0 && y_1 <= 1.0);
  g_warn_if_fail (y_2 >= 0.0 && y_2 <= 1.0);

  x_1 *= b->precision;
  y_1 *= b->precision;
  x_2 *= b->precision;
  y_2 *= b->precision;
  x_3 *= b->precision;
  y_3 *= b->precision;

#if 0
  g_debug ("Initializing bezier at {{%d,%d},{%d,%d},{%d,%d},{%d,%d}}",
           x0, y0, x1, y1, x2, y2, x3, y3);
#endif

  b->dx = x_0;
  b->dy = y_0;

  b->cx = 3 * (x_1 - x_0);
  b->cy = 3 * (y_1 - y_0);

  b->bx = 3 * (x_2 - x_1) - b->cx;
  b->by = 3 * (y_2 - y_1) - b->cy;

  b->ax = x_3 - 3 * x_2 + 3 * x_1 - x_0;
  b->ay = y_3 - 3 * y_2 + 3 * y_1 - y_0;

#if 0
  g_debug ("Cooeficients {{%d,%d},{%d,%d},{%d,%d},{%d,%d}}",
           b->ax, b->ay, b->bx, b->by, b->cx, b->cy, b->dx, b->dy);
#endif

  /*
   * Because of the way we do the multiplication in bezeir_t2x,y
   * these coefficients need to be at most 0x1fff; this should be the case,
   * I think, but have added this warning to catch any problems -- if it
   * triggers, we need to change those two functions a bit.
   */
  if (b->ax > 0x1fff || b->bx > 0x1fff || b->cx > 0x1fff)
    g_warning ("Calculated coefficients will result in multiplication "
               "overflow in meta_bezier_t2x and meta_bezier_t2y.");

  /*
   * Sample the bezier with CBZ_T_SAMPLES and calculate length at
   * each point.
   *
   * We are working with integers here, so we use the fast sqrti function.
   */
  length[0] = 0;

  for (t = CBZ_T_STEP, i = 1; i <= CBZ_T_SAMPLES; ++i, t += CBZ_T_STEP)
    {
      int x = meta_bezier_t2x (b, t);
      int y = meta_bezier_t2y (b, t);

      unsigned int l = sqrti ((y - yp) * (y - yp) + (x - xp) * (x - xp));

      l += length[i - 1];

      length[i] = l;

      xp = x;
      yp = y;
    }

  b->length = length[CBZ_T_SAMPLES];

  meta_bezier_sample (b);

#if 0
  g_debug ("length %d", b->length);
#endif
}

/**
 * meta_bezier_lookup:
 * @b: a #MetaBezier curve
 * @pos: the position on the bezier curve in the [0.0, 1.0] range
 *
 * Returns the value of this normalized point on the curve.
 */
double
meta_bezier_lookup (const MetaBezier *b,
                    double            pos)
{
  /* The position may be fine-grained than our calculated bezier curve,
   * find the two closest points and do a linear interpolation between
   * those two points.
   */
  int low_idx = CLAMP ((int)(pos * b->precision), 0, b->precision - 1);
  int high_idx = CLAMP (low_idx + 1, 0, b->precision - 1);
  double low = b->points[low_idx];
  double high = b->points[high_idx];
  double rval = low + (high - low) * (pos - (int)pos);

  return rval;
}
