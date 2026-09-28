/*
 * Copyright (C) 2026 Misha Nasledov
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

#include <linux/input-event-codes.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "meta/meta-idle-monitor.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-test/meta-context-test.h"

#define IDLE_TIME_MS 100

static MetaContext *test_context;

static gboolean
key_release_filter_cb (const ClutterEvent *event,
                       ClutterActor       *event_actor,
                       gpointer            user_data)
{
  gboolean *saw_release = user_data;

  if (clutter_event_type (event) == CLUTTER_KEY_RELEASE)
    *saw_release = TRUE;

  return CLUTTER_EVENT_PROPAGATE;
}

static void
meta_test_idle_monitor_power_save_input (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaIdleMonitor *idle_monitor = meta_backend_get_core_idle_monitor (backend);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (MetaVirtualMonitor) virtual_monitor = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  struct {
    const char *name;
    gboolean predates_power_save;
    gboolean resets_idle_time;
  } cases[] = {
    { "input that predates power saving", TRUE, FALSE },
    { "input during power saving", FALSE, TRUE },
  };
  size_t i;

  virtual_monitor = meta_create_test_monitor (test_context, 800, 600, 60.0f);
  virtual_keyboard =
    clutter_seat_create_virtual_device (seat, CLUTTER_KEYBOARD_DEVICE);

  for (i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      int64_t before_power_save_us;
      int64_t event_time_us;
      int64_t idle_time_ms;
      gboolean saw_release = FALSE;
      unsigned int filter_id;

      g_debug ("Testing %s", cases[i].name);

      /* Without GPUs, the power save start can be recorded within the same
       * microsecond. */
      before_power_save_us = g_get_monotonic_time () - 1;
      meta_monitor_manager_set_power_save_mode (monitor_manager,
                                                META_POWER_SAVE_OFF);
      g_usleep (IDLE_TIME_MS * 1000);

      event_time_us = cases[i].predates_power_save ? before_power_save_us
                                                   : CLUTTER_CURRENT_TIME;
      filter_id = clutter_event_add_filter (NULL, key_release_filter_cb,
                                            NULL, &saw_release);
      clutter_virtual_input_device_notify_key (virtual_keyboard,
                                               event_time_us,
                                               KEY_A,
                                               CLUTTER_KEY_STATE_PRESSED);
      clutter_virtual_input_device_notify_key (virtual_keyboard,
                                               event_time_us,
                                               KEY_A,
                                               CLUTTER_KEY_STATE_RELEASED);
      meta_flush_input (test_context);
      while (!saw_release)
        g_main_context_iteration (NULL, TRUE);
      clutter_event_remove_filter (filter_id);

      idle_time_ms = meta_idle_monitor_get_idletime (idle_monitor);
      if (cases[i].resets_idle_time)
        g_assert_cmpint (idle_time_ms, <, IDLE_TIME_MS);
      else
        g_assert_cmpint (idle_time_ms, >=, IDLE_TIME_MS);

      meta_monitor_manager_set_power_save_mode (monitor_manager,
                                                META_POWER_SAVE_ON);
    }
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  g_test_add_func ("/idle-monitor/power-save-input",
                   meta_test_idle_monitor_power_save_input);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
