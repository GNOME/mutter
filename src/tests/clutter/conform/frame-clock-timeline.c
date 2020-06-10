#include "clutter/clutter.h"
#include "tests/clutter-test-utils.h"

static const float refresh_rate = 60.0;

static ClutterFrameResult
timeline_frame_clock_frame (ClutterFrameClock *frame_clock,
                            int64_t            frame_count,
                            int64_t            time_us,
                            gpointer           user_data)
{
  ClutterFrameInfo frame_info;

  frame_info = (ClutterFrameInfo) {
    .presentation_time = g_get_monotonic_time (),
    .refresh_rate = refresh_rate,
  };
  clutter_frame_clock_notify_presented (frame_clock, &frame_info);
  clutter_frame_clock_schedule_update (frame_clock);

  return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
}

static const ClutterFrameListenerIface timeline_frame_listener_iface = {
  .frame = timeline_frame_clock_frame,
};

static void
on_marker_reached (ClutterTimeline *timeline,
                   const char      *marker_name,
                   unsigned int     frame_number,
                   gboolean        *marker_reached)
{
  *marker_reached = TRUE;
}

static void
on_timeline_new_frame (ClutterTimeline *timeline,
                       int              time_ms,
                       int             *frame_counter)
{
  (*frame_counter)++;
}

static void
on_timeline_completed (ClutterTimeline *timeline,
                       GMainLoop       *main_loop)
{
  g_main_loop_quit (main_loop);
}

static void
frame_clock_timeline_basic (void)
{
  GMainLoop *main_loop;
  ClutterFrameClock *frame_clock;
  ClutterTimeline *timeline;
  gboolean marker1_reached;
  int frame_counter;
  int64_t before_us;
  int64_t after_us;

  main_loop = g_main_loop_new (NULL, FALSE);
  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &timeline_frame_listener_iface,
                                         NULL);
  g_object_add_weak_pointer (G_OBJECT (frame_clock), (gpointer *) &frame_clock);

  timeline = g_object_new (CLUTTER_TYPE_TIMELINE,
                           "duration", 1000,
                           "frame-clock", frame_clock,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (timeline), (gpointer *) &timeline);

  clutter_timeline_add_marker_at_time (timeline, "marker1", 500);

  marker1_reached = FALSE;
  frame_counter = 0;

  g_signal_connect (timeline, "marker-reached::marker1",
                    G_CALLBACK (on_marker_reached),
                    &marker1_reached);
  g_signal_connect (timeline, "new-frame",
                    G_CALLBACK (on_timeline_new_frame),
                    &frame_counter);
  g_signal_connect (timeline, "completed",
                    G_CALLBACK (on_timeline_completed),
                    main_loop);

  clutter_timeline_start (timeline);

  before_us = g_get_monotonic_time ();

  g_main_loop_run (main_loop);

  after_us = g_get_monotonic_time ();

  g_assert_cmpint (after_us - before_us,
                   >=,
                   ms2us (clutter_timeline_get_duration (timeline)));

  g_assert_true (marker1_reached);

  /* Just check that we got at least a few frames. Require too high and we'll be
   * flaky.
   */
  g_assert_cmpint (frame_counter, >, 20);

  g_main_loop_unref (main_loop);
  g_object_unref (timeline);
  g_assert_null (timeline);
  clutter_frame_clock_destroy (frame_clock);
  g_assert_null (frame_clock);
}

static void
on_switch_reached (ClutterTimeline   *timeline,
                   const char        *marker_name,
                   unsigned int       frame_number,
                   ClutterFrameClock *new_frame_clock)
{
  ClutterFrameClock *old_frame_clock;

  old_frame_clock = clutter_timeline_get_frame_clock (timeline);
  clutter_frame_clock_inhibit (old_frame_clock);

  clutter_timeline_set_frame_clock (timeline, new_frame_clock);
}

static void
frame_clock_timeline_switch (void)
{
  GMainLoop *main_loop;
  ClutterFrameClock *frame_clock2;
  ClutterFrameClock *frame_clock1;
  ClutterTimeline *timeline;
  int frame_counter;
  int64_t before_us;
  int64_t after_us;

  main_loop = g_main_loop_new (NULL, FALSE);

  frame_clock1 = clutter_frame_clock_new (refresh_rate,
                                          &timeline_frame_listener_iface,
                                          NULL);
  g_object_add_weak_pointer (G_OBJECT (frame_clock1), (gpointer *) &frame_clock1);
  frame_clock2 = clutter_frame_clock_new (refresh_rate,
                                          &timeline_frame_listener_iface,
                                          NULL);
  g_object_add_weak_pointer (G_OBJECT (frame_clock2), (gpointer *) &frame_clock2);

  timeline = g_object_new (CLUTTER_TYPE_TIMELINE,
                           "duration", 1000,
                           "frame-clock", frame_clock1,
                           NULL);
  g_object_add_weak_pointer (G_OBJECT (timeline), (gpointer *) &timeline);

  clutter_timeline_add_marker_at_time (timeline, "switch", 500);

  frame_counter = 0;

  g_signal_connect (timeline, "marker-reached::switch",
                    G_CALLBACK (on_switch_reached),
                    frame_clock2);
  g_signal_connect (timeline, "new-frame",
                    G_CALLBACK (on_timeline_new_frame),
                    &frame_counter);
  g_signal_connect (timeline, "completed",
                    G_CALLBACK (on_timeline_completed),
                    main_loop);

  clutter_timeline_start (timeline);

  before_us = g_get_monotonic_time ();

  g_main_loop_run (main_loop);

  after_us = g_get_monotonic_time ();

  g_assert_cmpint (after_us - before_us,
                   >=,
                   ms2us (clutter_timeline_get_duration (timeline)));

  g_assert (clutter_timeline_get_frame_clock (timeline) == frame_clock2);

  /* The duration is 1s, with a 60hz clock, and we switch after 0.5s. To verify
   * we continued to get frames, check that we have a bit more than half of the
   * frames accounted for.
   */
  g_assert_cmpint (frame_counter, >, 35);

  g_main_loop_unref (main_loop);
  g_object_unref (timeline);
  g_assert_null (timeline);
  clutter_frame_clock_destroy (frame_clock1);
  g_assert_null (frame_clock1);
  clutter_frame_clock_destroy (frame_clock2);
  g_assert_null (frame_clock2);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/frame-clock/timeline/basic", frame_clock_timeline_basic)
  CLUTTER_TEST_UNIT ("/frame-clock/timeline/switch", frame_clock_timeline_switch)
)
