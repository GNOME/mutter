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
#include "compositor/meta-plugin-manager.h"
#include "core/main-private.h"
#include "meta/main.h"
#include "tests/meta-backend-test.h"
#include "tests/monitor-test-utils.h"
#include "tests/test-utils.h"

#define FRAME_WARNING "Frame has assigned frame counter but no frame drawn time"

static gboolean
run_tests (gpointer data)
{
  MetaBackend *backend = meta_get_backend ();
  MetaSettings *settings = meta_backend_get_settings (backend);
  gboolean ret;

  g_test_log_set_fatal_handler (NULL, NULL);

  meta_settings_override_experimental_features (settings);

  meta_settings_enable_experimental_feature (
    settings,
    META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER);

  ret = g_test_run ();

  meta_quit (ret != 0);

  return G_SOURCE_REMOVE;
}

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
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage;
  GList *stage_views;

  stage = meta_backend_get_stage (backend);
  g_assert_cmpint (clutter_actor_get_width (stage), ==, 1024 * 2);
  g_assert_cmpint (clutter_actor_get_height (stage), ==, 768);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 2);
}

static void
on_after_paint (ClutterStage     *stage,
                ClutterStageView *view,
                gboolean         *was_painted)
{
  *was_painted = TRUE;
}

static void
wait_for_paint (ClutterActor *stage)
{
  gboolean was_painted = FALSE;
  gulong was_painted_id;

  was_painted_id = g_signal_connect (CLUTTER_STAGE (stage),
                                     "after-paint",
                                     G_CALLBACK (on_after_paint),
                                     &was_painted);

  while (!was_painted)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (stage, was_painted_id);
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
  g_assert (g_list_length (stage_views) == n_views);
}

static void
meta_test_actor_stage_views (void)
{
  MetaBackend *backend = meta_get_backend ();
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

  wait_for_paint (stage);

  is_on_stage_views (container, 1, stage_views->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for the initial change */
  g_assert (stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Move the container to the second stage view */
  clutter_actor_set_x (container, 1040);

  wait_for_paint (stage);

  is_on_stage_views (container, 1, stage_views->next->data);
  is_on_stage_views (test_actor, 1, stage_views->next->data);

  /* The signal was emitted again */
  g_assert (stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Move the container so it's on both stage views while the test_actor
   * is only on the first one.
   */
  clutter_actor_set_x (container, 940);

  wait_for_paint (stage);

  is_on_stage_views (container, 2, stage_views->data, stage_views->next->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted again */
  g_assert (stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);

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
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage = meta_backend_get_stage (backend);

  clutter_stage_clear_stage_views (CLUTTER_STAGE (stage));
}

static void
meta_test_actor_stage_views_relayout (void)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage, *actor;
  ClutterTransition *transition;
  GMainLoop *main_loop;

  stage = meta_backend_get_stage (backend);

  actor = clutter_actor_new ();
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_easing_duration (actor, 100);
  clutter_actor_add_child (stage, actor);

  clutter_actor_show (stage);

  wait_for_paint (stage);
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
  MetaBackend *backend = meta_get_backend ();
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

  wait_for_paint (stage);

  is_on_stage_views (container, 2, stage_views->data, stage_views->next->data);
  is_on_stage_views (test_actor, 2, stage_views->data, stage_views->next->data);

  /* The signal was emitted for both actors */
  g_assert (stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Remove the test_actor from the scene-graph */
  g_object_ref (test_actor);
  clutter_actor_remove_child (container, test_actor);

  /* While the test_actor is not on stage, it must be on no stage views */
  is_on_stage_views (test_actor, 0);

  /* When the test_actor left the stage, the signal was emitted */
  g_assert (!stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_test_actor = FALSE;

  /* Add the test_actor again as a child of the stage */
  clutter_actor_add_child (stage, test_actor);
  g_object_unref (test_actor);

  wait_for_paint (stage);

  /* The container is still on both stage views... */
  is_on_stage_views (container, 2, stage_views->data, stage_views->next->data);

  /* ...while the test_actor is only on the first one now */
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for the test_actor again */
  g_assert (!stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_test_actor = FALSE;

  /* Move the container out of the stage... */
  clutter_actor_set_y (container, 2000);
  g_object_ref (test_actor);
  clutter_actor_remove_child (stage, test_actor);

  /* When the test_actor left the stage, the signal was emitted */
  g_assert (!stage_views_changed_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_test_actor = FALSE;

  /* ...and reparent the test_actor to the container again */
  clutter_actor_add_child (container, test_actor);
  g_object_unref (test_actor);

  wait_for_paint (stage);

  /* Now both actors are on no stage views */
  is_on_stage_views (container, 0);
  is_on_stage_views (test_actor, 0);

  /* The signal was emitted only for the container, the test_actor already
   * has no stage-views.
   */
  g_assert (stage_views_changed_container);
  g_assert (!stage_views_changed_test_actor);

  g_signal_handlers_disconnect_by_func (container, on_stage_views_changed,
                                        stage_views_changed_container_ptr);
  g_signal_handlers_disconnect_by_func (test_actor, on_stage_views_changed,
                                        stage_views_changed_test_actor_ptr);
  clutter_actor_destroy (container);
}

static void
meta_test_actor_stage_views_hide_parent (void)
{
  MetaBackend *backend = meta_get_backend ();
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

  wait_for_paint (stage);

  /* The containers and the test_actor are on all on the first view */
  is_on_stage_views (outer_container, 1, stage_views->data);
  is_on_stage_views (inner_container, 1, stage_views->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for all three */
  g_assert (stage_views_changed_outer_container);
  g_assert (stage_views_changed_inner_container);
  g_assert (stage_views_changed_test_actor);
  stage_views_changed_outer_container = FALSE;
  stage_views_changed_inner_container = FALSE;
  stage_views_changed_test_actor = FALSE;

  /* Hide the inner_container */
  clutter_actor_hide (inner_container);

  /* Move the outer_container so it's still on the first view */
  clutter_actor_set_x (outer_container, 1023);

  wait_for_paint (stage);

  /* The outer_container is still expanded so it should be on both views */
  is_on_stage_views (outer_container, 2,
                     stage_views->data, stage_views->next->data);

  /* The inner_container and test_actor aren't updated because they're hidden */
  is_on_stage_views (inner_container, 1, stage_views->data);
  is_on_stage_views (test_actor, 1, stage_views->data);

  /* The signal was emitted for the outer_container */
  g_assert (stage_views_changed_outer_container);
  g_assert (!stage_views_changed_inner_container);
  g_assert (!stage_views_changed_test_actor);
  stage_views_changed_outer_container = FALSE;

  /* Show the inner_container again */
  clutter_actor_show (inner_container);

  wait_for_paint (stage);

  /* All actors are on both views now */
  is_on_stage_views (outer_container, 2,
                     stage_views->data, stage_views->next->data);
  is_on_stage_views (inner_container, 2,
                     stage_views->data, stage_views->next->data);
  is_on_stage_views (test_actor, 2,
                     stage_views->data, stage_views->next->data);

  /* The signal was emitted for the inner_container and test_actor */
  g_assert (!stage_views_changed_outer_container);
  g_assert (stage_views_changed_inner_container);
  g_assert (stage_views_changed_test_actor);

  g_signal_handlers_disconnect_by_func (outer_container, on_stage_views_changed,
                                        stage_views_changed_outer_container_ptr);
  g_signal_handlers_disconnect_by_func (inner_container, on_stage_views_changed,
                                        stage_views_changed_inner_container_ptr);
  g_signal_handlers_disconnect_by_func (test_actor, on_stage_views_changed,
                                        stage_views_changed_test_actor_ptr);
  clutter_actor_destroy (outer_container);
}

static MetaMonitorTestSetup *
create_stage_view_test_setup (void)
{
  return create_monitor_test_setup (&initial_test_case_setup,
                                    MONITOR_TEST_FLAG_NO_STORED);
}

static void
assert_is_stage_view (ClutterStageView *stage_view,
                      int               x,
                      int               y,
                      int               width,
                      int               height)
{
  cairo_rectangle_int_t layout;

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
  MetaBackend *backend = meta_get_backend ();
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

  wait_for_paint (stage);

  is_on_stage_views (actor_1, 1, stage_views->data);
  is_on_stage_views (actor_2, 1, stage_views->next->data);

  prev_stage_views = g_list_copy_deep (stage_views,
                                       (GCopyFunc) g_object_ref, NULL);

  test_setup = create_monitor_test_setup (&hotplug_test_case_setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));

  g_assert (stage_views != prev_stage_views);
  g_assert_cmpint (g_list_length (stage_views), ==, 2);
  g_assert (prev_stage_views->data != stage_views->data);
  g_assert (prev_stage_views->next->data != stage_views->next->data);
  assert_is_stage_view (stage_views->data, 0, 0, 1024, 768);
  assert_is_stage_view (stage_views->next->data, 1024, 0, 1024, 768);

  g_list_free_full (prev_stage_views, (GDestroyNotify) g_object_unref);

  is_on_stage_views (actor_1, 0);
  is_on_stage_views (actor_2, 0);

  wait_for_paint (stage);

  is_on_stage_views (actor_1, 1, stage_views->data);
  is_on_stage_views (actor_2, 1, stage_views->next->data);

  clutter_actor_destroy (actor_1);
  clutter_actor_destroy (actor_2);
}

static void
meta_test_actor_stage_views_frame_clock (void)
{
  MetaBackend *backend = meta_get_backend ();
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
  test_setup = create_monitor_test_setup (&frame_clock_test_setup,
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

  wait_for_paint (stage);

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
      g_assert (clutter_timeline_get_frame_clock (timeline) ==
                test->frame_clock_1);
      test->frame_counter[0]++;
    }
  else if (test->phase == 2)
    {
      g_assert (clutter_timeline_get_frame_clock (timeline) ==
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
  g_assert (frame_clock == test->frame_clock_2);
  g_assert_cmpint (test->phase, ==, 1);

  test->phase = 2;
}

static void
meta_test_actor_stage_views_timeline (void)
{
  TimelineTest test = { 0 };
  MetaBackend *backend = meta_get_backend ();
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
  test_setup = create_monitor_test_setup (&frame_clock_test_setup,
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

  wait_for_paint (stage);

  is_on_stage_views (actor, 1, stage_views->data);

  clutter_actor_set_easing_duration (actor, 1000);
  clutter_actor_set_position (actor, 1200, 300);

  transition = clutter_actor_get_transition (actor, "position");
  g_assert_nonnull (transition);
  g_assert (clutter_timeline_get_frame_clock (CLUTTER_TIMELINE (transition)) ==
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
  MetaBackend *backend = meta_get_backend ();
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
  test_setup = create_monitor_test_setup (&frame_clock_test_setup,
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
  wait_for_paint (stage);

  is_on_stage_views (test_actor, 0);
  is_on_stage_views (container, 1, stage_views->data);
  is_on_stage_views (stage, 1, stage_views->data);

  timeline = clutter_timeline_new_for_actor (test_actor, 100);
  clutter_timeline_start (timeline);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);
  view_frame_clock = clutter_stage_view_get_frame_clock (stage_views->data);
  g_assert_nonnull (timeline_frame_clock);
  g_assert_nonnull (view_frame_clock);
  g_assert (timeline_frame_clock == view_frame_clock);

  /* Keep the stage view alive so it can be used to compare with later. */
  old_stage_view = g_object_ref (stage_views->data);
  old_frame_clock =
    g_object_ref (clutter_stage_view_get_frame_clock (old_stage_view));

  test_setup = create_monitor_test_setup (&frame_clock_test_setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
  wait_for_paint (stage);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 1);

  g_assert (stage_views->data != old_stage_view);
  view_frame_clock = clutter_stage_view_get_frame_clock (stage_views->data);
  g_assert_nonnull (view_frame_clock);
  g_assert (view_frame_clock != old_frame_clock);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);
  g_assert_nonnull (timeline_frame_clock);
  g_assert (timeline_frame_clock == view_frame_clock);

  g_object_unref (old_stage_view);
  g_object_unref (old_frame_clock);

  clutter_actor_destroy (test_actor);
  clutter_actor_destroy (container);
}

static void
meta_test_actor_stage_views_parent_views_changed (void)
{
  MetaBackend *backend = meta_get_backend ();
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
  test_setup = create_monitor_test_setup (&frame_clock_test_setup,
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
  wait_for_paint (stage);
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
  g_assert (timeline_frame_clock == first_view_frame_clock);

  clutter_actor_set_x (container, 1200);
  wait_for_paint (stage);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);
  g_assert_nonnull (timeline_frame_clock);
  g_assert (timeline_frame_clock == second_view_frame_clock);

  clutter_actor_destroy (test_actor);
  clutter_actor_destroy (container);
}

static void
meta_test_actor_stage_views_and_frame_clocks_freed (void)
{
  MetaBackend *backend = meta_get_backend ();
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

  wait_for_paint (stage);

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
  g_assert (timeline_frame_clock == first_view_frame_clock);

  /* Now set the timeline actor to actor_2 and make sure the timeline is
   * using the second frame clock.
   */
  clutter_timeline_set_actor (timeline, actor_2);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);

  g_assert_nonnull (timeline_frame_clock);
  g_assert (timeline_frame_clock == second_view_frame_clock);

  /* Trigger a hotplug and remove both monitors, after that the timeline
   * should have no frame clock set and both stage views and their
   * frame clocks should have been freed.
   */
  frame_clock_test_setup = initial_test_case_setup;
  frame_clock_test_setup.n_outputs = 0;
  frame_clock_test_setup.n_crtcs = 0;
  test_setup = create_monitor_test_setup (&frame_clock_test_setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  timeline_frame_clock = clutter_timeline_get_frame_clock (timeline);

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
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MonitorTestCaseSetup test_case_setup;
  MetaMonitorTestSetup *test_setup;

  test_case_setup = initial_test_case_setup;
  test_case_setup.n_outputs = n_views;
  test_case_setup.n_crtcs = n_views;
  test_setup = create_monitor_test_setup (&test_case_setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
}

static void
meta_test_timeline_actor_destroyed (void)
{
  MetaBackend *backend = meta_get_backend ();
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

  clutter_actor_destroy (actor);
  g_object_unref (timeline);

  ensure_view_count (1);

  stage_views = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage));
  g_assert_cmpint (g_list_length (stage_views), ==, 1);

  g_assert_false (did_stage_views_changed);
  clutter_actor_queue_redraw (persistent_actor);
  clutter_stage_schedule_update (CLUTTER_STAGE (stage));
  wait_for_paint (stage);
  g_assert_true (did_stage_views_changed);

  g_signal_handlers_disconnect_by_func (stage, on_stage_views_changed,
                                        &did_stage_views_changed);

  clutter_actor_destroy (persistent_actor);
}

static void
init_tests (int argc, char **argv)
{
  meta_monitor_manager_test_init_test_setup (create_stage_view_test_setup);

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
  g_test_add_func ("/stage-views/timeline/actor-destroyed",
                   meta_test_timeline_actor_destroyed);
}

int
main (int argc, char *argv[])
{
  test_init (&argc, &argv);
  init_tests (argc, argv);

  meta_plugin_manager_load (test_get_plugin_name ());

  meta_override_compositor_configuration (META_COMPOSITOR_TYPE_WAYLAND,
                                          META_TYPE_BACKEND_TEST,
                                          NULL);

  meta_init ();
  meta_register_with_session ();

  g_idle_add (run_tests, NULL);

  return meta_run ();
}
