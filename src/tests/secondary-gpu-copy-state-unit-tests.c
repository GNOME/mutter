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

static MtkRegion *
create_damage (int x,
               int y)
{
  return mtk_region_create_rectangle (&MTK_RECTANGLE_INIT (x, y, 2, 2));
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
    copy_state, damage_1);
  assert_full_damage (repair);
  g_clear_pointer (&repair, mtk_region_unref);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_1);

  g_assert_cmpuint (
    meta_secondary_gpu_copy_state_get_next_buffer_index (copy_state),
    ==,
    1);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_2);
  assert_full_damage (repair);
  g_clear_pointer (&repair, mtk_region_unref);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_2);

  g_assert_cmpuint (
    meta_secondary_gpu_copy_state_get_next_buffer_index (copy_state),
    ==,
    2);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_3);
  assert_full_damage (repair);
  g_clear_pointer (&repair, mtk_region_unref);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage_3);

  g_assert_cmpuint (
    meta_secondary_gpu_copy_state_get_next_buffer_index (copy_state),
    ==,
    0);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage_4);

  g_assert_false (mtk_region_contains_point (repair, 10, 10));
  g_assert_true (mtk_region_contains_point (repair, 20, 10));
  g_assert_true (mtk_region_contains_point (repair, 30, 10));
  g_assert_true (mtk_region_contains_point (repair, 40, 10));
}

static void
test_empty_damage_needs_no_copy (void)
{
  g_autoptr (MetaSecondaryGpuCopyState) copy_state = NULL;
  g_autoptr (MtkRegion) empty_damage = mtk_region_create ();
  g_autoptr (MtkRegion) repair = NULL;

  copy_state = meta_secondary_gpu_copy_state_new (1,
                                                  TEST_WIDTH,
                                                  TEST_HEIGHT);
  meta_secondary_gpu_copy_state_finish_frame (copy_state,
                                              empty_damage);
  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, empty_damage);

  g_assert_true (mtk_region_is_empty (repair));
}

static void
test_full_damage_is_retained (void)
{
  g_autoptr (MetaSecondaryGpuCopyState) copy_state = NULL;
  g_autoptr (MtkRegion) damage = create_damage (10, 10);
  g_autoptr (MtkRegion) full_damage = mtk_region_create_rectangle (
    &MTK_RECTANGLE_INIT (0, 0, TEST_WIDTH, TEST_HEIGHT));
  g_autoptr (MtkRegion) repair = NULL;

  copy_state = meta_secondary_gpu_copy_state_new (2,
                                                  TEST_WIDTH,
                                                  TEST_HEIGHT);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, damage);
  meta_secondary_gpu_copy_state_finish_frame (copy_state, full_damage);

  repair = meta_secondary_gpu_copy_state_get_damage (
    copy_state, damage);
  assert_full_damage (repair);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/backends/native/secondary-gpu-copy/rotating-buffers",
                   test_accumulates_damage_for_rotating_buffers);
  g_test_add_func ("/backends/native/secondary-gpu-copy/empty-damage",
                   test_empty_damage_needs_no_copy);
  g_test_add_func ("/backends/native/secondary-gpu-copy/full-damage",
                   test_full_damage_is_retained);

  return g_test_run ();
}
