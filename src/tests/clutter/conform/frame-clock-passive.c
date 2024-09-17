/*
 * Copyright (C) 2021 Red Hat Inc.
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
#include "tests/clutter-test-utils.h"

typedef enum
{
  STATE_INIT,
  STATE_PENDING_DISPATCH,
  STATE_PENDING_FRAME,
  STATE_FRAME_RECEIVED,
} State;

typedef struct
{
  ClutterFrameClock *frame_clock;
  State state;
  gboolean dispatching;
} TestCase;

static ClutterFrameResult
frame_clock_frame (ClutterFrameClock *frame_clock,
                   ClutterFrame      *frame,
                   gpointer           user_data)
{
  TestCase *test_case = user_data;

  g_assert_true (test_case->dispatching);
  g_assert_cmpint (test_case->state, ==, STATE_PENDING_FRAME);
  test_case->state = STATE_FRAME_RECEIVED;

  return CLUTTER_FRAME_RESULT_IDLE;
}

static const ClutterFrameListenerIface frame_listener_iface = {
  .frame = frame_clock_frame,
};

static gboolean
dispatch_idle (gpointer user_data)
{
  TestCase *test_case = user_data;

  g_assert_cmpint (test_case->state, ==, STATE_PENDING_DISPATCH);
  test_case->state = STATE_PENDING_FRAME;

  test_case->dispatching = TRUE;
  clutter_frame_clock_dispatch (test_case->frame_clock,
                                g_get_monotonic_time ());
  test_case->dispatching = FALSE;

  return G_SOURCE_REMOVE;
}

struct _TestDriver
{
  GObject parent;

  TestCase *test_case;
};

G_DECLARE_FINAL_TYPE (TestDriver, test_driver, TEST, DRIVER,
                      ClutterFrameClockDriver)
G_DEFINE_TYPE (TestDriver, test_driver, CLUTTER_TYPE_FRAME_CLOCK_DRIVER)

static void
test_driver_schedule_update (ClutterFrameClockDriver *driver)
{
  TestDriver *test_driver = TEST_DRIVER (driver);
  TestCase *test_case = test_driver->test_case;

  g_assert_cmpint (test_case->state, ==, STATE_INIT);
  test_case->state = STATE_PENDING_DISPATCH;

  g_idle_add (dispatch_idle, test_case);
}

static void
test_driver_class_init (TestDriverClass *klass)
{
  ClutterFrameClockDriverClass *driver_class =
    CLUTTER_FRAME_CLOCK_DRIVER_CLASS (klass);

  driver_class->schedule_update = test_driver_schedule_update;
}

static void
test_driver_init (TestDriver *test_driver)
{
}

static void
frame_clock_passive_basic (void)
{
  g_autoptr (ClutterFrameClock) frame_clock = NULL;
  g_autoptr (TestDriver) test_driver = NULL;
  TestCase test_case;

  test_driver = g_object_new (test_driver_get_type (), NULL);
  test_driver->test_case = &test_case;

  frame_clock = clutter_frame_clock_new (60.0f, 0, NULL,
                                         &frame_listener_iface,
                                         &test_case);
  clutter_frame_clock_set_passive (frame_clock,
                                   CLUTTER_FRAME_CLOCK_DRIVER (test_driver));

  test_case = (TestCase) {
    .frame_clock = frame_clock,
    .state = STATE_INIT,
  };

  clutter_frame_clock_schedule_update (frame_clock);

  while (test_case.state != STATE_FRAME_RECEIVED)
    g_main_context_iteration (NULL, TRUE);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/frame-clock/passive/basic", frame_clock_passive_basic)
)
