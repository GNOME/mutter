/*
 * Copyright (C) 2020 Jonas Dreßler
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
#include "clutter/clutter-stage-view-private.h"
#include "compositor/meta-window-actor-private.h"
#include "meta-test/meta-context-test.h"
#include "meta/meta-window-actor.h"
#include "tests/meta-backend-test.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-test-utils.h"
#include "x11/meta-x11-display-private.h"

#define X11_TEST_CLIENT_NAME "x11_test_client"
#define X11_TEST_CLIENT_WINDOW "window1"

static MetaContext *test_context;
static MetaBackend *test_backend;

static MonitorTestCaseSetup initial_test_case_setup = {
  .modes = {
    {
      .width = 1024,
      .height = 768,
      .refresh_rate = 60.0
    }
  },
  .n_modes = 1,
  .outputs = {
     {
      .crtc = 0,
      .modes = { 0 },
      .n_modes = 1,
      .preferred_mode = 0,
      .possible_crtcs = { 0 },
      .n_possible_crtcs = 1,
      .width_mm = 222,
      .height_mm = 125
    },
    {
      .crtc = 1,
      .modes = { 0 },
      .n_modes = 1,
      .preferred_mode = 0,
      .possible_crtcs = { 1 },
      .n_possible_crtcs = 1,
      .width_mm = 220,
      .height_mm = 124
    }
  },
  .n_outputs = 2,
  .crtcs = {
    {
      .current_mode = 0
    },
    {
      .current_mode = 0
    }
  },
  .n_crtcs = 2
};

static void
meta_test_stage_views_exist (void)
{
  MetaBackend *backend = test_backend;
  ClutterActor *stage;
  GList *stage_views;

  stage = meta_backend_get_stage (backend);
  g_assert_cmpint ((int) clutter_actor_get_width (stage), ==, 1024 * 2);
  g_assert_cmpint ((int) clutter_actor_get_height (stage), ==, 768);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 2);
}

static void
wait_for_window_map (ClutterStage *stage,
                     ClutterActor *window_actor)
{
  while (!clutter_actor_is_mapped (window_actor))
    meta_wait_for_paint (stage);
}

static void
on_stage_views_changed (ClutterActor *actor,
                        gboolean     *stage_views_changed)
{
  *stage_views_changed = TRUE;
}

static void
is_on_stage_views (ClutterActor *actor,
                   unsigned int  n_views,
                   ...)
{
  va_list valist;
  int i = 0;
  GList *stage_views = clutter_actor_peek_stage_views (actor);

  va_start (valist, n_views);
  for (i = 0; i < n_views; i++)
    {
      ClutterStageView *view = va_arg (valist, ClutterStageView*);
      g_assert_nonnull (g_list_find (stage_views, view));
    }

  va_end (valist);
  g_assert_true (g_list_length (stage_views) == n_views);
}

static void
meta_test_actor_stage_views (void)
{
  MetaBackend *backend = test_backend;
  ClutterActor *stage, *container, *test_actor;
  GList *stage_views;
  gboolean stage_views_changed_container = FALSE;
  gboolean stage_views_changed_test_actor = FALSE;
  gboolean *stage_views_changed_container_ptr =
    &stage_views_changed_container;
  gboolean *stage_views_changed_test_actor_ptr =
    &stage_views_changed_test_actor;

  stage = meta_backend_get_stage (backend);
  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  container = clutter_actor_new ();
  clutter_actor_set_size (container, 100, 100);
  clutter_actor_add_child (stage, container);

  test_actor = clutter_actor_new ();
  clutter_actor_set_size (test_actor, 50, 50);
  clutter_actor_add_child (container, test_actor);

  g_signal_connect (container, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_container_ptr);
  g_signal_connect (test_actor, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_test_actor_ptr);

  clutter_actor_show (stage);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (container, 1, stage_views->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for the initial change */
  g_assert_true (stage_views_changed_container);
  g_assert_true (stage_views_changed_test_actor);
  stage_views_changed_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Move the container to the second stage view */
  clutter_actor_set_x (container, 1040);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (container, 1, stage_views->next->data);
  is_on_stage_views (test_actor, 1, stage_views->next->data);

  /* The signal was emitted again */
  g_assert_true (stage_views_changed_container);
  g_assert_true (stage_views_changed_test_actor);
  stage_views_changed_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Move the container so it's on both stage views while the test_actor
   * is only on the first one.
   */
  clutter_actor_set_x (container, 940);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (container, 2, stage_views->data, stage_views->next->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted again */
  g_assert_true (stage_views_changed_container);
  g_assert_true (stage_views_changed_test_actor);

  g_signal_handlers_disconnect_by_func (container, on_stage_views_changed,
                                        stage_views_changed_container_ptr);
  g_signal_handlers_disconnect_by_func (test_actor, on_stage_views_changed,
                                        stage_views_changed_test_actor_ptr);
  clutter_actor_destroy (container);
}

static void
on_relayout_actor_frame (ClutterTimeline *timeline,
                         int              msec,
                         ClutterActor    *actor)
{
  MetaBackend *backend = test_backend;
  ClutterActor *stage = meta_backend_get_stage (backend);

  clutter_stage_clear_stage_views (CLUTTER_STAGE (stage));
}

static void
meta_test_actor_stage_views_relayout (void)
{
  MetaBackend *backend = test_backend;
  ClutterActor *stage, *actor;
  ClutterTransition *transition;
  GMainLoop *main_loop;

  stage = meta_backend_get_stage (backend);

  actor = clutter_actor_new ();
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_easing_duration (actor, 100);
  clutter_actor_add_child (stage, actor);

  clutter_actor_show (stage);

  meta_wait_for_paint (CLUTTER_STAGE (stage));
  clutter_actor_set_position (actor, 1000.0, 0.0);
  transition = clutter_actor_get_transition (actor, "position");
  g_signal_connect_after (transition, "new-frame",
                          G_CALLBACK (on_relayout_actor_frame),
                          actor);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect_swapped (transition, "stopped",
                            G_CALLBACK (g_main_loop_quit),
                            main_loop);

  g_main_loop_run (main_loop);

  clutter_actor_destroy (actor);
  g_main_loop_unref (main_loop);
}

static void
meta_test_actor_stage_views_reparent (void)
{
  MetaBackend *backend = test_backend;
  ClutterActor *stage, *container, *test_actor;
  GList *stage_views;
  gboolean stage_views_changed_container = FALSE;
  gboolean stage_views_changed_test_actor = FALSE;
  gboolean *stage_views_changed_container_ptr =
    &stage_views_changed_container;
  gboolean *stage_views_changed_test_actor_ptr =
    &stage_views_changed_test_actor;

  stage = meta_backend_get_stage (backend);
  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  container = clutter_actor_new ();
  clutter_actor_set_size (container, 100, 100);
  clutter_actor_set_x (container, 1020);
  clutter_actor_add_child (stage, container);

  test_actor = clutter_actor_new ();
  clutter_actor_set_size (test_actor, 20, 20);
  clutter_actor_add_child (container, test_actor);

  g_signal_connect (container, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_container_ptr);
  g_signal_connect (test_actor, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_test_actor_ptr);

  clutter_actor_show (stage);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (container, 2, stage_views->data, stage_views->next->data);
  is_on_stage_views (test_actor, 2, stage_views->data, stage_views->next->data);

  /* The signal was emitted for both actors */
  g_assert_true (stage_views_changed_container);
  g_assert_true (stage_views_changed_test_actor);
  stage_views_changed_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Remove the test_actor from the scene-graph */
  g_object_ref (test_actor);
  clutter_actor_remove_child (container, test_actor);

  /* While the test_actor is not on stage, it must be on no stage views */
  is_on_stage_views (test_actor, 0);

  /* When the test_actor left the stage, the signal was emitted */
  g_assert_false (stage_views_changed_container);
  g_assert_true (stage_views_changed_test_actor);
  stage_views_changed_test_actor = FALSE;

  /* Add the test_actor again as a child of the stage */
  clutter_actor_add_child (stage, test_actor);
  g_object_unref (test_actor);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  /* The container is still on both stage views... */
  is_on_stage_views (container, 2, stage_views->data, stage_views->next->data);

  /* ...while the test_actor is only on the first one now */
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for the test_actor again */
  g_assert_false (stage_views_changed_container);
  g_assert_true (stage_views_changed_test_actor);
  stage_views_changed_test_actor = FALSE;

  /* Move the container out of the stage... */
  clutter_actor_set_y (container, 2000);
  g_object_ref (test_actor);
  clutter_actor_remove_child (stage, test_actor);

  /* When the test_actor left the stage, the signal was emitted */
  g_assert_false (stage_views_changed_container);
  g_assert_true (stage_views_changed_test_actor);
  stage_views_changed_test_actor = FALSE;

  /* ...and reparent the test_actor to the container again */
  clutter_actor_add_child (container, test_actor);
  g_object_unref (test_actor);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  /* Now both actors are on no stage views */
  is_on_stage_views (container, 0);
  is_on_stage_views (test_actor, 0);

  /* The signal was emitted only for the container, the test_actor already
   * has no stage-views.
   */
  g_assert_true (stage_views_changed_container);
  g_assert_false (stage_views_changed_test_actor);

  g_signal_handlers_disconnect_by_func (container, on_stage_views_changed,
                                        stage_views_changed_container_ptr);
  g_signal_handlers_disconnect_by_func (test_actor, on_stage_views_changed,
                                        stage_views_changed_test_actor_ptr);
  clutter_actor_destroy (container);
}

static void
meta_test_actor_stage_views_hide_parent (void)
{
  MetaBackend *backend = test_backend;
  ClutterActor *stage, *outer_container, *inner_container, *test_actor;
  GList *stage_views;
  gboolean stage_views_changed_outer_container = FALSE;
  gboolean stage_views_changed_inner_container = FALSE;
  gboolean stage_views_changed_test_actor = FALSE;
  gboolean *stage_views_changed_outer_container_ptr =
    &stage_views_changed_outer_container;
  gboolean *stage_views_changed_inner_container_ptr =
    &stage_views_changed_inner_container;
  gboolean *stage_views_changed_test_actor_ptr =
    &stage_views_changed_test_actor;

  stage = meta_backend_get_stage (backend);
  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  outer_container = clutter_actor_new ();
  clutter_actor_set_size (outer_container, 50, 50);
  clutter_actor_add_child (stage, outer_container);

  inner_container = clutter_actor_new ();
  clutter_actor_add_child (outer_container, inner_container);

  test_actor = clutter_actor_new ();
  clutter_actor_set_size (test_actor, 20, 20);
  clutter_actor_add_child (inner_container, test_actor);

  g_signal_connect (outer_container, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_outer_container_ptr);
  g_signal_connect (inner_container, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_inner_container_ptr);
  g_signal_connect (test_actor, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    stage_views_changed_test_actor_ptr);

  clutter_actor_show (stage);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  /* The containers and the test_actor are on all on the first view */
  is_on_stage_views (outer_container, 1, stage_views->data);
  is_on_stage_views (inner_container, 1, stage_views->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for all three */
  g_assert_true (stage_views_changed_outer_container);
  g_assert_true (stage_views_changed_inner_container);
  g_assert_true (stage_views_changed_test_actor);
  stage_views_changed_outer_container = FALSE;
  stage_views_changed_inner_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Hide the inner_container */
  clutter_actor_hide (inner_container);

  /* Move the outer_container so it's still on the first view */
  clutter_actor_set_x (outer_container, 1023);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  /* The outer_container is still expanded so it should be on both views */
  is_on_stage_views (outer_container, 2,
                     stage_views->data, stage_views->next->data);

  /* The inner_container and test_actor aren't updated because they're hidden */
  is_on_stage_views (inner_container, 1, stage_views->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for the outer_container */
  g_assert_true (stage_views_changed_outer_container);
  g_assert_false (stage_views_changed_inner_container);
  g_assert_false (stage_views_changed_test_actor);
  stage_views_changed_outer_container = FALSE;

  /* Show the inner_container again */
  clutter_actor_show (inner_container);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  /* All actors are on both views now */
  is_on_stage_views (outer_container, 2,
                     stage_views->data, stage_views->next->data);
  is_on_stage_views (inner_container, 2,
                     stage_views->data, stage_views->next->data);
  is_on_stage_views (test_actor, 2,
                     stage_views->data, stage_views->next->data);

  /* The signal was emitted for the inner_container and test_actor */
  g_assert_false (stage_views_changed_outer_container);
  g_assert_true (stage_views_changed_inner_container);
  g_assert_true (stage_views_changed_test_actor);

  g_signal_handlers_disconnect_by_func (outer_container, on_stage_views_changed,
                                        stage_views_changed_outer_container_ptr);
  g_signal_handlers_disconnect_by_func (inner_container, on_stage_views_changed,
                                        stage_views_changed_inner_container_ptr);
  g_signal_handlers_disconnect_by_func (test_actor, on_stage_views_changed,
                                        stage_views_changed_test_actor_ptr);
  clutter_actor_destroy (outer_container);
}

static MetaMonitorTestSetup *
create_stage_view_test_setup (MetaBackend *backend)
{
  return meta_create_monitor_test_setup (backend,
                                         &initial_test_case_setup,
                                         MONITOR_TEST_FLAG_NO_STORED);
}

static void
assert_is_stage_view (ClutterStageView *stage_view,
                      int               x,
                      int               y,
                      int               width,
                      int               height)
{
  MtkRectangle layout;

  g_assert_nonnull (stage_view);
  g_assert_true (CLUTTER_IS_STAGE_VIEW (stage_view));

  clutter_stage_view_get_layout (stage_view, &layout);
  g_assert_cmpint (layout.x, ==, x);
  g_assert_cmpint (layout.y, ==, y);
  g_assert_cmpint (layout.width, ==, width);
  g_assert_cmpint (layout.height, ==, height);
}

static void
meta_test_actor_stage_views_hot_plug (void)
{
  MetaBackend *backend = test_backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterActor *actor_1;
  ClutterActor *actor_2;
  GList *stage_views;
  GList *prev_stage_views;
  MonitorTestCaseSetup hotplug_test_case_setup = initial_test_case_setup;
  MetaMonitorTestSetup *test_setup;

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 2);
  assert_is_stage_view (stage_views->data, 0, 0, 1024, 768);
  assert_is_stage_view (stage_views->next->data, 1024, 0, 1024, 768);

  actor_1 = clutter_actor_new ();
  clutter_actor_set_size (actor_1, 100, 100);
  clutter_actor_set_position (actor_1, 100, 100);
  clutter_actor_add_child (stage, actor_1);

  actor_2 = clutter_actor_new ();
  clutter_actor_set_size (actor_2, 100, 100);
  clutter_actor_set_position (actor_2, 1100, 100);
  clutter_actor_add_child (stage, actor_2);

  clutter_actor_show (stage);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (actor_1, 1, stage_views->data);
  is_on_stage_views (actor_2, 1, stage_views->next->data);

  prev_stage_views = g_list_copy_deep (stage_views,
                                       (GCopyFunc) g_object_ref, NULL);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &hotplug_test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  g_assert_true (stage_views != prev_stage_views);
  g_assert_cmpint (g_list_length (stage_views), ==, 2);
  g_assert_true (prev_stage_views->data != stage_views->data);
  g_assert_true (prev_stage_views->next->data != stage_views->next->data);
  assert_is_stage_view (stage_views->data, 0, 0, 1024, 768);
  assert_is_stage_view (stage_views->next->data, 1024, 0, 1024, 768);

  g_list_free_full (prev_stage_views, (GDestroyNotify) g_object_unref);

  is_on_stage_views (actor_1, 0);
  is_on_stage_views (actor_2, 0);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (actor_1, 1, stage_views->data);
  is_on_stage_views (actor_2, 1, stage_views->next->data);

  clutter_actor_destroy (actor_1);
  clutter_actor_destroy (actor_2);
}

static void
meta_test_actor_stage_views_frame_clock (void)
{
  MetaBackend *backend = test_backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterActor *actor_1;
  ClutterActor *actor_2;
  ClutterActor *actor_3;
  GList *stage_views;
  MonitorTestCaseSetup frame_clock_test_setup = initial_test_case_setup;
  MetaMonitorTestSetup *test_setup;
  ClutterFrameClock *frame_clock;

  frame_clock_test_setup.modes[1].width = 1024;
  frame_clock_test_setup.modes[1].height = 768;
  frame_clock_test_setup.modes[1].refresh_rate = 30.0;
  frame_clock_test_setup.n_modes = 2;
  frame_clock_test_setup.outputs[1].modes[0] = 1;
  frame_clock_test_setup.outputs[1].preferred_mode = 1;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &frame_clock_test_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  g_assert_cmpfloat (clutter_stage_view_get_refresh_rate (stage_views->data),
                     ==,
                     60.0);
  g_assert_cmpfloat (clutter_stage_view_get_refresh_rate (stage_views->next->data),
                     ==,
                     30.0);

  actor_1 = clutter_actor_new ();
  clutter_actor_set_size (actor_1, 100, 100);
  clutter_actor_set_position (actor_1, 100, 100);
  clutter_actor_add_child (stage, actor_1);

  actor_2 = clutter_actor_new ();
  clutter_actor_set_size (actor_2, 100, 100);
  clutter_actor_set_position (actor_2, 1100, 100);
  clutter_actor_add_child (stage, actor_2);

  actor_3 = clutter_actor_new ();
  clutter_actor_set_size (actor_3, 100, 100);
  clutter_actor_set_position (actor_3, 1000, 400);
  clutter_actor_add_child (stage, actor_3);

  clutter_actor_show (stage);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (actor_1, 1, stage_views->data);
  is_on_stage_views (actor_2, 1, stage_views->next->data);
  is_on_stage_views (actor_3, 2,
                     stage_views->data,
                     stage_views->next->data);

  frame_clock = clutter_actor_pick_frame_clock (actor_1, NULL);
  g_assert_cmpfloat (clutter_frame_clock_get_refresh_rate (frame_clock),
                     ==,
                     60.0);
  frame_clock = clutter_actor_pick_frame_clock (actor_2, NULL);
  g_assert_cmpfloat (clutter_frame_clock_get_refresh_rate (frame_clock),
                     ==,
                     30.0);
  frame_clock = clutter_actor_pick_frame_clock (actor_3, NULL);
  g_assert_cmpfloat (clutter_frame_clock_get_refresh_rate (frame_clock),
                     ==,
                     60.0);

  clutter_actor_destroy (actor_1);
  clutter_actor_destroy (actor_2);
  clutter_actor_destroy (actor_3);
}

typedef struct _TimelineTest
{
  GMainLoop *main_loop;
  ClutterFrameClock *frame_clock_1;
  ClutterFrameClock *frame_clock_2;
  int phase;

  int frame_counter[2];
} TimelineTest;

static void
on_transition_stopped (ClutterTransition *transition,
                       gboolean           is_finished,
                       TimelineTest      *test)
{
  g_assert_true (is_finished);

  g_assert_cmpint (test->phase, ==, 2);

  test->phase = 3;

  g_main_loop_quit (test->main_loop);
}

static void
on_transition_new_frame (ClutterTransition *transition,
                         int                elapsed_time_ms,
                         TimelineTest      *test)
{
  ClutterTimeline *timeline = CLUTTER_TIMELINE (transition);

  if (test->phase == 1)
    {
      g_assert_true (clutter_timeline_get_frame_clock (timeline) ==
                test->frame_clock_1);
      test->frame_counter[0]++;
    }
  else if (test->phase == 2)
    {
      g_assert_true (clutter_timeline_get_frame_clock (timeline) ==
                test->frame_clock_2);
      test->frame_counter[1]++;
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
on_transition_frame_clock_changed (ClutterTimeline    *timeline,
                                   GParamSpec         *pspec,
                                   TimelineTest       *test)
{
  ClutterFrameClock *frame_clock;

  frame_clock = clutter_timeline_get_frame_clock (timeline);
  g_assert_true (frame_clock == test->frame_clock_2);
  g_assert_cmpint (test->phase, ==, 1);

  test->phase = 2;
}

static void
meta_test_actor_stage_views_timeline (void)
{
  TimelineTest test = { 0 };
  MetaBackend *backend = test_backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MonitorTestCaseSetup frame_clock_test_setup;
  ClutterActor *actor;
  GList *stage_views;
  ClutterStageView *stage_view_1;
  ClutterStageView *stage_view_2;
  MetaMonitorTestSetup *test_setup;
  ClutterTransition *transition;

  frame_clock_test_setup = initial_test_case_setup;
  frame_clock_test_setup.modes[1].width = 1024;
  frame_clock_test_setup.modes[1].height = 768;
  frame_clock_test_setup.modes[1].refresh_rate = 30.0;
  frame_clock_test_setup.n_modes = 2;
  frame_clock_test_setup.outputs[1].modes[0] = 1;
  frame_clock_test_setup.outputs[1].preferred_mode = 1;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &frame_clock_test_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  stage_view_1 = stage_views->data;
  stage_view_2 = stage_views->next->data;
  g_assert_nonnull (stage_view_1);
  g_assert_nonnull (stage_view_2);
  test.frame_clock_1 = clutter_stage_view_get_frame_clock (stage_view_1);
  test.frame_clock_2 = clutter_stage_view_get_frame_clock (stage_view_2);
  g_assert_nonnull (test.frame_clock_1);
  g_assert_nonnull (test.frame_clock_2);

  actor = clutter_actor_new ();
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_position (actor, 800, 100);
  clutter_actor_add_child (stage, actor);

  clutter_actor_show (stage);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (actor, 1, stage_views->data);

  clutter_actor_set_easing_duration (actor, 1000);
  clutter_actor_set_position (actor, 1200, 300);

  transition = clutter_actor_get_transition (actor, "position");
  g_assert_nonnull (transition);
  g_assert_true (clutter_timeline_get_frame_clock (CLUTTER_TIMELINE (transition)) ==
            test.frame_clock_1);

  test.main_loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (transition, "stopped",
                    G_CALLBACK (on_transition_stopped),
                    &test);
  g_signal_connect (transition, "new-frame",
                    G_CALLBACK (on_transition_new_frame),
                    &test);
  g_signal_connect (transition, "notify::frame-clock",
                    G_CALLBACK (on_transition_frame_clock_changed),
                    &test);

  test.phase = 1;

  g_main_loop_run (test.main_loop);

  g_assert_cmpint (test.phase, ==, 3);
  g_assert_cmpint (test.frame_counter[0], >, 0);
  g_assert_cmpint (test.frame_counter[1], >, 0);

  clutter_actor_destroy (actor);
  g_main_loop_unref (test.main_loop);
}

static void
meta_test_actor_stage_views_parent_views_rebuilt (void)
{
  MetaBackend *backend = test_backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MonitorTestCaseSetup frame_clock_test_setup;
  MetaMonitorTestSetup *test_setup;
  ClutterActor *stage, *container, *test_actor;
  GList *stage_views;
  ClutterTimeline *timeline;
  ClutterFrameClock *timeline_frame_clock;
  ClutterFrameClock *view_frame_clock;
  ClutterStageView *old_stage_view;
  ClutterFrameClock *old_frame_clock;

  stage = meta_backend_get_stage (backend);

  frame_clock_test_setup = initial_test_case_setup;
  frame_clock_test_setup.n_outputs = 1;
  frame_clock_test_setup.n_crtcs = 1;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &frame_clock_test_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 1);

  container = clutter_actor_new ();
  clutter_actor_set_size (container, 100, 100);
  clutter_actor_set_position (container, 0, 0);
  clutter_actor_add_child (stage, container);

  test_actor = clutter_actor_new ();
  clutter_actor_set_size (test_actor, 0, 0);
  clutter_actor_add_child (container, test_actor);

  clutter_actor_show (stage);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (test_actor, 0);
  is_on_stage_views (container, 1, stage_views->data);
  is_on_stage_views (stage, 1, stage_views->data);

  timeline = clutter_timeline_new_for_actor (test_actor, 100);
  clutter_timeline_start (timeline);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);
  view_frame_clock = clutter_stage_view_get_frame_clock (stage_views->data);
  g_assert_nonnull (timeline_frame_clock);
  g_assert_nonnull (view_frame_clock);
  g_assert_true (timeline_frame_clock == view_frame_clock);

  /* Keep the stage view alive so it can be used to compare with later. */
  old_stage_view = g_object_ref (stage_views->data);
  old_frame_clock =
    g_object_ref (clutter_stage_view_get_frame_clock (old_stage_view));

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &frame_clock_test_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 1);

  g_assert_true (stage_views->data != old_stage_view);
  view_frame_clock = clutter_stage_view_get_frame_clock (stage_views->data);
  g_assert_nonnull (view_frame_clock);
  g_assert_true (view_frame_clock != old_frame_clock);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);
  g_assert_nonnull (timeline_frame_clock);
  g_assert_true (timeline_frame_clock == view_frame_clock);

  g_object_unref (old_stage_view);
  g_object_unref (old_frame_clock);

  clutter_actor_destroy (test_actor);
  clutter_actor_destroy (container);

  g_object_unref (timeline);
}

static void
meta_test_actor_stage_views_parent_views_changed (void)
{
  MetaBackend *backend = test_backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MonitorTestCaseSetup frame_clock_test_setup;
  MetaMonitorTestSetup *test_setup;
  ClutterActor *stage, *container, *test_actor;
  GList *stage_views;
  ClutterTimeline *timeline;
  ClutterFrameClock *timeline_frame_clock;
  ClutterFrameClock *first_view_frame_clock;
  ClutterFrameClock *second_view_frame_clock;

  stage = meta_backend_get_stage (backend);

  frame_clock_test_setup = initial_test_case_setup;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &frame_clock_test_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 2);

  container = clutter_actor_new ();
  clutter_actor_set_size (container, 100, 100);
  clutter_actor_set_position (container, 0, 0);
  clutter_actor_add_child (stage, container);

  test_actor = clutter_actor_new ();
  clutter_actor_set_size (test_actor, 0, 0);
  clutter_actor_add_child (container, test_actor);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 2);
  clutter_actor_show (stage);
  meta_wait_for_paint (CLUTTER_STAGE (stage));
  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 2);

  is_on_stage_views (test_actor, 0);
  is_on_stage_views (container, 1, stage_views->data);
  is_on_stage_views (stage, 2,
                     stage_views->data,
                     stage_views->next->data);

  timeline = clutter_timeline_new_for_actor (test_actor, 100);
  clutter_timeline_start (timeline);

  first_view_frame_clock =
    clutter_stage_view_get_frame_clock (stage_views->data);
  second_view_frame_clock =
    clutter_stage_view_get_frame_clock (stage_views->next->data);
  g_assert_nonnull (first_view_frame_clock);
  g_assert_nonnull (second_view_frame_clock);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);

  g_assert_nonnull (timeline_frame_clock);
  g_assert_true (timeline_frame_clock == first_view_frame_clock);

  clutter_actor_set_x (container, 1200);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);
  g_assert_nonnull (timeline_frame_clock);
  g_assert_true (timeline_frame_clock == second_view_frame_clock);

  g_object_unref (timeline);
  clutter_actor_destroy (test_actor);
  clutter_actor_destroy (container);
}

static void
meta_test_actor_stage_views_and_frame_clocks_freed (void)
{
  MetaBackend *backend = test_backend;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterActor *actor_1;
  ClutterActor *actor_2;
  GList *stage_views;
  ClutterStageView *first_view;
  ClutterStageView *second_view;
  ClutterTimeline *timeline;
  ClutterFrameClock *timeline_frame_clock;
  ClutterFrameClock *first_view_frame_clock;
  ClutterFrameClock *second_view_frame_clock;
  MonitorTestCaseSetup frame_clock_test_setup;
  MetaMonitorTestSetup *test_setup;

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  first_view = stage_views->data;
  second_view = stage_views->next->data;

  g_object_add_weak_pointer (G_OBJECT (first_view), (gpointer *) &first_view);
  g_object_add_weak_pointer (G_OBJECT (second_view), (gpointer *) &second_view);

  /* Create two actors, one on the first stage view, another one on the
   * second view.
   */
  actor_1 = clutter_actor_new ();
  clutter_actor_set_size (actor_1, 100, 100);
  clutter_actor_set_position (actor_1, 100, 100);
  clutter_actor_add_child (stage, actor_1);

  actor_2 = clutter_actor_new ();
  clutter_actor_set_size (actor_2, 100, 100);
  clutter_actor_set_position (actor_2, 1100, 100);
  clutter_actor_add_child (stage, actor_2);

  clutter_actor_show (stage);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  is_on_stage_views (actor_1, 1, first_view);
  is_on_stage_views (actor_2, 1, second_view);

  /* Now create a timeline for the first actor and make sure its using the
   * frame clock of the first view.
   */
  timeline = clutter_timeline_new_for_actor (actor_1, 100);
  clutter_timeline_start (timeline);

  first_view_frame_clock =
    clutter_stage_view_get_frame_clock (first_view);
  second_view_frame_clock =
    clutter_stage_view_get_frame_clock (second_view);
  g_assert_nonnull (first_view_frame_clock);
  g_assert_nonnull (second_view_frame_clock);

  g_object_add_weak_pointer (G_OBJECT (first_view_frame_clock),
                             (gpointer *) &first_view_frame_clock);
  g_object_add_weak_pointer (G_OBJECT (second_view_frame_clock),
                             (gpointer *) &second_view_frame_clock);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);

  g_assert_nonnull (timeline_frame_clock);
  g_assert_true (timeline_frame_clock == first_view_frame_clock);

  /* Now set the timeline actor to actor_2 and make sure the timeline is
   * using the second frame clock.
   */
  clutter_timeline_set_actor (timeline, actor_2);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);

  g_assert_nonnull (timeline_frame_clock);
  g_assert_true (timeline_frame_clock == second_view_frame_clock);

  /* Trigger a hotplug and remove both monitors, after that the timeline
   * should have no frame clock set and both stage views and their
   * frame clocks should have been freed.
   */
  frame_clock_test_setup = initial_test_case_setup;
  frame_clock_test_setup.n_outputs = 0;
  frame_clock_test_setup.n_crtcs = 0;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &frame_clock_test_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);

  g_object_unref (timeline);
  g_assert_null (timeline_frame_clock);
  g_assert_null (first_view);
  g_assert_null (first_view_frame_clock);
  g_assert_null (second_view);
  g_assert_null (second_view_frame_clock);

  clutter_actor_destroy (actor_1);
  clutter_actor_destroy (actor_2);
}

static void
ensure_view_count (int n_views)
{
  MetaBackend *backend = test_backend;
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MonitorTestCaseSetup test_case_setup;
  MetaMonitorTestSetup *test_setup;
  GList *stage_views;

  test_case_setup = initial_test_case_setup;
  test_case_setup.n_outputs = n_views;
  test_case_setup.n_crtcs = n_views;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpuint (g_list_length (stage_views), ==, n_views);
}

static void
check_test_client_state (MetaTestClient *test_client)
{
  GError *error = NULL;

  if (!meta_test_client_wait (test_client, &error))
    {
      g_error ("Failed to sync test client '%s': %s",
               meta_test_client_get_id (test_client), error->message);
    }
}

static void
meta_test_actor_stage_views_queue_frame_drawn (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaTestClient *x11_test_client;
  MonitorTestCaseSetup hotplug_test_case_setup = initial_test_case_setup;
  MetaMonitorTestSetup *test_setup;
  GError *error = NULL;
  MetaWindow *test_window;
  ClutterActor *window_actor;

  x11_test_client = meta_test_client_new (test_context,
                                          X11_TEST_CLIENT_NAME,
                                          META_WINDOW_CLIENT_TYPE_X11,
                                          &error);
  if (!x11_test_client)
    g_error ("Failed to launch X11 test client: %s", error->message);

  if (!meta_test_client_do (x11_test_client, &error,
                            "create", X11_TEST_CLIENT_WINDOW,
                            NULL))
    g_error ("Failed to create X11 window: %s", error->message);
  if (!meta_test_client_do (x11_test_client, &error,
                            "show", X11_TEST_CLIENT_WINDOW,
                            NULL))
    g_error ("Failed to show the window: %s", error->message);
  check_test_client_state (x11_test_client);

  /* Make sure we have a single output. */
  hotplug_test_case_setup.n_outputs = 1;
  hotplug_test_case_setup.n_crtcs = 1;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &hotplug_test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
  meta_wait_for_paint (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (clutter_actor_peek_stage_views (stage)),
                   ==,
                   1);

  /* Find client window actor and ensure it's on a stage view. */
  test_window = meta_test_client_find_window (x11_test_client,
                                              X11_TEST_CLIENT_WINDOW,
                                              &error);
  if (!test_window)
    g_error ("Failed to find the window: %s", error->message);
  window_actor = CLUTTER_ACTOR (meta_window_actor_from_window (test_window));
  wait_for_window_map (CLUTTER_STAGE (stage), window_actor);
  g_assert_nonnull (clutter_actor_peek_stage_views (window_actor));

  /* Queue an X11 _NET_WM_FRAME_DRAWN event; this will find the frame clock via
   * the actor stage view list.
   */
  meta_window_actor_queue_frame_drawn (META_WINDOW_ACTOR (window_actor), TRUE);

  /* Hotplug to rebuild the views, will clear the window actor view list. */
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &hotplug_test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
  g_assert_null (clutter_actor_peek_stage_views (window_actor));

  /* Queue an X11 _NET_WM_FRAME_DRAWN event; this will find the frame clock via
   * the stage's frame clock, as the actor hasn't been been through relayout.
   */
  meta_window_actor_queue_frame_drawn (META_WINDOW_ACTOR (window_actor), TRUE);

  /* Hotplug again to re-rebuild the views, will again clear the window actor
   * view list, which will be a no-op. */
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &hotplug_test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  /* Make sure we're not using some old frame clock when queuing another
   * _NET_WM_FRAME_DRAWN event. */
  meta_window_actor_queue_frame_drawn (META_WINDOW_ACTOR (window_actor), TRUE);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  if (!meta_test_client_quit (x11_test_client, &error))
    g_error ("Failed to quit X11 test client: %s", error->message);
  meta_test_client_destroy (x11_test_client);
}

static void
meta_test_timeline_actor_destroyed (void)
{
  MetaBackend *backend = test_backend;
  ClutterActor *stage;
  GList *stage_views;
  ClutterActor *persistent_actor;
  ClutterActor *actor;
  ClutterTimeline *timeline;
  gboolean did_stage_views_changed = FALSE;

  ensure_view_count (0);

  stage = meta_backend_get_stage (backend);
  clutter_actor_show (stage);

  persistent_actor = clutter_actor_new ();
  clutter_actor_add_child (stage, persistent_actor);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_null (stage_views);
  stage_views = clutter_actor_peek_stage_views (stage);
  g_assert_null (stage_views);
  g_assert_null (clutter_actor_pick_frame_clock (stage, NULL));

  actor = clutter_actor_new ();
  clutter_actor_add_child (stage, actor);
  g_assert_null (clutter_actor_pick_frame_clock (actor, NULL));

  timeline = clutter_timeline_new_for_actor (actor, 100);
  clutter_timeline_start (timeline);

  g_signal_connect (stage, "stage-views-changed",
                    G_CALLBACK (on_stage_views_changed),
                    &did_stage_views_changed);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 0);

  clutter_actor_destroy (actor);
  g_object_unref (timeline);

  ensure_view_count (1);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 1);

  g_assert_true (did_stage_views_changed);
  clutter_actor_queue_redraw (persistent_actor);
  clutter_stage_schedule_update (CLUTTER_STAGE (stage));
  meta_wait_for_paint (CLUTTER_STAGE (stage));
  g_assert_true (did_stage_views_changed);

  g_signal_handlers_disconnect_by_func (stage, on_stage_views_changed,
                                        &did_stage_views_changed);

  clutter_actor_destroy (persistent_actor);
}

static void
meta_test_timeline_actor_tree_clear (void)
{
  ClutterActor *stage;
  ClutterActor *container1;
  ClutterActor *container2;
  g_autoptr (ClutterActor) floating = NULL;
  g_autoptr (ClutterTimeline) timeline = NULL;
  GList *stage_views;

  stage = meta_backend_get_stage (meta_context_get_backend (test_context));

  ensure_view_count (1);

  container1 = clutter_actor_new ();
  clutter_actor_set_size (container1, 100, 100);
  clutter_actor_add_child (stage, container1);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  container2 = clutter_actor_new ();
  clutter_actor_set_size (container2, 100, 100);
  clutter_actor_add_child (stage, container2);

  floating = g_object_ref_sink (clutter_actor_new ());
  clutter_actor_set_size (floating, 100, 100);

  clutter_actor_add_child (container2, floating);
  timeline = clutter_timeline_new_for_actor (floating, 100);
  clutter_actor_remove_child (container2, floating);

  clutter_actor_add_child (container1, floating);

  ensure_view_count (1);

  is_on_stage_views (container1, 0);
  is_on_stage_views (container2, 0);
  is_on_stage_views (floating, 0);

  meta_wait_for_paint (CLUTTER_STAGE (stage));

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  is_on_stage_views (container1, 1, stage_views->data);
  is_on_stage_views (container2, 1, stage_views->data);
  is_on_stage_views (floating, 1, stage_views->data);

  clutter_actor_destroy (floating);
  clutter_actor_destroy (container1);
  clutter_actor_destroy (container2);
}

static void
assert_view_pixel (CoglFramebuffer *framebuffer,
                   int              x,
                   int              y,
                   uint32_t         expected_rgba)
{
  uint8_t pixel[4];
  uint32_t rgba;

  cogl_framebuffer_read_pixels (framebuffer, x, y, 1, 1,
                                COGL_PIXEL_FORMAT_RGBA_8888, pixel);
  rgba = (pixel[0] << 24) | (pixel[1] << 16) | (pixel[2] << 8) | pixel[3];
  g_assert_cmphex (rgba, ==, expected_rgba);
}

static void
meta_test_stage_views_fractional_position (void)
{
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (test_backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  ClutterActor *stage = meta_backend_get_stage (test_backend);
  MonitorTestCaseSetup frac_test_case_setup = {
    .modes = {
      {
        .width = 1024,
        .height = 767,
        .refresh_rate = 60.000495910644531
      },
      {
        .width = 1200,
        .height = 900,
        .refresh_rate = 60.000495910644531
      }
    },
    .n_modes = 2,
    .outputs = {
       {
        .crtc = 0,
        .modes = { 0 },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 0 },
        .n_possible_crtcs = 1,
        .width_mm = 222,
        .height_mm = 125,
        .serial = "0x123456a",
      },
      {
        .crtc = 1,
        .modes = { 1 },
        .n_modes = 1,
        .preferred_mode = 1,
        .possible_crtcs = { 1 },
        .n_possible_crtcs = 1,
        .width_mm = 220,
        .height_mm = 124,
        .serial = "0x123456b",
      }
    },
    .n_outputs = 2,
    .crtcs = {
      {
        .current_mode = 0
      },
      {
        .current_mode = 1
      }
    },
    .n_crtcs = 2
  };
  MetaMonitorTestSetup *test_setup;
  ClutterBackend *clutter_backend = clutter_get_default_backend ();
  CoglContext *cogl_context =
    clutter_backend_get_cogl_context (clutter_backend);
  GList *stage_views;
  ClutterStageView *view;
  CoglFramebuffer *framebuffer;
  ClutterActor *base_actor;
  ClutterActor *top_actor;
  ClutterActor *pattern_actor;
  ClutterContent *pattern_content;
  CoglTexture *pattern_texture;
  g_autofree uint8_t *pattern_data = NULL;
  g_autoptr (GError) error = NULL;
  int i;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &frac_test_case_setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context,
                                  "stage-views-fractional-position.xml");
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  /* The scaled view's device pixel offset is fractional: 767 * 1.5 = 1150.5 */
  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 2);
  view = stage_views->next->data;
  assert_is_stage_view (view, 0, 767, 800, 600);
  g_assert_cmpfloat (clutter_stage_view_get_scale (view), ==, 1.5f);

  base_actor = clutter_actor_new ();
  clutter_actor_set_background_color (base_actor,
                                      &COGL_COLOR_INIT (0, 255, 0, 255));
  clutter_actor_set_position (base_actor, 0, 767);
  clutter_actor_set_size (base_actor, 800, 600);
  clutter_actor_add_child (stage, base_actor);

  top_actor = clutter_actor_new ();
  clutter_actor_set_background_color (top_actor,
                                      &COGL_COLOR_INIT (255, 255, 255, 255));
  clutter_actor_set_position (top_actor, 0, 767);
  clutter_actor_set_size (top_actor, 800, 300);
  clutter_actor_add_child (stage, top_actor);

  /* 6x300 texture of alternating black/white rows, shown 1:1; under linear
   * sampling the rows stay pure only if the transform is pixel-exact. */
  pattern_data = g_malloc (6 * 300 * 4);
  for (i = 0; i < 6 * 300; i++)
    {
      uint8_t value = (i / 6) % 2 == 0 ? 0xff : 0x00;

      pattern_data[i * 4 + 0] = value;
      pattern_data[i * 4 + 1] = value;
      pattern_data[i * 4 + 2] = value;
      pattern_data[i * 4 + 3] = 0xff;
    }
  pattern_texture =
    COGL_TEXTURE (cogl_texture_2d_new_from_data (cogl_context, 6, 300,
                                                 COGL_PIXEL_FORMAT_RGBA_8888,
                                                 6 * 4, pattern_data,
                                                 &error));
  g_assert_no_error (error);
  pattern_content =
    clutter_texture_content_new_from_texture (pattern_texture, NULL);
  g_object_unref (pattern_texture);

  pattern_actor = clutter_actor_new ();
  clutter_actor_set_content (pattern_actor, pattern_content);
  clutter_actor_set_content_scaling_filters (pattern_actor,
                                             CLUTTER_SCALING_FILTER_LINEAR,
                                             CLUTTER_SCALING_FILTER_LINEAR);
  g_object_unref (pattern_content);
  clutter_actor_set_position (pattern_actor, 0, 767);
  clutter_actor_set_size (pattern_actor, 4, 200);
  clutter_actor_add_child (stage, pattern_actor);

  clutter_actor_queue_redraw (stage);
  meta_wait_for_paint (CLUTTER_STAGE (stage));

  framebuffer = clutter_stage_view_get_framebuffer (view);

  /* Solid actors must land on exact device rows: white/green boundary at
   * row 450, green covering the view's first and last rows. */
  assert_view_pixel (framebuffer, 600, 0, 0xffffffff);
  assert_view_pixel (framebuffer, 600, 449, 0xffffffff);
  assert_view_pixel (framebuffer, 600, 450, 0x00ff00ff);
  assert_view_pixel (framebuffer, 600, 899, 0x00ff00ff);

  /* Pattern samples 1:1: alternating rows stay pure black and white. */
  assert_view_pixel (framebuffer, 3, 100, 0xffffffff);
  assert_view_pixel (framebuffer, 3, 101, 0x000000ff);
  assert_view_pixel (framebuffer, 3, 102, 0xffffffff);
  assert_view_pixel (framebuffer, 3, 103, 0x000000ff);

  /* Same content via paint_to_framebuffer_clipped into a y-flipped offscreen
   * must land on the same device rows. */
  {
    g_autoptr (CoglTexture) capture_texture = NULL;
    g_autoptr (CoglOffscreen) capture = NULL;
    CoglFramebuffer *capture_fb;

    capture_texture = cogl_texture_2d_new_with_size (cogl_context, 1200, 900);
    g_assert_nonnull (capture_texture);
    capture = cogl_offscreen_new_with_texture (capture_texture);
    capture_fb = COGL_FRAMEBUFFER (capture);
    g_assert_true (cogl_framebuffer_allocate (capture_fb, &error));
    g_assert_no_error (error);

    clutter_stage_paint_to_framebuffer (CLUTTER_STAGE (stage),
                                        capture_fb,
                                        &(MtkRectangle) { 0, 767, 800, 600 },
                                        1.5,
                                        NULL,
                                        CLUTTER_PAINT_FLAG_CLEAR);

    assert_view_pixel (capture_fb, 600, 0, 0xffffffff);
    assert_view_pixel (capture_fb, 600, 449, 0xffffffff);
    assert_view_pixel (capture_fb, 600, 450, 0x00ff00ff);
    assert_view_pixel (capture_fb, 600, 899, 0x00ff00ff);
    assert_view_pixel (capture_fb, 3, 100, 0xffffffff);
    assert_view_pixel (capture_fb, 3, 101, 0x000000ff);
    assert_view_pixel (capture_fb, 3, 102, 0xffffffff);
    assert_view_pixel (capture_fb, 3, 103, 0x000000ff);
  }

  clutter_actor_destroy (pattern_actor);
  clutter_actor_destroy (top_actor);
  clutter_actor_destroy (base_actor);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &initial_test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
}

static void
on_before_tests (MetaContext *context)
{
  test_backend = meta_context_get_backend (context);
}

static void
init_tests (void)
{
  meta_init_monitor_test_setup (create_stage_view_test_setup);

  g_test_add_func ("/stage-view/stage-views-exist",
                   meta_test_stage_views_exist);
  g_test_add_func ("/stage-views/actor-stage-views",
                   meta_test_actor_stage_views);
  g_test_add_func ("/stage-views/actor-stage-views-relayout",
                   meta_test_actor_stage_views_relayout);
  g_test_add_func ("/stage-views/actor-stage-views-reparent",
                   meta_test_actor_stage_views_reparent);
  g_test_add_func ("/stage-views/actor-stage-views-hide-parent",
                   meta_test_actor_stage_views_hide_parent);
  g_test_add_func ("/stage-views/actor-stage-views-hot-plug",
                   meta_test_actor_stage_views_hot_plug);
  g_test_add_func ("/stage-views/actor-stage-views-frame-clock",
                   meta_test_actor_stage_views_frame_clock);
  g_test_add_func ("/stage-views/actor-stage-views-timeline",
                   meta_test_actor_stage_views_timeline);
  g_test_add_func ("/stage-views/actor-stage-views-parent-rebuilt",
                   meta_test_actor_stage_views_parent_views_rebuilt);
  g_test_add_func ("/stage-views/actor-stage-views-parent-changed",
                   meta_test_actor_stage_views_parent_views_changed);
  g_test_add_func ("/stage-views/actor-stage-views-and-frame-clocks-freed",
                   meta_test_actor_stage_views_and_frame_clocks_freed);
  g_test_add_func ("/stage-views/actor-stage-viwes-queue-frame-drawn",
                   meta_test_actor_stage_views_queue_frame_drawn);
  g_test_add_func ("/stage-views/timeline/actor-destroyed",
                   meta_test_timeline_actor_destroyed);
  g_test_add_func ("/stage-views/timeline/tree-clear",
                   meta_test_timeline_actor_tree_clear);
  g_test_add_func ("/stage-views/fractional-position",
                   meta_test_stage_views_fractional_position);
}

int
main (int argc, char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      (META_CONTEXT_TEST_FLAG_TEST_CLIENT |
                                       META_CONTEXT_TEST_FLAG_NO_ANIMATIONS));
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
