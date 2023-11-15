#include <stdlib.h>
#include <glib.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

#define TEST_TIMELINE_DURATION 3000

/*
 * Make the test tolarate being half a second off track in each direction,
 * the thing we're testing for will still be tested for.
 */
#define TEST_ERROR_TOLERANCE 500

typedef struct _TestState
{
  ClutterTimeline *timeline;
  int64_t start_time_us;
  int new_frame_counter;
  int expected_frame;
  int completion_count;
  int cycle_frame_counter;
} TestState;


static void
new_frame_cb (ClutterTimeline *timeline,
              int              frame_num,
              TestState       *state)
{
  int64_t current_time_us;
  int current_frame_ms;
  long msec_diff;
  int loop_overflow = 0;

  current_time_us = g_get_monotonic_time ();
  current_frame_ms = clutter_timeline_get_elapsed_time (state->timeline);
  msec_diff = us2ms (current_time_us - state->start_time_us);

  /* If we expect to have interpolated past the end of the timeline
   * we keep track of the overflow so we can determine when
   * the next timeout will happen. We then clip expected_frames
   * to TEST_TIMELINE_DURATION since clutter-timeline
   * semantics guaranty this frame is always signaled before
   * looping */
  if (state->expected_frame > TEST_TIMELINE_DURATION)
    {
      loop_overflow = state->expected_frame - TEST_TIMELINE_DURATION;
      state->expected_frame = TEST_TIMELINE_DURATION;
    }

  switch (state->cycle_frame_counter)
    {
    case 0:
    case 1:
      if (current_frame_ms >= (state->expected_frame - TEST_ERROR_TOLERANCE) &&
          current_frame_ms <= (state->expected_frame + TEST_ERROR_TOLERANCE))
        {
          g_test_message ("elapsed milliseconds=%-5li "
                          "expected frame=%-4i actual frame=%-4i (OK)",
                          msec_diff,
                          state->expected_frame,
                          current_frame_ms);
        }
      else
        {
          g_test_message ("elapsed milliseconds=%-5li "
                          "expected frame=%-4i actual frame=%-4i (FAILED)",
                          msec_diff,
                          state->expected_frame,
                          current_frame_ms);
          g_test_fail ();
        }
      break;
    case 2:
      g_assert_cmpint (current_frame_ms, ==, TEST_TIMELINE_DURATION);
      break;
    default:
      g_assert_not_reached ();
    }

  /* We already tested that we interpolated when looping, lets stop now. */
  if (state->completion_count == 1 &&
      state->cycle_frame_counter == 0)
    {
      clutter_timeline_stop (timeline);
      return;
    }

  switch (state->cycle_frame_counter)
    {
    case 0:
      {
        /*
         * First frame, sleep so we're about in the middle of the cycle,
         * before the end of the timeline cycle.
         */
        int delay_ms = ms (1500);

        state->expected_frame = current_frame_ms + delay_ms;
        g_test_message ("Sleeping for 1.5 seconds "
                        "so next frame should be (%d + %d) = %d",
                        current_frame_ms,
                        delay_ms,
                        state->expected_frame);
        g_usleep (ms2us (delay_ms));
        break;
      }
    case 1:
      {
        /*
         * Second frame, we're about in the middle of the cycle; sleep one cycle,
         * and check that we end up in the middle again.
         */
        int delay_ms = TEST_TIMELINE_DURATION;

        state->expected_frame = current_frame_ms + delay_ms;
        g_test_message ("Sleeping for %d seconds "
                        "so next frame should be (%d + %d) = %d, "
                        "which is %d into the next cycle",
                        TEST_TIMELINE_DURATION / 1000,
                        current_frame_ms,
                        delay_ms,
                        state->expected_frame,
                        state->expected_frame - TEST_TIMELINE_DURATION);
        g_usleep (ms2us (delay_ms));

        g_assert_cmpint (state->expected_frame, >, TEST_TIMELINE_DURATION);

        state->expected_frame += loop_overflow;
        state->expected_frame -= TEST_TIMELINE_DURATION;
        g_test_message ("End of timeline reached: "
                        "Wrapping expected frame too %d",
                        state->expected_frame);
        break;
      }
    case 2:
    case 3:
      {
        break;
      }
    }

  state->new_frame_counter++;
  state->cycle_frame_counter++;
}

static void
completed_cb (ClutterTimeline *timeline,
              TestState       *state)
{
  state->completion_count++;
  state->cycle_frame_counter = 0;

  if (state->completion_count >= 2)
    g_assert_not_reached ();
}

static void
stopped_cb (ClutterTimeline *timeline,
            gboolean         is_finished,
            TestState       *state)
{
  g_assert_cmpint (state->completion_count, ==, 1);

  clutter_test_quit ();
}

static void
timeline_interpolation (void)
{
  ClutterActor *stage;
  TestState state;

  stage = clutter_test_get_stage ();

  state.timeline = 
    clutter_timeline_new_for_actor (stage, TEST_TIMELINE_DURATION);
  clutter_timeline_set_repeat_count (state.timeline, -1);
  g_signal_connect (state.timeline,
                    "new-frame",
                    G_CALLBACK (new_frame_cb),
                    &state);
  g_signal_connect (state.timeline,
                    "completed",
                    G_CALLBACK (completed_cb),
                    &state);
  g_signal_connect (state.timeline,
                    "stopped",
                    G_CALLBACK (stopped_cb),
                    &state);

  state.completion_count = 0;
  state.new_frame_counter = 0;
  state.cycle_frame_counter = 0;
  state.expected_frame = 0;

  clutter_actor_show (stage);

  state.start_time_us = g_get_monotonic_time ();
  clutter_timeline_start (state.timeline);

  clutter_test_main ();

  g_object_unref (state.timeline);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/timeline/interpolate", timeline_interpolation)
)
