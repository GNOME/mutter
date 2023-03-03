/*
 * Copyright (C) 2023 Red Hat
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

#include "backends/meta-monitor-manager-private.h"
#include "meta-test/meta-context-test.h"

typedef enum _State
{
  INIT,
  PAINTED,
  PRESENTED,
} State;

static MetaContext *test_context;

static void
on_after_paint (ClutterStage     *stage,
                ClutterStageView *view,
                ClutterFrame     *frame,
                State            *state)
{
  *state = PAINTED;
}

static void
on_presented (ClutterStage     *stage,
              ClutterStageView *view,
              ClutterFrameInfo *frame_info,
              State            *state)
{
  if (*state == PAINTED)
    *state = PRESENTED;
}

static void
meta_test_reload (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterActor *stage = meta_backend_get_stage (backend);
  GList *logical_monitors;
  gulong after_paint_handler_id;
  gulong presented_handler_id;
  g_autoptr (GError) error = NULL;
  State state;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);

  after_paint_handler_id = g_signal_connect (stage, "after-paint",
                                             G_CALLBACK (on_after_paint),
                                             &state);
  presented_handler_id = g_signal_connect (stage, "presented",
                                           G_CALLBACK (on_presented),
                                           &state);

  state = INIT;
  clutter_actor_queue_redraw (stage);
  while (state < PAINTED)
    g_main_context_iteration (NULL, TRUE);

  meta_monitor_manager_reload (monitor_manager);

  while (state < PRESENTED)
    g_main_context_iteration (NULL, TRUE);

  state = INIT;
  clutter_actor_queue_redraw (stage);
  while (state < PRESENTED)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (stage, after_paint_handler_id);
  g_signal_handler_disconnect (stage, presented_handler_id);
}

static void
init_tests (void)
{
  g_test_add_func ("/hotplug/reload",
                   meta_test_reload);
}

int
main (int argc, char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  test_context = context;

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
