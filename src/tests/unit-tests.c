/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat, Inc.
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

#include "tests/unit-tests.h"

#include <glib.h>
#include <stdlib.h>

#include <meta/main.h>
#include <meta/util.h>

#include "core/boxes-private.h"
#include "core/display-private.h"
#include "meta-test/meta-context-test.h"
#include "meta/compositor.h"
#include "meta/meta-context.h"
#include "tests/boxes-tests.h"
#include "tests/monitor-store-unit-tests.h"
#include "tests/monitor-transform-tests.h"
#include "tests/meta-test-utils.h"
#include "tests/orientation-manager-unit-tests.h"
#include "tests/hdr-metadata-unit-tests.h"
#include "tests/button-transform-tests.h"

MetaContext *test_context;

typedef struct _MetaTestLaterOrderCallbackData
{
  GMainLoop *loop; /* Loop to terminate when done. */
  int callback_num; /* Callback number integer. */
  int *expected_callback_num; /* Pointer to the expected callback number. */
} MetaTestLaterOrderCallbackData;

static gboolean
test_later_order_callback (gpointer user_data)
{
  MetaTestLaterOrderCallbackData *data = user_data;

  g_assert_cmpint (data->callback_num, ==, *data->expected_callback_num);

  if (*data->expected_callback_num == 0)
    g_main_loop_quit (data->loop);
  else
    (*data->expected_callback_num)--;

  return FALSE;
}

static void
meta_test_util_later_order (void)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaLaters *laters = meta_compositor_get_laters (compositor);
  GMainLoop *loop;
  int expected_callback_num;
  int i;
  const int num_callbacks = 3;
  MetaTestLaterOrderCallbackData callback_data[num_callbacks];

  loop = g_main_loop_new (NULL, FALSE);

  /* Schedule three BEFORE_DRAW callbacks each with its own number associated
   * with it.
   */
  for (i = 0; i < num_callbacks; i++)
    {
      callback_data[i] = (MetaTestLaterOrderCallbackData) {
        .loop = loop,
        .callback_num = i,
        .expected_callback_num = &expected_callback_num,
      };
      meta_laters_add (laters, META_LATER_BEFORE_REDRAW,
                       test_later_order_callback,
                       &callback_data[i],
                       NULL);
    }

  /* Check that the callbacks are invoked in the opposite order that they were
   * scheduled. Each callback will decrease the number by 1 after it checks the
   * validity.
   */
  expected_callback_num = num_callbacks - 1;
  g_main_loop_run (loop);
  g_assert_cmpint (expected_callback_num, ==, 0);
  g_main_loop_unref (loop);
}

typedef enum _MetaTestLaterScheduleFromLaterState
{
  META_TEST_LATER_EXPECT_CALC_SHOWING,
  META_TEST_LATER_EXPECT_SYNC_STACK,
  META_TEST_LATER_EXPECT_BEFORE_REDRAW,
  META_TEST_LATER_FINISHED,
} MetaTestLaterScheduleFromLaterState;

typedef struct _MetaTestLaterScheduleFromLaterData
{
  GMainLoop *loop;
  MetaTestLaterScheduleFromLaterState state;
} MetaTestLaterScheduleFromLaterData;

static gboolean
test_later_schedule_from_later_sync_stack_callback (gpointer user_data);

static gboolean
test_later_schedule_from_later_calc_showing_callback (gpointer user_data)
{
  MetaTestLaterScheduleFromLaterData *data = user_data;
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaLaters *laters = meta_compositor_get_laters (compositor);

  g_assert_cmpint (data->state, ==, META_TEST_LATER_EXPECT_CALC_SHOWING);

  meta_laters_add (laters, META_LATER_SYNC_STACK,
                   test_later_schedule_from_later_sync_stack_callback,
                   data,
                   NULL);

  data->state = META_TEST_LATER_EXPECT_SYNC_STACK;

  return FALSE;
}

static gboolean
test_later_schedule_from_later_sync_stack_callback (gpointer user_data)
{
  MetaTestLaterScheduleFromLaterData *data = user_data;

  g_assert_cmpint (data->state, ==, META_TEST_LATER_EXPECT_SYNC_STACK);

  data->state = META_TEST_LATER_EXPECT_BEFORE_REDRAW;

  return FALSE;
}

static gboolean
test_later_schedule_from_later_before_redraw_callback (gpointer user_data)
{
  MetaTestLaterScheduleFromLaterData *data = user_data;

  g_assert_cmpint (data->state, ==, META_TEST_LATER_EXPECT_BEFORE_REDRAW);
  data->state = META_TEST_LATER_FINISHED;
  g_main_loop_quit (data->loop);

  return FALSE;
}

static void
meta_test_util_later_schedule_from_later (void)
{
  MetaTestLaterScheduleFromLaterData data;
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaLaters *laters = meta_compositor_get_laters (compositor);

  data.loop = g_main_loop_new (NULL, FALSE);

  /* Test that scheduling a MetaLater with 'when' being later than the one being
   * invoked causes it to be invoked before any callback with a later 'when'
   * value being invoked.
   *
   * The first and last callback is queued here. The one to be invoked in
   * between is invoked in test_later_schedule_from_later_calc_showing_callback.
   */
  meta_laters_add (laters, META_LATER_CALC_SHOWING,
                   test_later_schedule_from_later_calc_showing_callback,
                   &data,
                   NULL);
  meta_laters_add (laters, META_LATER_BEFORE_REDRAW,
                   test_later_schedule_from_later_before_redraw_callback,
                   &data,
                   NULL);

  data.state = META_TEST_LATER_EXPECT_CALC_SHOWING;

  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);

  g_assert_cmpint (data.state, ==, META_TEST_LATER_FINISHED);
}

static void
init_tests (void)
{
  g_test_add_func ("/util/meta-later/order", meta_test_util_later_order);
  g_test_add_func ("/util/meta-later/schedule-from-later",
                   meta_test_util_later_schedule_from_later);

  init_monitor_store_tests ();
  init_boxes_tests ();
  init_monitor_transform_tests ();
  init_orientation_manager_tests ();
  init_hdr_metadata_tests ();
  init_button_transform_tests ();
}

int
main (int argc, char *argv[])
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  if (!meta_context_configure (context, &argc, &argv, &error))
    g_error ("Failed to configure test context: %s", error->message);

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
