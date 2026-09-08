/*
 * Copyright (C) 2026 Red Hat Inc.
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

#include "clutter/clutter.h"
#include "clutter/clutter-mutter.h"

#define TEST_DAMAGE_HISTORY_LENGTH 16

static void
test_invalid_buffer_ages (void)
{
  ClutterDamageHistory *history = clutter_damage_history_new ();
  g_autoptr (MtkRegion) damage = mtk_region_create ();

  for (int i = 0; i < TEST_DAMAGE_HISTORY_LENGTH; i++)
    {
      clutter_damage_history_record (history, damage);
      clutter_damage_history_step (history);
    }

  g_assert_false (clutter_damage_history_is_age_valid (history, 0));
  g_assert_false (clutter_damage_history_is_age_valid (history, -1));
  g_assert_false (clutter_damage_history_is_age_valid (history, G_MININT));
  g_assert_false (clutter_damage_history_is_age_valid (history, G_MAXINT));

  clutter_damage_history_free (history);
}

static void
test_age_one_needs_no_history (void)
{
  ClutterDamageHistory *history = clutter_damage_history_new ();

  g_assert_true (clutter_damage_history_is_age_valid (history, 1));
  g_assert_false (clutter_damage_history_is_age_valid (history, 2));

  clutter_damage_history_free (history);
}

static void
test_only_completed_frames_count (void)
{
  ClutterDamageHistory *history = clutter_damage_history_new ();
  g_autoptr (MtkRegion) first_damage = NULL;
  g_autoptr (MtkRegion) second_damage = NULL;

  first_damage =
    mtk_region_create_rectangle (&MTK_RECTANGLE_INIT (10, 10, 2, 2));
  second_damage =
    mtk_region_create_rectangle (&MTK_RECTANGLE_INIT (20, 10, 2, 2));

  clutter_damage_history_record (history, first_damage);
  g_assert_false (clutter_damage_history_is_age_valid (history, 2));
  clutter_damage_history_step (history);

  /* A buffer of age 2 needs the previous frame and the current damage. */
  g_assert_true (clutter_damage_history_is_age_valid (history, 2));
  g_assert_false (clutter_damage_history_is_age_valid (history, 3));

  clutter_damage_history_record (history, second_damage);
  g_assert_false (clutter_damage_history_is_age_valid (history, 3));
  g_assert_true (mtk_region_equal (clutter_damage_history_lookup (history, 1),
                                  first_damage));

  clutter_damage_history_step (history);
  g_assert_true (clutter_damage_history_is_age_valid (history, 3));
  g_assert_true (mtk_region_equal (clutter_damage_history_lookup (history, 1),
                                  second_damage));
  g_assert_true (mtk_region_equal (clutter_damage_history_lookup (history, 2),
                                  first_damage));

  clutter_damage_history_free (history);
}

static void
test_oldest_repairable_buffer (void)
{
  ClutterDamageHistory *history = clutter_damage_history_new ();
  g_autoptr (MtkRegion) damage = mtk_region_create ();

  for (int i = 0; i < TEST_DAMAGE_HISTORY_LENGTH - 1; i++)
    {
      clutter_damage_history_record (history, damage);
      clutter_damage_history_step (history);
    }

  /* The 15 completed frames are enough to repair a buffer of age 16. */
  g_assert_true (clutter_damage_history_is_age_valid (history, 16));
  g_assert_false (clutter_damage_history_is_age_valid (history, 17));

  /* Recording the current frame and wrapping preserve the same limit. */
  for (int i = 0; i < TEST_DAMAGE_HISTORY_LENGTH; i++)
    {
      clutter_damage_history_record (history, damage);
      g_assert_true (clutter_damage_history_is_age_valid (history, 16));
      g_assert_false (clutter_damage_history_is_age_valid (history, 17));
      clutter_damage_history_step (history);
    }

  clutter_damage_history_free (history);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/clutter/damage-history/invalid-buffer-ages",
                   test_invalid_buffer_ages);
  g_test_add_func ("/clutter/damage-history/age-one",
                   test_age_one_needs_no_history);
  g_test_add_func ("/clutter/damage-history/completed-frames",
                   test_only_completed_frames_count);
  g_test_add_func ("/clutter/damage-history/oldest-repairable-buffer",
                   test_oldest_repairable_buffer);

  return g_test_run ();
}
