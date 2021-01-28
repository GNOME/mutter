#include "clutter/clutter.h"
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

static void
init_frame_info (ClutterFrameInfo *frame_info,
                 int64_t           presentation_time_us)
{
  *frame_info = (ClutterFrameInfo) {
    .presentation_time = presentation_time_us,
    .refresh_rate = refresh_rate,
    .flags = CLUTTER_FRAME_INFO_FLAG_NONE,
    .sequence = 0,
  };
}

static gboolean
fake_hw_clock_source_dispatch (GSource     *source,
                               GSourceFunc  callback,
                               gpointer     user_data)
{
  FakeHwClock *fake_hw_clock = (FakeHwClock *) source;
  ClutterFrameClock *frame_clock = fake_hw_clock->frame_clock;

  if (fake_hw_clock->has_pending_present)
    {
      ClutterFrameInfo frame_info;

      fake_hw_clock->has_pending_present = FALSE;
      init_frame_info (&frame_info, g_source_get_time (source));
      clutter_frame_clock_notify_presented (frame_clock, &frame_info);
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
                   int64_t            time_us,
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

  clutter_frame_clock_destroy (frame_clock);
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
                             int64_t            time_us,
                             gpointer           user_data)
{
  GMainLoop *main_loop = user_data;
  ClutterFrameInfo frame_info;

  g_assert_cmpint (frame_count, ==, expected_frame_count);

  expected_frame_count++;

  if (test_frame_count == 0)
    {
      g_main_loop_quit (main_loop);
      return CLUTTER_FRAME_RESULT_IDLE;
    }

  test_frame_count--;

  init_frame_info (&frame_info, g_get_monotonic_time ());
  clutter_frame_clock_notify_presented (frame_clock, &frame_info);
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
  clutter_frame_clock_destroy (frame_clock);
}

static gboolean
schedule_update_timeout (gpointer user_data)
{
  ClutterFrameClock *frame_clock = user_data;

  clutter_frame_clock_schedule_update (frame_clock);

  return G_SOURCE_REMOVE;
}

static ClutterFrameResult
delayed_damage_frame_clock_frame (ClutterFrameClock *frame_clock,
                                  int64_t            frame_count,
                                  int64_t            time_us,
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

  g_timeout_add (100, schedule_update_timeout, frame_clock);

  return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
}

static const ClutterFrameListenerIface delayed_damage_frame_listener_iface = {
  .frame = delayed_damage_frame_clock_frame,
};

static void
frame_clock_delayed_damage (void)
{
  FrameClockTest test;
  ClutterFrameClock *frame_clock;
  int64_t before_us;
  int64_t after_us;
  FakeHwClock *fake_hw_clock;
  GSource *source;

  test_frame_count = 2;
  expected_frame_count = 0;

  test.main_loop = g_main_loop_new (NULL, FALSE);
  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &delayed_damage_frame_listener_iface,
                                         &test);

  fake_hw_clock = fake_hw_clock_new (frame_clock, NULL, NULL);
  source = &fake_hw_clock->source;
  g_source_attach (source, NULL);

  test.fake_hw_clock = fake_hw_clock;

  before_us = g_get_monotonic_time ();

  clutter_frame_clock_schedule_update (frame_clock);
  g_main_loop_run (test.main_loop);

  after_us = g_get_monotonic_time ();

  g_assert_cmpint (after_us - before_us, >, 100000 + refresh_interval_us);

  g_main_loop_unref (test.main_loop);
  clutter_frame_clock_destroy (frame_clock);
  g_source_destroy (source);
  g_source_unref (source);
}

static ClutterFrameResult
no_damage_frame_clock_frame (ClutterFrameClock *frame_clock,
                             int64_t            frame_count,
                             int64_t            time_us,
                             gpointer           user_data)
{
  g_assert_not_reached ();

  return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
}

static const ClutterFrameListenerIface no_damage_frame_listener_iface = {
  .frame = no_damage_frame_clock_frame,
};

static gboolean
quit_main_loop_idle (gpointer user_data)
{
  GMainLoop *main_loop = user_data;

  g_main_loop_quit (main_loop);

  return G_SOURCE_REMOVE;
}

static void
frame_clock_no_damage (void)
{
  GMainLoop *main_loop;
  ClutterFrameClock *frame_clock;

  test_frame_count = 10;
  expected_frame_count = 0;

  main_loop = g_main_loop_new (NULL, FALSE);
  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &no_damage_frame_listener_iface,
                                         NULL);

  g_timeout_add (100, quit_main_loop_idle, main_loop);

  g_main_loop_run (main_loop);

  g_main_loop_unref (main_loop);
  clutter_frame_clock_destroy (frame_clock);
}

typedef struct _UpdateNowFrameClockTest
{
  FrameClockTest base;
  guint idle_source_id;
} UpdateNowFrameClockTest;

static ClutterFrameResult
update_now_frame_clock_frame (ClutterFrameClock *frame_clock,
                              int64_t            frame_count,
                              int64_t            time_us,
                              gpointer           user_data)
{
  UpdateNowFrameClockTest *test = user_data;
  GMainLoop *main_loop = test->base.main_loop;

  g_assert_cmpint (frame_count, ==, expected_frame_count);

  expected_frame_count++;

  g_clear_handle_id (&test->idle_source_id, g_source_remove);

  if (test_frame_count == 0)
    {
      g_main_loop_quit (main_loop);
      return CLUTTER_FRAME_RESULT_IDLE;
    }
  else
    {
      test->base.fake_hw_clock->has_pending_present = TRUE;
    }

  test_frame_count--;

  return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
}

static const ClutterFrameListenerIface update_now_frame_listener_iface = {
  .frame = update_now_frame_clock_frame,
};

static gboolean
assert_not_reached_idle (gpointer user_data)
{
  g_assert_not_reached ();
  return G_SOURCE_REMOVE;
}

static gboolean
schedule_update_now_hw_callback (gpointer user_data)
{
  UpdateNowFrameClockTest *test = user_data;
  ClutterFrameClock *frame_clock = test->base.fake_hw_clock->frame_clock;

  clutter_frame_clock_schedule_update_now (frame_clock);
  g_assert (!test->idle_source_id);
  test->idle_source_id = g_idle_add (assert_not_reached_idle, NULL);

  return G_SOURCE_CONTINUE;
}

static void
frame_clock_schedule_update_now (void)
{
  UpdateNowFrameClockTest test = { 0 };
  ClutterFrameClock *frame_clock;
  int64_t before_us;
  int64_t after_us;
  GSource *source;
  FakeHwClock *fake_hw_clock;

  test_frame_count = 10;
  expected_frame_count = 0;

  test.base.main_loop = g_main_loop_new (NULL, FALSE);
  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &update_now_frame_listener_iface,
                                         &test);

  fake_hw_clock = fake_hw_clock_new (frame_clock,
                                     schedule_update_now_hw_callback,
                                     &test);
  source = &fake_hw_clock->source;
  g_source_attach (source, NULL);

  test.base.fake_hw_clock = fake_hw_clock;

  before_us = g_get_monotonic_time ();

  clutter_frame_clock_schedule_update (frame_clock);
  g_main_loop_run (test.base.main_loop);

  after_us = g_get_monotonic_time ();

  g_assert_cmpint (after_us - before_us, >, 10 * refresh_interval_us);

  g_main_loop_unref (test.base.main_loop);

  clutter_frame_clock_destroy (frame_clock);
  g_source_destroy (source);
  g_source_unref (source);
}

static void
before_frame_frame_clock_before_frame (ClutterFrameClock *frame_clock,
                                       int64_t            frame_count,
                                       gpointer           user_data)
{
  int64_t *expected_frame_count = user_data;

  g_assert_cmpint (*expected_frame_count, ==, frame_count);
}

static ClutterFrameResult
before_frame_frame_clock_frame (ClutterFrameClock *frame_clock,
                                int64_t            frame_count,
                                int64_t            time_us,
                                gpointer           user_data)
{
  int64_t *expected_frame_count = user_data;
  ClutterFrameInfo frame_info;

  g_assert_cmpint (*expected_frame_count, ==, frame_count);

  (*expected_frame_count)++;

  init_frame_info (&frame_info, g_get_monotonic_time ());
  clutter_frame_clock_notify_presented (frame_clock, &frame_info);
  clutter_frame_clock_schedule_update (frame_clock);

  return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
}

static const ClutterFrameListenerIface before_frame_frame_listener_iface = {
  .before_frame = before_frame_frame_clock_before_frame,
  .frame = before_frame_frame_clock_frame,
};

static gboolean
quit_main_loop_timeout (gpointer user_data)
{
  GMainLoop *main_loop = user_data;

  g_main_loop_quit (main_loop);

  return G_SOURCE_REMOVE;
}

static void
frame_clock_before_frame (void)
{
  GMainLoop *main_loop;
  ClutterFrameClock *frame_clock;

  expected_frame_count = 0;

  main_loop = g_main_loop_new (NULL, FALSE);
  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &before_frame_frame_listener_iface,
                                         &expected_frame_count);

  clutter_frame_clock_schedule_update (frame_clock);
  g_timeout_add (100, quit_main_loop_timeout, main_loop);
  g_main_loop_run (main_loop);

  /* We should have at least processed a couple of frames within 100 ms. */
  g_assert_cmpint (expected_frame_count, >, 2);

  g_main_loop_unref (main_loop);
  clutter_frame_clock_destroy (frame_clock);
}

typedef struct _InhibitTest
{
  GMainLoop *main_loop;
  ClutterFrameClock *frame_clock;

  gboolean frame_count;
  gboolean pending_inhibit;
  gboolean pending_quit;
} InhibitTest;

static ClutterFrameResult
inhibit_frame_clock_frame (ClutterFrameClock *frame_clock,
                           int64_t            frame_count,
                           int64_t            time_us,
                           gpointer           user_data)
{
  InhibitTest *test = user_data;
  ClutterFrameInfo frame_info;

  g_assert_cmpint (frame_count, ==, test->frame_count);

  test->frame_count++;

  init_frame_info (&frame_info, g_get_monotonic_time ());
  clutter_frame_clock_notify_presented (frame_clock, &frame_info);
  clutter_frame_clock_schedule_update (frame_clock);

  if (test->pending_inhibit)
    {
      test->pending_inhibit = FALSE;
      clutter_frame_clock_inhibit (frame_clock);
    }

  clutter_frame_clock_schedule_update (frame_clock);

  if (test->pending_quit)
    g_main_loop_quit (test->main_loop);

  return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
}

static const ClutterFrameListenerIface inhibit_frame_listener_iface = {
  .frame = inhibit_frame_clock_frame,
};

static gboolean
uninhibit_timeout (gpointer user_data)
{
  InhibitTest *test = user_data;

  g_assert_cmpint (test->frame_count, ==, 1);

  clutter_frame_clock_uninhibit (test->frame_clock);
  test->pending_quit = TRUE;

  return G_SOURCE_REMOVE;
}

static void
frame_clock_inhibit (void)
{
  InhibitTest test = { 0 };

  expected_frame_count = 0;

  test.main_loop = g_main_loop_new (NULL, FALSE);
  test.frame_clock = clutter_frame_clock_new (refresh_rate,
                                              &inhibit_frame_listener_iface,
                                              &test);

  test.pending_inhibit = TRUE;

  clutter_frame_clock_schedule_update (test.frame_clock);
  g_timeout_add (100, uninhibit_timeout, &test);
  g_main_loop_run (test.main_loop);

  g_assert_cmpint (test.frame_count, ==, 2);

  g_main_loop_unref (test.main_loop);
  clutter_frame_clock_destroy (test.frame_clock);
}

typedef struct _RescheduleOnIdleFrameClockTest
{
  FrameClockTest base;
} RescheduleOnIdleFrameClockTest;

static ClutterFrameResult
reschedule_on_idle_clock_frame (ClutterFrameClock *frame_clock,
                                int64_t            frame_count,
                                int64_t            time_us,
                                gpointer           user_data)
{
  RescheduleOnIdleFrameClockTest *test = user_data;
  GMainLoop *main_loop = test->base.main_loop;

  g_assert_cmpint (frame_count, ==, expected_frame_count);

  expected_frame_count++;

  if (test_frame_count == 0)
    {
      g_main_loop_quit (main_loop);
      return CLUTTER_FRAME_RESULT_IDLE;
    }

  test_frame_count--;

  clutter_frame_clock_schedule_update (frame_clock);

  return CLUTTER_FRAME_RESULT_IDLE;
}

static const ClutterFrameListenerIface reschedule_on_idle_listener_iface = {
  .frame = reschedule_on_idle_clock_frame,
};

static void
frame_clock_reschedule_on_idle (void)
{
  RescheduleOnIdleFrameClockTest test;
  ClutterFrameClock *frame_clock;
  FakeHwClock *fake_hw_clock;
  GSource *source;

  test_frame_count = 10;
  expected_frame_count = 0;

  test.base.main_loop = g_main_loop_new (NULL, FALSE);
  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &reschedule_on_idle_listener_iface,
                                         &test);
  fake_hw_clock = fake_hw_clock_new (frame_clock, NULL, NULL);
  source = &fake_hw_clock->source;
  g_source_attach (source, NULL);
  test.base.fake_hw_clock = fake_hw_clock;

  clutter_frame_clock_schedule_update (frame_clock);
  g_main_loop_run (test.base.main_loop);

  g_main_loop_unref (test.base.main_loop);
  clutter_frame_clock_destroy (frame_clock);
}

static const ClutterFrameListenerIface dummy_frame_listener_iface = {
  .frame = NULL,
};

static void
on_destroy (ClutterFrameClock *frame_clock,
            gboolean          *destroy_signalled)
{
  g_assert_false (*destroy_signalled);
  *destroy_signalled = TRUE;
}

static void
frame_clock_destroy_signal (void)
{
  ClutterFrameClock *frame_clock;
  ClutterFrameClock *frame_clock_backup;
  gboolean destroy_signalled;

  /* Test that the destroy signal is emitted when removing last reference. */

  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &dummy_frame_listener_iface,
                                         NULL);

  destroy_signalled = FALSE;
  g_signal_connect (frame_clock, "destroy",
                    G_CALLBACK (on_destroy),
                    &destroy_signalled);
  g_object_add_weak_pointer (G_OBJECT (frame_clock), (gpointer *) &frame_clock);

  g_object_unref (frame_clock);
  g_assert_true (destroy_signalled);
  g_assert_null (frame_clock);

  /* Test that destroy signal is emitted when destroying with references still
   * left.
   */

  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &dummy_frame_listener_iface,
                                         NULL);
  frame_clock_backup = frame_clock;

  destroy_signalled = FALSE;
  g_signal_connect (frame_clock, "destroy",
                    G_CALLBACK (on_destroy),
                    &destroy_signalled);
  g_object_add_weak_pointer (G_OBJECT (frame_clock), (gpointer *) &frame_clock);
  g_object_ref (frame_clock);

  clutter_frame_clock_destroy (frame_clock);
  g_assert_true (destroy_signalled);
  g_assert_null (frame_clock);
  g_object_unref (frame_clock_backup);
}

static gboolean
notify_ready_and_schedule_update_idle (gpointer user_data)
{
  ClutterFrameClock *frame_clock = user_data;

  clutter_frame_clock_notify_ready (frame_clock);
  clutter_frame_clock_schedule_update (frame_clock);

  return G_SOURCE_REMOVE;
}

static ClutterFrameResult
frame_clock_ready_frame (ClutterFrameClock *frame_clock,
                         int64_t            frame_count,
                         int64_t            time_us,
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

  g_idle_add (notify_ready_and_schedule_update_idle, frame_clock);

  return CLUTTER_FRAME_RESULT_PENDING_PRESENTED;
}

static const ClutterFrameListenerIface frame_clock_ready_listener_iface = {
  .frame = frame_clock_ready_frame,
};

static void
frame_clock_notify_ready (void)
{
  GMainLoop *main_loop;
  ClutterFrameClock *frame_clock;
  int64_t before_us;
  int64_t after_us;

  test_frame_count = 10;
  expected_frame_count = 0;

  main_loop = g_main_loop_new (NULL, FALSE);
  frame_clock = clutter_frame_clock_new (refresh_rate,
                                         &frame_clock_ready_listener_iface,
                                         main_loop);

  before_us = g_get_monotonic_time ();

  clutter_frame_clock_schedule_update (frame_clock);
  g_main_loop_run (main_loop);

  after_us = g_get_monotonic_time ();

  /* The initial frame will only be delayed by 2 ms, so we are checking one
   * less.
   */
  g_assert_cmpint (after_us - before_us, >, 8 * refresh_interval_us);

  g_main_loop_unref (main_loop);
  clutter_frame_clock_destroy (frame_clock);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/frame-clock/schedule-update", frame_clock_schedule_update)
  CLUTTER_TEST_UNIT ("/frame-clock/immediate-present", frame_clock_immediate_present)
  CLUTTER_TEST_UNIT ("/frame-clock/delayed-damage", frame_clock_delayed_damage)
  CLUTTER_TEST_UNIT ("/frame-clock/no-damage", frame_clock_no_damage)
  CLUTTER_TEST_UNIT ("/frame-clock/schedule-update-now", frame_clock_schedule_update_now)
  CLUTTER_TEST_UNIT ("/frame-clock/before-frame", frame_clock_before_frame)
  CLUTTER_TEST_UNIT ("/frame-clock/inhibit", frame_clock_inhibit)
  CLUTTER_TEST_UNIT ("/frame-clock/reschedule-on-idle", frame_clock_reschedule_on_idle)
  CLUTTER_TEST_UNIT ("/frame-clock/destroy-signal", frame_clock_destroy_signal)
  CLUTTER_TEST_UNIT ("/frame-clock/notify-ready", frame_clock_notify_ready)
)
