/*
 * Copyright (C) 2025 Red Hat
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
test_extrapolate_interval_boundary (void)
{
  int64_t interval_us = s2us (10);
  int64_t now_us;
  int64_t next_interval_boundary_us;

  now_us = g_get_monotonic_time ();

  next_interval_boundary_us =
    mtk_extrapolate_next_interval_boundary (now_us - 1, interval_us);
  g_assert_cmpint (next_interval_boundary_us, ==, now_us + interval_us - 1);

  next_interval_boundary_us =
    mtk_extrapolate_next_interval_boundary (now_us - interval_us - 1,
                                            interval_us);
  g_assert_cmpint (next_interval_boundary_us, ==, now_us + interval_us - 1);

  next_interval_boundary_us =
    mtk_extrapolate_next_interval_boundary (now_us, interval_us);
  g_assert_cmpint (next_interval_boundary_us, ==, now_us + interval_us);

  next_interval_boundary_us =
    mtk_extrapolate_next_interval_boundary (now_us + interval_us - 1,
                                            interval_us);
  g_assert_cmpint (next_interval_boundary_us, ==, now_us + interval_us - 1);

  next_interval_boundary_us =
    mtk_extrapolate_next_interval_boundary (0, interval_us);
  g_assert_cmpint (next_interval_boundary_us, >=, now_us);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/mtk/time-utils/extrapolate-interval-boundary",
                   test_extrapolate_interval_boundary);

  return g_test_run ();
}
