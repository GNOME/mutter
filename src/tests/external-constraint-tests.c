/*
 * Copyright (C) 2025 Red Hat, Inc.
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

#include "backends/meta-virtual-monitor.h"
#include "compositor/meta-window-actor-private.h"
#include "core/window-private.h"
#include "meta/display.h"
#include "meta/meta-external-constraint.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-shell.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"

#define TEST_CLIENT_TITLE "external-constraint-test-window"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;

/* Global state for constraint testing */
static struct {
  gboolean enabled;
  MtkRectangle target_rect;
  gboolean was_called;
  MetaWindow *expected_window;
} constraint_state;

/* Define our test constraint type */
#define TEST_TYPE_CONSTRAINT (test_constraint_get_type ())
G_DECLARE_FINAL_TYPE (TestConstraint, test_constraint,
                      TEST, CONSTRAINT, GObject)

struct _TestConstraint
{
  GObject parent_instance;
};

static void test_constraint_external_constraint_iface_init (MetaExternalConstraintInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestConstraint, test_constraint, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (META_TYPE_EXTERNAL_CONSTRAINT,
                                                test_constraint_external_constraint_iface_init))

/* Our test constraint function */
static gboolean
test_constraint_constrain (MetaExternalConstraint     *constraint,
                           MetaWindow                 *window,
                           MetaExternalConstraintInfo *info)
{
  if (!constraint_state.enabled)
    return FALSE;

  /* Apply our test constraint */
  *info->new_rect = constraint_state.target_rect;
  constraint_state.was_called = TRUE;
  constraint_state.expected_window = window;

  return TRUE;  /* Skip other constraints */
}

static void
test_constraint_external_constraint_iface_init (MetaExternalConstraintInterface *iface)
{
  iface->constrain = test_constraint_constrain;
}

static void
test_constraint_class_init (TestConstraintClass *klass)
{
}

static void
test_constraint_init (TestConstraint *constraint)
{
}

static void
on_effects_completed (MetaWindowActor *window_actor,
                      gboolean        *done)
{
  *done = TRUE;
}

static void
wait_for_window_added (MetaWindow *window)
{
  MetaWindowActor *window_actor;
  gboolean done = FALSE;
  gulong handler_id;

  window_actor = meta_window_actor_from_window (window);
  handler_id = g_signal_connect (window_actor, "effects-completed",
                                 G_CALLBACK (on_effects_completed), &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (window_actor, handler_id);
}

/* Test: Basic external constraint on window creation */
static void
test_external_constraint_basic (void)
{
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;
  MetaWindow *window = NULL;
  MtkRectangle frame_rect;

  /* Configure constraint */
  constraint_state.enabled = TRUE;
  constraint_state.target_rect = (MtkRectangle) { 100, 150, 300, 200 };
  constraint_state.was_called = FALSE;

  /* Create test window */
  test_client = meta_test_client_new (test_context,
                                      "external-constraint-test-client",
                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  g_assert_nonnull (test_client);
  g_assert_no_error (error);

  meta_test_client_run (test_client,
                        "create " TEST_CLIENT_TITLE " csd\n"
                        "show " TEST_CLIENT_TITLE "\n");

  while (!(window = meta_test_client_find_window (test_client,
                                                  TEST_CLIENT_TITLE,
                                                  NULL)))
    g_main_context_iteration (NULL, TRUE);

  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *) &window);
  wait_for_window_added (window);

  /* Verify constraint was called */
  g_assert_true (constraint_state.was_called);
  g_assert_true (constraint_state.expected_window == window);

  /* Verify window position matches constraint */
  meta_window_get_frame_rect (window, &frame_rect);

  g_assert_cmpint (frame_rect.x, ==, constraint_state.target_rect.x);
  g_assert_cmpint (frame_rect.y, ==, constraint_state.target_rect.y);
  g_assert_cmpint (frame_rect.width, ==, constraint_state.target_rect.width);
  g_assert_cmpint (frame_rect.height, ==, constraint_state.target_rect.height);

  /* Cleanup */
  meta_test_client_destroy (test_client);
  while (window)
    g_main_context_iteration (NULL, TRUE);

  constraint_state.enabled = FALSE;
  constraint_state.expected_window = NULL;
}

/* Test: External constraint prevents window move */
static void
test_external_constraint_move (void)
{
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;
  MetaWindow *window = NULL;
  MtkRectangle frame_rect;

  /* Configure constraint to fixed position */
  constraint_state.enabled = TRUE;
  constraint_state.target_rect = (MtkRectangle) { 200, 100, 400, 300 };
  constraint_state.was_called = FALSE;

  /* Create window */
  test_client = meta_test_client_new (test_context,
                                      "external-constraint-test-client",
                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  g_assert_nonnull (test_client);
  g_assert_no_error (error);

  meta_test_client_run (test_client,
                        "create " TEST_CLIENT_TITLE " csd\n"
                        "show " TEST_CLIENT_TITLE "\n");

  while (!(window = meta_test_client_find_window (test_client,
                                                  TEST_CLIENT_TITLE,
                                                  NULL)))
    g_main_context_iteration (NULL, TRUE);

  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *) &window);
  wait_for_window_added (window);

  /* Verify initial position */
  meta_window_get_frame_rect (window, &frame_rect);

  /* Try to move window */
  constraint_state.was_called = FALSE;

  meta_window_move_frame (window, TRUE, 500, 500);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  /* Verify constraint prevented the move */
  g_assert_true (constraint_state.was_called);

  meta_window_get_frame_rect (window, &frame_rect);

  g_assert_cmpint (frame_rect.x, ==, constraint_state.target_rect.x);
  g_assert_cmpint (frame_rect.y, ==, constraint_state.target_rect.y);

  /* Cleanup */
  meta_test_client_destroy (test_client);
  while (window)
    g_main_context_iteration (NULL, TRUE);

  constraint_state.enabled = FALSE;
  constraint_state.expected_window = NULL;
}

/* Test: External constraint limits maximized window size */
static void
test_external_constraint_maximized (void)
{
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;
  MetaWindow *window = NULL;
  MtkRectangle frame_rect;

  /* Configure constraint to a smaller size than monitor */
  constraint_state.enabled = TRUE;
  constraint_state.target_rect = (MtkRectangle) { 50, 50, 400, 300 };
  constraint_state.was_called = FALSE;

  /* Create window */
  test_client = meta_test_client_new (test_context,
                                      "external-constraint-test-client",
                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  g_assert_nonnull (test_client);
  g_assert_no_error (error);

  meta_test_client_run (test_client,
                        "create " TEST_CLIENT_TITLE " csd\n"
                        "show " TEST_CLIENT_TITLE "\n");

  while (!(window = meta_test_client_find_window (test_client,
                                                   TEST_CLIENT_TITLE,
                                                   NULL)))
    g_main_context_iteration (NULL, TRUE);

  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *) &window);
  wait_for_window_added (window);

  /* Maximize the window */
  constraint_state.was_called = FALSE;
  meta_window_maximize (window);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  /* Verify constraint was called during maximize */
  g_assert_true (constraint_state.was_called);

  /* Verify maximized window respects constraint size */
  meta_window_get_frame_rect (window, &frame_rect);

  g_assert_cmpint (frame_rect.x, ==, constraint_state.target_rect.x);
  g_assert_cmpint (frame_rect.y, ==, constraint_state.target_rect.y);
  g_assert_cmpint (frame_rect.width, ==, constraint_state.target_rect.width);
  g_assert_cmpint (frame_rect.height, ==, constraint_state.target_rect.height);

  /* Cleanup */
  meta_test_client_destroy (test_client);
  while (window)
    g_main_context_iteration (NULL, TRUE);

  constraint_state.enabled = FALSE;
  constraint_state.expected_window = NULL;
}

/* Test: External constraint limits fullscreen window size */
static void
test_external_constraint_fullscreen (void)
{
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;
  MetaWindow *window = NULL;
  MtkRectangle frame_rect;

  /* Configure constraint to a smaller size than monitor */
  constraint_state.enabled = TRUE;
  constraint_state.target_rect = (MtkRectangle) { 100, 80, 500, 350 };
  constraint_state.was_called = FALSE;

  /* Create window */
  test_client = meta_test_client_new (test_context,
                                      "external-constraint-test-client",
                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  g_assert_nonnull (test_client);
  g_assert_no_error (error);

  meta_test_client_run (test_client,
                        "create " TEST_CLIENT_TITLE " csd\n"
                        "show " TEST_CLIENT_TITLE "\n");

  while (!(window = meta_test_client_find_window (test_client,
                                                   TEST_CLIENT_TITLE,
                                                   NULL)))
    g_main_context_iteration (NULL, TRUE);

  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *) &window);
  wait_for_window_added (window);

  /* Make the window fullscreen */
  constraint_state.was_called = FALSE;
  meta_window_make_fullscreen (window);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  /* Verify constraint was called during fullscreen */
  g_assert_true (constraint_state.was_called);

  /* Verify fullscreen window respects constraint size */
  meta_window_get_frame_rect (window, &frame_rect);

  g_assert_cmpint (frame_rect.x, ==, constraint_state.target_rect.x);
  g_assert_cmpint (frame_rect.y, ==, constraint_state.target_rect.y);
  g_assert_cmpint (frame_rect.width, ==, constraint_state.target_rect.width);
  g_assert_cmpint (frame_rect.height, ==, constraint_state.target_rect.height);

  /* Cleanup */
  meta_test_client_destroy (test_client);
  while (window)
    g_main_context_iteration (NULL, TRUE);

  constraint_state.enabled = FALSE;
  constraint_state.expected_window = NULL;
}

/* Test: External constraint applies on window resize */
static void
test_external_constraint_resize (void)
{
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;
  MetaWindow *window = NULL;
  MtkRectangle frame_rect;

  /* Configure constraint to limit window size */
  constraint_state.enabled = TRUE;
  constraint_state.target_rect = (MtkRectangle) { 150, 100, 350, 250 };
  constraint_state.was_called = FALSE;

  /* Create window */
  test_client = meta_test_client_new (test_context,
                                      "external-constraint-test-client",
                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  g_assert_nonnull (test_client);
  g_assert_no_error (error);

  meta_test_client_run (test_client,
                        "create " TEST_CLIENT_TITLE " csd\n"
                        "show " TEST_CLIENT_TITLE "\n");

  while (!(window = meta_test_client_find_window (test_client,
                                                   TEST_CLIENT_TITLE,
                                                   NULL)))
    g_main_context_iteration (NULL, TRUE);

  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *) &window);
  wait_for_window_added (window);

  /* Verify initial constraint was applied */
  meta_window_get_frame_rect (window, &frame_rect);
  g_assert_cmpint (frame_rect.width, ==, constraint_state.target_rect.width);
  g_assert_cmpint (frame_rect.height, ==, constraint_state.target_rect.height);

  /* Try to resize window to a larger size via the client */
  constraint_state.was_called = FALSE;
  meta_window_move_resize_frame (window, TRUE, 10, 10, 500, 400);

  /* Wait for the resize to be processed */
  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  /* Give it a bit more time to ensure all constraint processing completes */
  g_usleep (50000);  /* 50ms */

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  /* Verify constraint was called during client-initiated resize */
  g_assert_true (constraint_state.was_called);

  /* Verify window size is constrained to our limit, not the requested 500x400 */
  meta_window_get_frame_rect (window, &frame_rect);

  g_assert_cmpint (frame_rect.x, >=, constraint_state.target_rect.x);
  g_assert_cmpint (frame_rect.y, >=, constraint_state.target_rect.y);
  g_assert_cmpint (frame_rect.width, <=, constraint_state.target_rect.width);
  g_assert_cmpint (frame_rect.height, <=, constraint_state.target_rect.height);

  /* Cleanup */
  meta_test_client_destroy (test_client);
  while (window)
    g_main_context_iteration (NULL, TRUE);

  constraint_state.enabled = FALSE;
  constraint_state.expected_window = NULL;
}

static MetaExternalConstraint *test_constraint = NULL;

static void
on_window_created (MetaDisplay *display,
                   MetaWindow  *window,
                   gpointer     user_data)
{
  /* Add constraint to every newly created window */
  if (test_constraint)
    meta_window_add_external_constraint (window, test_constraint);
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor;
  MetaDisplay *display;

  /* Create our constraint */
  test_constraint = g_object_new (TEST_TYPE_CONSTRAINT, NULL);

  /* Connect to window-created signal to add constraint to new windows */
  display = meta_context_get_display (test_context);
  g_signal_connect (display, "window-created",
                    G_CALLBACK (on_window_created), NULL);

  /* Setup test environment */
  compositor = meta_context_get_wayland_compositor (test_context);
  test_driver = meta_wayland_test_driver_new (compositor);
  virtual_monitor = meta_create_test_monitor (test_context, 640, 480, 60.0);
}

static void
on_after_tests (void)
{
  MetaDisplay *display;

  /* Disconnect signal handler */
  display = meta_context_get_display (test_context);
  g_signal_handlers_disconnect_by_func (display,
                                        G_CALLBACK (on_window_created),
                                        NULL);

  /* Cleanup constraint */
  g_clear_object (&test_constraint);

  g_clear_object (&test_driver);
  g_clear_object (&virtual_monitor);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/external-constraints/basic",
                   test_external_constraint_basic);
  g_test_add_func ("/backends/external-constraints/move",
                   test_external_constraint_move);
  g_test_add_func ("/backends/external-constraints/resize",
                   test_external_constraint_resize);
  g_test_add_func ("/backends/external-constraints/maximized",
                   test_external_constraint_maximized);
  g_test_add_func ("/backends/external-constraints/fullscreen",
                   test_external_constraint_fullscreen);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
#ifdef MUTTER_PRIVILEGED_TEST
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
#endif
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
