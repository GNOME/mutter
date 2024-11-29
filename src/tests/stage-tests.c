/*
 * Copyright (C) 2024 Red Hat Inc.
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
 *
 */

#include "config.h"

#include "backends/meta-backend-private.h"
#include "core/meta-context-private.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-test/meta-context-test.h"

static MetaContext *test_context;

static gboolean
event_filter_cb (const ClutterEvent *event,
                 ClutterActor       *event_actor,
                 gpointer            user_data)
{
  gboolean *saw_event = user_data;

  if (clutter_event_type (event) == CLUTTER_DEVICE_ADDED)
    *saw_event = TRUE;

  return CLUTTER_EVENT_PROPAGATE;
}

static void
meta_test_stage_scheduling_delayed_show (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (MetaVirtualMonitor) virtual_monitor = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  guint filter_id;
  gboolean saw_event = FALSE;

  virtual_monitor = meta_create_test_monitor (test_context, 800, 600, 60.0f);
  g_debug ("Wait for initial dummy dispatch");
  while (TRUE)
    {
      if (!g_main_context_iteration (NULL, FALSE))
        break;
    }

  filter_id = clutter_event_add_filter (NULL, event_filter_cb, NULL, &saw_event);
  g_debug ("Creating virtual pointer");
  virtual_pointer =
    clutter_seat_create_virtual_device (seat, CLUTTER_KEYBOARD_DEVICE);
  while (!saw_event)
    g_main_context_iteration (NULL, TRUE);
  g_debug ("Scheduling update with DEVICE_ADDED in stage queue");
  clutter_stage_schedule_update (CLUTTER_STAGE (stage));
  g_debug ("Showing stage");
  clutter_actor_show (stage);
  g_debug ("Waiting for paint");
  clutter_actor_queue_redraw (stage);
  meta_wait_for_paint (test_context);
  clutter_event_remove_filter (filter_id);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_auto (GVariantBuilder) plugin_options_builder =
    G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
  g_autoptr (GVariant) plugin_options = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  g_variant_builder_add (&plugin_options_builder, "{sv}",
                         "show-stage", g_variant_new_boolean (FALSE));
  plugin_options =
    g_variant_ref_sink (g_variant_builder_end (&plugin_options_builder));
  meta_context_set_plugin_options (context, plugin_options);

  test_context = context;

  g_test_add_func ("/stage/scheduling/delayed-show",
                   meta_test_stage_scheduling_delayed_show);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
