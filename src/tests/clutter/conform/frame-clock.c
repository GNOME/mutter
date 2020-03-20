#include "clutter/clutter-frame-clock.h"
#include "tests/clutter-test-utils.h"

static const float refresh_rate = 60.0;
static const int64_t refresh_interval_us = (int64_t) (0.5 + G_USEC_PER_SEC /
                                                      refresh_rate);

static int64_t test_frame_count;
static int64_t expected_frame_count;

typedef struct _FakeHwClock
{
  GSource source;

  ClutterFrameClock *frame_clock;

  int64_t next_presentation_time_us;
  gboolean has_pending_present;
} FakeHwClock;

typedef struct _FrameClockTest
{
  FakeHwClock *fake_hw_clock;

  GMainLoop *main_loop;
} FrameClockTest;

static gboolean
fake_hw_clock_source_dispatch (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
  FakeHwClock *fake_hw_clock = (FakeHwClock *) source;
  ClutterFrameClock *frame_clock = fake_hw_clock->frame_clock;

  if (fake_hw_clock->has_pending_present)
    {
      fake_hw_clock->has_pending_present = FALSE;
      clutter_frame_clock_notify_presented (frame_clock,
                                            g_source_get_time (source));
      if (callback)
        callback (user_data);
    }

  fake_hw_clock->next_presentation_time_us += refresh_interval_us;
  g_source_set_ready_time (source, fake_hw_clock->next_presentation_time_us);

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs fake_hw_clock_source_funcs = {
  NULL,
  NULL,
  fake_hw_clock_source_dispatch,
  NULL
};

static FakeHwClock *
fake_hw_clock_new (ClutterFrameClock *frame_clock,
                   GSourceFunc        callback,
                   gpointer           user_data)
{
  GSource *source;
  FakeHwClock *fake_hw_clock;

  source = g_source_new (&fake_hw_clock_source_funcs, sizeof (FakeHwClock));
  fake_hw_clock = (FakeHwClock *) source;
  fake_hw_clock->frame_clock = frame_clock;

  fake_hw_clock->next_presentation_time_us =
    g_get_monotonic_time () + refresh_interval_us;
  g_source_set_ready_time (source, fake_hw_clock->next_presentation_time_us);
  g_source_set_callback (source, callback, user_data, NULL);

  return fake_hw_clock;
}

static ClutterFrameResult
frame_clock_frame (ClutterFrameClock *frame_clock,
                   int64_t            frame_count,
                   gpointer           user_data)
{
  FrameClockTest *test = user_data;
  GMainLoop *main_loop = test->main_loop;

  g_assert_cmpint (frame_count, ==, expected_frame_count);

  expected_frame_count++;

  if (test_frame_count == 0)
    {
      g_main_loop_quit (main_loop);
      return CLUTTER_FRAME_RESULT_IDLE;
    }
  else
    {
      test->fake_hw_clock->has_pending_present = TRUE;
    }

  test_frame_count--;

  return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
}

static const ClutterFrameListenerIface frame_listener_iface = {
  .frame = frame_clock_frame,
};

static gboolean
schedule_update_hw_callback (gpointer user_data)
{
  ClutterFrameClock *frame_clock = user_data;

  clutter_frame_clock_schedule_update (frame_clock);

  return G_SOURCE_CONTINUE;
}

static void
frame_clock_schedule_update (void)
{
  FrameClockTest test;
  ClutterFrameClock *frame_clock;
  int64_t before_us;
  int64_t after_us;
  GSource *source;
  FakeHwClock *fake_hw_clock;

  test_frame_count = 10;
  expected_frame_count = 0;

  test.main_loop = g_main_loop_new (NULL, FALSE);
  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &frame_listener_iface,
                                         &test);

  fake_hw_clock = fake_hw_clock_new (frame_clock,
                                     schedule_update_hw_callback,
                                     frame_clock);
  source = &fake_hw_clock->source;
  g_source_attach (source, NULL);

  test.fake_hw_clock = fake_hw_clock;

  before_us = g_get_monotonic_time ();

  clutter_frame_clock_schedule_update (frame_clock);
  g_main_loop_run (test.main_loop);

  after_us = g_get_monotonic_time ();

  g_assert_cmpint (after_us - before_us, >, 10 * refresh_interval_us);

  g_main_loop_unref (test.main_loop);

  g_object_unref (frame_clock);
  g_source_destroy (source);
  g_source_unref (source);
}

static gboolean
schedule_update_idle (gpointer user_data)
{
  ClutterFrameClock *frame_clock = user_data;

  clutter_frame_clock_schedule_update (frame_clock);

  return G_SOURCE_REMOVE;
}

static ClutterFrameResult
immediate_frame_clock_frame (ClutterFrameClock *frame_clock,
                             int64_t            frame_count,
                             gpointer           user_data)
{
  GMainLoop *main_loop = user_data;

  g_assert_cmpint (frame_count, ==, expected_frame_count);

  expected_frame_count++;

  if (test_frame_count == 0)
    {
      g_main_loop_quit (main_loop);
      return CLUTTER_FRAME_RESULT_IDLE;
    }

  test_frame_count--;

  clutter_frame_clock_notify_presented (frame_clock, g_get_monotonic_time ());
  g_idle_add (schedule_update_idle, frame_clock);

  return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
}

static const ClutterFrameListenerIface immediate_frame_listener_iface = {
  .frame = immediate_frame_clock_frame,
};

static void
frame_clock_immediate_present (void)
{
  GMainLoop *main_loop;
  ClutterFrameClock *frame_clock;
  int64_t before_us;
  int64_t after_us;

  test_frame_count = 10;
  expected_frame_count = 0;

  main_loop = g_main_loop_new (NULL, FALSE);
  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &immediate_frame_listener_iface,
                                         main_loop);

  before_us = g_get_monotonic_time ();

  clutter_frame_clock_schedule_update (frame_clock);
  g_main_loop_run (main_loop);

  after_us = g_get_monotonic_time ();

  /* The initial frame will only be delayed by 2 ms, so we are checking one
   * less.
   */
  g_assert_cmpint (after_us - before_us, >, 9 * refresh_interval_us);

  g_main_loop_unref (main_loop);
  g_object_unref (frame_clock);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/frame-clock/schedule-update", frame_clock_schedule_update)
  CLUTTER_TEST_UNIT ("/frame-clock/immediate-present", frame_clock_immediate_present)
)
