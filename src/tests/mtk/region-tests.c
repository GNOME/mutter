/*
 * Copyright (C) 2023 Bilal Elmoussaoui
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
 */

#include "config.h"
#include "mtk/mtk.h"

#include <glib.h>


static void
test_contains_point (void)
{
  g_autoptr (MtkRegion) r1 = NULL;

  r1 = mtk_region_create_rectangle (&MTK_RECTANGLE_INIT (0, 0, 100, 100));

  g_assert (!mtk_region_contains_point (r1, 200, 200));
  g_assert (mtk_region_contains_point (r1, 50, 50));
}

/* A re-implementation of a pixman translation test */
#define LARGE 32000
static void
test_translate (void)
{
  MtkRectangle rect = MTK_RECTANGLE_INIT (-LARGE, -LARGE, LARGE, LARGE);
  g_autoptr (MtkRegion) r1, r2 = NULL;

  r1 = mtk_region_create_rectangles (&rect, 1);
  g_assert_cmpint (mtk_region_num_rectangles (r1), ==, 1);
  r2 = mtk_region_create_rectangle (&rect);
  g_assert_cmpint (mtk_region_num_rectangles (r2), ==, 1);

  g_assert (mtk_region_equal (r1, r2));

  mtk_region_translate (r1, -LARGE, LARGE);
  mtk_region_translate (r1, LARGE, -LARGE);

  g_assert (mtk_region_equal (r1, r2));
}

static void
test_region (void)
{
  g_autoptr (MtkRegion) r1 = NULL;

  r1 = mtk_region_create ();
  g_assert (mtk_region_is_empty (r1));

  MtkRectangle rect = MTK_RECTANGLE_INIT (5, 5, 20, 20);
  mtk_region_union_rectangle (r1, &rect);

  g_assert (!mtk_region_is_empty (r1));
  MtkRectangle extents = mtk_region_get_extents (r1);
  g_assert (mtk_rectangle_equal (&extents, &rect));

  mtk_region_translate (r1, 15, 20);
  extents = mtk_region_get_extents (r1);
  g_assert_cmpint (extents.x, ==,  rect.x + 15);
  g_assert_cmpint (extents.y, ==,  rect.y + 20);
  g_assert_cmpint (extents.width, ==, rect.width);
  g_assert_cmpint (extents.height, ==, rect.height);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/mtk/region/region", test_region);
  g_test_add_func ("/mtk/region/contains-point", test_contains_point);
  g_test_add_func ("/mtk/region/translate", test_translate);

  return g_test_run ();
}
