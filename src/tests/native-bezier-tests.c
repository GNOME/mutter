/*
 * Copyright (C) 2023 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "native-bezier-tests.h"

#include "backends/native/meta-bezier.h"

static void
meta_test_bezier_linear (void)
{
  const int precision = 256;
  g_autoptr (MetaBezier) b = meta_bezier_new (precision);
  double i;
  double point;

  meta_bezier_init (b, 0.0, 0.0, 1.0, 1.0);

  /* The bezier curve code has a slight bug: the last point is always enforced
   * to be 1.0. For low scales this can lead to a jump from N-2 to N-1*/
  for (i = 0.0; i < 1.0; i += 1.0 / precision)
    {
      point = meta_bezier_lookup (b, i);
      g_assert_cmpfloat_with_epsilon (i, point, 0.01);
    }

  point = meta_bezier_lookup (b, 1.0);
  g_assert_cmpfloat (point, ==, 1.0);
}

static void
meta_test_bezier_steep (void)
{
  const int precision = 1000;
  g_autoptr (MetaBezier) b = meta_bezier_new (precision);
  double i;

  meta_bezier_init (b, 0.0, 1.0, 0.0, 1.0);

  /*  ^  _____________
   *  | /
   *  || steep
   *  ||
   *  ||
   *  +---------------t>
   */
  for (i = 0.2; i < 1.0; i += 1.0 / precision)
    {
      double point = meta_bezier_lookup (b, i);
      g_assert_cmpfloat (point, >, 0.90);
    }
}

static void
meta_test_bezier_flat (void)
{
  const int precision = 1000;
  g_autoptr (MetaBezier) b = meta_bezier_new (precision);
  double i;

  /*  ^              |
   *  |              |
   *  |        flat  |
   *  |             /
   *  |____________/
   *  +-------------->
   */
  meta_bezier_init (b, 1.0, 0.0, 1.0, 0.0);

  for (i = 0.0; i < 0.8; i++)
    {
      double point = meta_bezier_lookup (b, i);
      g_assert_cmpfloat (point, <, 0.20);
    }
}

static void
meta_test_bezier_snake (void)
{
  const int precision = 1000;
  g_autoptr (MetaBezier) b = meta_bezier_new (precision);
  double i;

  /*  ^         _______
   *  |        /
   *  |        | snake
   *  |        |
   *  |________/
   *  +--------------->
   */
  meta_bezier_init (b, 1.0, 0.0, 0.0, 1.0);

  for (i = 0.0; i < 1.0; i += 1.0 / precision)
    {
      double point = meta_bezier_lookup (b, i);
      if (i < 0.33)
        g_assert_cmpfloat (point, <=, 0.1);
      else if (i > 0.66)
        g_assert_cmpfloat (point, >=, 0.9);
    }
}

void
init_bezier_tests (void)
{
  g_test_add_func ("/backends/bezier/linear", meta_test_bezier_linear);
  g_test_add_func ("/backends/bezier/steep", meta_test_bezier_steep);
  g_test_add_func ("/backends/bezier/flat", meta_test_bezier_flat);
  g_test_add_func ("/backends/bezier/snake", meta_test_bezier_snake);
}

