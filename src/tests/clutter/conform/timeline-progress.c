#include <glib.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static void
timeline_progress_step (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterTimeline *timeline;

  timeline = clutter_timeline_new_for_actor (stage, 1000);

  if (!g_test_quiet ())
    g_print ("mode: step(3, end)\n");

  clutter_timeline_rewind (timeline);
  clutter_timeline_set_step_progress (timeline, 3, CLUTTER_STEP_MODE_END);
  g_assert_cmpint (clutter_timeline_get_progress (timeline), ==, 0);

  clutter_timeline_advance (timeline, 1000 / 3 - 1);
  g_assert_cmpint (clutter_timeline_get_progress (timeline) * 1000, ==, 0);

  clutter_timeline_advance (timeline, 1000 / 3 + 1);
  g_assert_cmpint (clutter_timeline_get_progress (timeline) * 1000, ==, 333);

  clutter_timeline_advance (timeline, 1000 / 3 * 2 - 1);
  g_assert_cmpint (clutter_timeline_get_progress (timeline) * 1000, ==, 333);

  clutter_timeline_advance (timeline, 1000 / 3 * 2 + 1);
  g_assert_cmpint (clutter_timeline_get_progress (timeline) * 1000, ==, 666);

  clutter_timeline_rewind (timeline);
  clutter_timeline_set_progress_mode (timeline, CLUTTER_STEP_START);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 0.0);

  clutter_timeline_advance (timeline, 1);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  clutter_timeline_advance (timeline, 500);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  clutter_timeline_advance (timeline, 999);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  clutter_timeline_advance (timeline, 1000);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  if (!g_test_quiet ())
    g_print ("mode: step-start\n");

  clutter_timeline_rewind (timeline);
  clutter_timeline_set_progress_mode (timeline, CLUTTER_STEP_START);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 0.0);

  clutter_timeline_advance (timeline, 1);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  clutter_timeline_advance (timeline, 500);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  clutter_timeline_advance (timeline, 999);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  clutter_timeline_advance (timeline, 1000);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  if (!g_test_quiet ())
    g_print ("mode: step-end\n");

  clutter_timeline_rewind (timeline);
  clutter_timeline_set_progress_mode (timeline, CLUTTER_STEP_END);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 0.0);

  clutter_timeline_advance (timeline, 1);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 0.0);

  clutter_timeline_advance (timeline, 500);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 0.0);

  clutter_timeline_advance (timeline, 999);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 0.0);

  clutter_timeline_advance (timeline, 1000);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  g_object_unref (timeline);
}

static void
timeline_progress_mode (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterTimeline *timeline;

  timeline = clutter_timeline_new_for_actor (stage, 1000);

  g_assert (clutter_timeline_get_progress_mode (timeline) == CLUTTER_LINEAR);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 0.0);

  clutter_timeline_advance (timeline, 500);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 0.5);

  clutter_timeline_advance (timeline, 1000);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 1.0);

  clutter_timeline_rewind (timeline);
  g_assert_cmpfloat (clutter_timeline_get_progress (timeline), ==, 0.0);

  g_object_unref (timeline);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/timeline/progress/step", timeline_progress_step);
  CLUTTER_TEST_UNIT ("/timeline/progress/mode", timeline_progress_mode)
)
