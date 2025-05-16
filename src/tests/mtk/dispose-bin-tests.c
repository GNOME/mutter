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

#include <glib.h>

#include "mtk/mtk.h"

static gboolean destroy_called = FALSE;
static gpointer destroyed_data = NULL;

static void
test_destroy_notify (gpointer user_data)
{
  destroy_called = TRUE;
  destroyed_data = user_data;
}

static void
test_add_and_dispose_single (void)
{
  MtkDisposeBin *bin;
  g_autofree char *test_data = NULL;

  bin = mtk_dispose_bin_new();
  g_assert_nonnull(bin);

  test_data = g_strdup ("test data");
  destroy_called = FALSE;
  destroyed_data = NULL;

  mtk_dispose_bin_add (bin, test_data, test_destroy_notify);
  mtk_dispose_bin_dispose (bin);

  g_assert_true (destroy_called);
  g_assert_true (test_data == destroyed_data);
}

static void
test_destroy_notify_multiple (gpointer user_data)
{
  gboolean *called = user_data;

  *called = TRUE;
}

static void
test_add_and_dispose_multiple (void)
{
  MtkDisposeBin *bin;
  gboolean called[3] = {};

  bin = mtk_dispose_bin_new();
  g_assert_nonnull(bin);

  mtk_dispose_bin_add (bin, &called[0], test_destroy_notify_multiple);
  mtk_dispose_bin_add (bin, &called[1], test_destroy_notify_multiple);
  mtk_dispose_bin_add (bin, &called[2], test_destroy_notify_multiple);

  mtk_dispose_bin_dispose(bin);

  g_assert_true (called[0]);
  g_assert_true (called[1]);
  g_assert_true (called[2]);
}

static void
test_dispose_empty_bin (void)
{
  MtkDisposeBin *bin;

  bin = mtk_dispose_bin_new ();
  g_assert_nonnull (bin);
  mtk_dispose_bin_dispose (bin);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/mtk/dispose-bin/single-add-dispose", test_add_and_dispose_single);
  g_test_add_func ("/mtk/dispose-bin/multiple-add-dispose", test_add_and_dispose_multiple);
  g_test_add_func ("/mtk/dispose-bin/dispose-empty", test_dispose_empty_bin);

  return g_test_run();
}
