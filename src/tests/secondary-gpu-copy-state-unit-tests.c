/*
 * Copyright (C) 2026 Red Hat
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

#include "backends/native/meta-secondary-gpu-copy-state.h"

#define TEST_WIDTH 100
#define TEST_HEIGHT 80
#define TEST_DAMAGE_HISTORY_LENGTH 16
#define TEST_MAX_DAMAGE_RECTANGLES 16

static MtkRegion *
create_damage (int x,
               int y)
{
  return mtk_region_create_rectangle (&MTK_RECTANGLE_INIT (x, y, 2, 2));
}

static MtkRegion *
create_damage_rectangles (int first_rectangle,
                          int n_rectangles)
{
  g_autoptr (MtkRegion) damage = mtk_region_create ();

  for (int i = 0; i < n_rectangles; i++)
    {
      int rectangle = first_rectangle + i;

      mtk_region_union_rectangle (
        damage,
        &MTK_RECTANGLE_INIT ((rectangle % 20) * 4,
                             (rectangle / 20) * 4,
                             2,
                             2));
    }

  return g_steal_pointer (&damage);
}

static void
assert_full_damage (const MtkRegion *damage)
{
  g_autoptr (MtkRegion) full_damage = NULL;

  full_damage = mtk_region_create_rectangle (
    &MTK_RECTANGLE_INIT (0, 0, TEST_WIDTH, TEST_HEIGHT));
  g_assert_true (mtk_region_equal (damage, full_damage));
}

static void
test_accumulates_damage_for_rotating_buffers (void)
{
  g_autoptr (MetaSecondaryGpuCopyState) copy_state = NULL;
  g_autoptr (MtkRegion) damage_1 = create_damage (10, 10);
  g_autoptr (MtkRegion) damage_2 = create_damage (20, 10);
  g_autoptr (MtkRegion) damage_3 = create_damage (30, 10);
  g_autoptr (MtkRegion) damage_4 = create_damage (40, 10);
  g_autoptr (MtkRegion) repair = NULL;

  copy_state = meta_secondary_gpu_copy_state_new (3,
                                                  TEST_WIDTH,
                                                  TEST_HEIGHT);

  g_assert_cmpuint (
    meta_secondary_gpu_copy_state_get_next_buffer_index (copy_state),
    ==,
    0);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_1, TEST_MAX_DAMAGE_RECTANGLES);
  assert_full_damage (repair);
  g_clear_pointer (&repair, mtk_region_unref);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_1, TRUE);

  g_assert_cmpuint (
    meta_secondary_gpu_copy_state_get_next_buffer_index (copy_state),
    ==,
    1);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_2, TEST_MAX_DAMAGE_RECTANGLES);
  assert_full_damage (repair);
  g_clear_pointer (&repair, mtk_region_unref);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_2, TRUE);

  g_assert_cmpuint (
    meta_secondary_gpu_copy_state_get_next_buffer_index (copy_state),
    ==,
    2);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_3, TEST_MAX_DAMAGE_RECTANGLES);
  assert_full_damage (repair);
  g_clear_pointer (&repair, mtk_region_unref);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_3, TRUE);

  g_assert_cmpuint (
    meta_secondary_gpu_copy_state_get_next_buffer_index (copy_state),
    ==,
    0);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_4, TEST_MAX_DAMAGE_RECTANGLES);

  g_assert_false (mtk_region_contains_point (repair, 10, 10));
  g_assert_true (mtk_region_contains_point (repair, 20, 10));
  g_assert_true (mtk_region_contains_point (repair, 30, 10));
  g_assert_true (mtk_region_contains_point (repair, 40, 10));
}

static void
test_retains_damage_from_failed_copy (void)
{
  g_autoptr (MetaSecondaryGpuCopyState) copy_state = NULL;
  g_autoptr (MtkRegion) damage_1 = create_damage (10, 10);
  g_autoptr (MtkRegion) damage_2 = create_damage (20, 10);
  g_autoptr (MtkRegion) damage_3 = create_damage (30, 10);
  g_autoptr (MtkRegion) damage_4 = create_damage (40, 10);
  g_autoptr (MtkRegion) damage_5 = create_damage (50, 10);
  g_autoptr (MtkRegion) repair = NULL;

  copy_state = meta_secondary_gpu_copy_state_new (3,
                                                  TEST_WIDTH,
                                                  TEST_HEIGHT);

  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_1, TRUE);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_2, TRUE);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_3, TRUE);

  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_4, TEST_MAX_DAMAGE_RECTANGLES);
  g_assert_true (mtk_region_contains_point (repair, 20, 10));
  g_clear_pointer (&repair, mtk_region_unref);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_4, FALSE);

  g_assert_cmpuint (
    meta_secondary_gpu_copy_state_get_next_buffer_index (copy_state),
    ==,
    0);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_5, TEST_MAX_DAMAGE_RECTANGLES);

  g_assert_false (mtk_region_contains_point (repair, 10, 10));
  g_assert_true (mtk_region_contains_point (repair, 20, 10));
  g_assert_true (mtk_region_contains_point (repair, 30, 10));
  g_assert_true (mtk_region_contains_point (repair, 40, 10));
  g_assert_true (mtk_region_contains_point (repair, 50, 10));
}

static void
test_reset_invalidates_buffer_contents (void)
{
  g_autoptr (MetaSecondaryGpuCopyState) copy_state = NULL;
  g_autoptr (MtkRegion) damage = create_damage (10, 10);
  g_autoptr (MtkRegion) repair = NULL;

  copy_state = meta_secondary_gpu_copy_state_new (2,
                                                  TEST_WIDTH,
                                                  TEST_HEIGHT);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage, TRUE);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage, TRUE);

  meta_secondary_gpu_copy_state_reset (copy_state);

  g_assert_cmpuint (
    meta_secondary_gpu_copy_state_get_next_buffer_index (copy_state),
    ==,
    0);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage, TEST_MAX_DAMAGE_RECTANGLES);
  assert_full_damage (repair);
}

static void
test_empty_damage_means_full_damage (void)
{
  g_autoptr (MetaSecondaryGpuCopyState) copy_state = NULL;
  g_autoptr (MtkRegion) empty_damage = mtk_region_create ();
  g_autoptr (MtkRegion) repair = NULL;

  copy_state = meta_secondary_gpu_copy_state_new (1,
                                                  TEST_WIDTH,
                                                  TEST_HEIGHT);
  meta_secondary_gpu_copy_state_finish_frame (copy_state,
                                              empty_damage,
                                              TRUE);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, empty_damage, TEST_MAX_DAMAGE_RECTANGLES);

  assert_full_damage (repair);
}

static void
test_too_many_damage_rectangles_means_full_damage (void)
{
  g_autoptr (MetaSecondaryGpuCopyState) copy_state = NULL;
  g_autoptr (MtkRegion) damage_1 = create_damage (90, 70);
  g_autoptr (MtkRegion) damage_2 = create_damage_rectangles (0, 6);
  g_autoptr (MtkRegion) damage_3 = create_damage_rectangles (6, 6);
  g_autoptr (MtkRegion) damage_4 = create_damage_rectangles (12, 5);
  g_autoptr (MtkRegion) repair = NULL;

  copy_state = meta_secondary_gpu_copy_state_new (3,
                                                  TEST_WIDTH,
                                                  TEST_HEIGHT);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_1, TRUE);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_2, TRUE);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_3, TRUE);

  repair = meta_secondary_gpu_copy_state_get_damage (copy_state,
                                                     damage_4,
                                                     G_MAXUINT);
  g_assert_cmpint (mtk_region_num_rectangles (repair), ==, 17);
  g_clear_pointer (&repair, mtk_region_unref);

  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_4, TEST_MAX_DAMAGE_RECTANGLES);
  assert_full_damage (repair);
}

static void
test_old_damage_history_means_full_damage (void)
{
  g_autoptr (MetaSecondaryGpuCopyState) copy_state = NULL;
  g_autoptr (MtkRegion) damage = create_damage (10, 10);
  g_autoptr (MtkRegion) repair = NULL;

  copy_state = meta_secondary_gpu_copy_state_new (1,
                                                  TEST_WIDTH,
                                                  TEST_HEIGHT);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage, TRUE);

  for (int i = 1; i < TEST_DAMAGE_HISTORY_LENGTH; i++)
    meta_secondary_gpu_copy_state_finish_frame (copy_state, damage, FALSE);

  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage, TEST_MAX_DAMAGE_RECTANGLES);
  g_assert_true (mtk_region_equal (repair, damage));
  g_clear_pointer (&repair, mtk_region_unref);

  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage, FALSE);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage, TEST_MAX_DAMAGE_RECTANGLES);
  assert_full_damage (repair);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/backends/native/secondary-gpu-copy/rotating-buffers",
                   test_accumulates_damage_for_rotating_buffers);
  g_test_add_func ("/backends/native/secondary-gpu-copy/failed-copy",
                   test_retains_damage_from_failed_copy);
  g_test_add_func ("/backends/native/secondary-gpu-copy/reset",
                   test_reset_invalidates_buffer_contents);
  g_test_add_func ("/backends/native/secondary-gpu-copy/full-damage",
                   test_empty_damage_means_full_damage);
  g_test_add_func ("/backends/native/secondary-gpu-copy/too-many-rectangles",
                   test_too_many_damage_rectangles_means_full_damage);
  g_test_add_func ("/backends/native/secondary-gpu-copy/history-too-old",
                   test_old_damage_history_means_full_damage);

  return g_test_run ();
}
