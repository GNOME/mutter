/*
 * Copyright (C) 2005 Elijah Newren
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

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <glib.h>

#define NUM_RANDOM_RUNS 10000

static void
init_random_ness (void)
{
  srand (time (NULL));
}

static void
get_random_rect (MtkRectangle *rect)
{
  rect->x = rand () % 1600;
  rect->y = rand () % 1200;
  rect->width = rand () % 1600 + 1;
  rect->height = rand () % 1200 + 1;
}

static void
test_init_rect (void)
{
  MtkRectangle rect;

  rect = MTK_RECTANGLE_INIT (1, 2, 3, 4);
  g_assert_cmpint (rect.x, ==, 1);
  g_assert_cmpint (rect.y, ==, 2);
  g_assert_cmpint (rect.width, ==, 3);
  g_assert_cmpint (rect.height, ==, 4);
}

static void
test_area (void)
{
  MtkRectangle temp;
  int i;
  for (i = 0; i < NUM_RANDOM_RUNS; i++)
    {
      get_random_rect (&temp);
      g_assert (mtk_rectangle_area (&temp) == temp.width * temp.height);
    }

  temp = MTK_RECTANGLE_INIT (0, 0, 5, 7);
  g_assert (mtk_rectangle_area (&temp) == 35);
}

static void
test_intersect (void)
{
  MtkRectangle a = {100, 200,  50,  40};
  MtkRectangle b = {  0,  50, 110, 152};
  MtkRectangle c = {  0,   0,  10,  10};
  MtkRectangle d = {100, 100,  50,  50};
  MtkRectangle b_intersect_d = {100, 100, 10, 50};
  MtkRectangle temp;
  MtkRectangle temp2;

  mtk_rectangle_intersect (&a, &b, &temp);
  temp2 = MTK_RECTANGLE_INIT (100, 200, 10, 2);
  g_assert (mtk_rectangle_equal (&temp, &temp2));
  g_assert (mtk_rectangle_area (&temp) == 20);

  mtk_rectangle_intersect (&a, &c, &temp);
  g_assert (mtk_rectangle_area (&temp) == 0);

  mtk_rectangle_intersect (&a, &d, &temp);
  g_assert (mtk_rectangle_area (&temp) == 0);

  mtk_rectangle_intersect (&b, &d, &b);
  g_assert (mtk_rectangle_equal (&b, &b_intersect_d));
}

static void
test_equal (void)
{
  MtkRectangle a = {10, 12, 4, 18};
  MtkRectangle b = a;
  MtkRectangle c = {10, 12, 4, 19};
  MtkRectangle d = {10, 12, 7, 18};
  MtkRectangle e = {10, 62, 4, 18};
  MtkRectangle f = {27, 12, 4, 18};

  g_assert ( mtk_rectangle_equal (&a, &b));
  g_assert (!mtk_rectangle_equal (&a, &c));
  g_assert (!mtk_rectangle_equal (&a, &d));
  g_assert (!mtk_rectangle_equal (&a, &e));
  g_assert (!mtk_rectangle_equal (&a, &f));
}

static void
test_overlap_funcs (void)
{
  MtkRectangle temp1, temp2;
  int i;
  for (i = 0; i < NUM_RANDOM_RUNS; i++)
    {
      get_random_rect (&temp1);
      get_random_rect (&temp2);
      g_assert (mtk_rectangle_overlap (&temp1, &temp2) ==
                (mtk_rectangle_horiz_overlap (&temp1, &temp2) &&
                 mtk_rectangle_vert_overlap (&temp1, &temp2)));
    }

  temp1 = MTK_RECTANGLE_INIT ( 0, 0, 10, 10);
  temp2 = MTK_RECTANGLE_INIT (20, 0, 10,  5);
  g_assert (!mtk_rectangle_overlap (&temp1, &temp2));
  g_assert (!mtk_rectangle_horiz_overlap (&temp1, &temp2));
  g_assert (mtk_rectangle_vert_overlap (&temp1, &temp2));
}

static void
test_basic_fitting (void)
{
  MtkRectangle temp1, temp2, temp3;
  int i;
  /* Four cases:
   *   case   temp1 fits temp2    temp1 could fit temp2
   *     1           Y                      Y
   *     2           N                      Y
   *     3           Y                      N
   *     4           N                      N
   * Of the four cases, case 3 is impossible.  An alternate way of looking
   * at this table is that either the middle column must be no, or the last
   * column must be yes.  So we test that.  Also, we can repeat the test
   * reversing temp1 and temp2.
   */
  for (i = 0; i < NUM_RANDOM_RUNS; i++)
    {
      get_random_rect (&temp1);
      get_random_rect (&temp2);
      g_assert (mtk_rectangle_contains_rect (&temp1, &temp2) == FALSE ||
                mtk_rectangle_could_fit_rect (&temp1, &temp2) == TRUE);
      g_assert (mtk_rectangle_contains_rect (&temp2, &temp1) == FALSE ||
                mtk_rectangle_could_fit_rect (&temp2, &temp1) == TRUE);
    }

  temp1 = MTK_RECTANGLE_INIT ( 0, 0, 10, 10);
  temp2 = MTK_RECTANGLE_INIT ( 5, 5,  5,  5);
  temp3 = MTK_RECTANGLE_INIT ( 8, 2,  3,  7);
  g_assert ( mtk_rectangle_contains_rect (&temp1, &temp2));
  g_assert (!mtk_rectangle_contains_rect (&temp2, &temp1));
  g_assert (!mtk_rectangle_contains_rect (&temp1, &temp3));
  g_assert ( mtk_rectangle_could_fit_rect (&temp1, &temp3));
  g_assert (!mtk_rectangle_could_fit_rect (&temp3, &temp2));
}

static void
test_adjacent_to (void)
{
  MtkRectangle base = { .x = 10, .y = 10, .width = 10, .height = 10 };
  MtkRectangle adjacent[] = {
    { .x = 20, .y = 10, .width = 10, .height = 10 },
    { .x = 0, .y = 10, .width = 10, .height = 10 },
    { .x = 0, .y = 1, .width = 10, .height = 10 },
    { .x = 20, .y = 19, .width = 10, .height = 10 },
    { .x = 10, .y = 20, .width = 10, .height = 10 },
    { .x = 10, .y = 0, .width = 10, .height = 10 },
  };
  MtkRectangle not_adjacent[] = {
    { .x = 0, .y = 0, .width = 10, .height = 10 },
    { .x = 20, .y = 20, .width = 10, .height = 10 },
    { .x = 21, .y = 10, .width = 10, .height = 10 },
    { .x = 10, .y = 21, .width = 10, .height = 10 },
    { .x = 10, .y = 5, .width = 10, .height = 10 },
    { .x = 11, .y = 10, .width = 10, .height = 10 },
    { .x = 19, .y = 10, .width = 10, .height = 10 },
  };
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (adjacent); i++)
    g_assert (mtk_rectangle_is_adjacent_to (&base, &adjacent[i]));

  for (i = 0; i < G_N_ELEMENTS (not_adjacent); i++)
    g_assert (!mtk_rectangle_is_adjacent_to (&base, &not_adjacent[i]));
}

int
main (int    argc,
      char **argv)
{
  init_random_ness ();

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/mtk/rectangle/init", test_init_rect);
  g_test_add_func ("/mtk/rectangle/area", test_area);
  g_test_add_func ("/mtk/rectangle/intersect", test_intersect);
  g_test_add_func ("/mtk/rectangle/equal", test_equal);
  g_test_add_func ("/mtk/rectangle/overlap", test_overlap_funcs);
  g_test_add_func ("/mtk/rectangle/basic-fitting", test_basic_fitting);
  g_test_add_func ("/mtk/rectangle/adjacent-to", test_adjacent_to);

  return g_test_run ();
}
