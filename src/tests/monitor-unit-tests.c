/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat, Inc.
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

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-config-migration.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-orientation-manager.h"
#include "backends/meta-output.h"
#include "core/window-private.h"
#include "meta-backend-test.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-monitor-manager-test.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-sensors-proxy-mock.h"
#include "tests/meta-test-utils.h"
#include "x11/meta-x11-display-private.h"

static MetaContext *test_context;
static MetaBackend *test_backend;

static MonitorTestCase initial_test_case = {
  .setup = {
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
  },

  .expect = {
    .monitors = {
      {
        .outputs = { 0 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 0,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 222,
        .height_mm = 125
      },
      {
        .outputs = { 1 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 1,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 220,
        .height_mm = 124
      }
    },
    .n_monitors = 2,
    .logical_monitors = {
      {
        .monitors = { 0 },
        .n_monitors = 1,
        .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      },
      {
        .monitors = { 1 },
        .n_monitors = 1,
        .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      }
    },
    .n_logical_monitors = 2,
    .primary_logical_monitor = 0,
    .n_outputs = 2,
    .crtcs = {
      {
        .current_mode = 0,
      },
      {
        .current_mode = 0,
        .x = 1024,
      }
    },
    .n_crtcs = 2,
    .screen_width = 1024 * 2,
    .screen_height = 768
  }
};

static MetaTestClient *wayland_monitor_test_client = NULL;
static MetaTestClient *x11_monitor_test_client = NULL;

#define WAYLAND_TEST_CLIENT_NAME "wayland_monitor_test_client"
#define WAYLAND_TEST_CLIENT_WINDOW "window1"
#define X11_TEST_CLIENT_NAME "x11_monitor_test_client"
#define X11_TEST_CLIENT_WINDOW "window1"

static void
on_monitors_changed (gboolean *monitors_changed)
{
  *monitors_changed = TRUE;
}

static void
create_monitor_test_clients (MetaContext *context)
{
  GError *error = NULL;

  wayland_monitor_test_client = meta_test_client_new (context,
                                                      WAYLAND_TEST_CLIENT_NAME,
                                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                                      &error);
  if (!wayland_monitor_test_client)
    g_error ("Failed to launch Wayland test client: %s", error->message);

  x11_monitor_test_client = meta_test_client_new (context,
                                                  X11_TEST_CLIENT_NAME,
                                                  META_WINDOW_CLIENT_TYPE_X11,
                                                  &error);
  if (!x11_monitor_test_client)
    g_error ("Failed to launch X11 test client: %s", error->message);

  if (!meta_test_client_do (wayland_monitor_test_client, &error,
                            "create", WAYLAND_TEST_CLIENT_WINDOW,
                            NULL))
    g_error ("Failed to create Wayland window: %s", error->message);

  if (!meta_test_client_do (x11_monitor_test_client, &error,
                            "create", X11_TEST_CLIENT_WINDOW,
                            NULL))
    g_error ("Failed to create X11 window: %s", error->message);

  if (!meta_test_client_do (wayland_monitor_test_client, &error,
                            "show", WAYLAND_TEST_CLIENT_WINDOW,
                            NULL))
    g_error ("Failed to show the window: %s", error->message);

  if (!meta_test_client_do (x11_monitor_test_client, &error,
                            "show", X11_TEST_CLIENT_WINDOW,
                            NULL))
    g_error ("Failed to show the window: %s", error->message);
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
check_monitor_test_clients_state (void)
{
  check_test_client_state (wayland_monitor_test_client);
  check_test_client_state (x11_monitor_test_client);
}

static void
destroy_monitor_test_clients (void)
{
  GError *error = NULL;

  if (!meta_test_client_quit (wayland_monitor_test_client, &error))
    g_error ("Failed to quit Wayland test client: %s", error->message);

  if (!meta_test_client_quit (x11_monitor_test_client, &error))
    g_error ("Failed to quit X11 test client: %s", error->message);

  meta_test_client_destroy (wayland_monitor_test_client);
  meta_test_client_destroy (x11_monitor_test_client);
}

static void
meta_test_monitor_initial_linear_config (void)
{
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &initial_test_case.expect));
  check_monitor_test_clients_state ();
}

static void
emulate_hotplug (MetaMonitorTestSetup *test_setup)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
  g_usleep (G_USEC_PER_SEC / 100);
}

static void
meta_test_monitor_config_store_set_current_on_empty (void)
{
  g_autoptr (MetaMonitorsConfig) linear_config = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *old_current;

  linear_config = meta_monitor_config_manager_create_linear (config_manager);
  old_current = meta_monitor_config_manager_get_current (config_manager);

  g_assert_null (old_current);
  g_assert_nonnull (linear_config);

  meta_monitor_config_manager_set_current (config_manager, linear_config);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            linear_config);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
meta_test_monitor_config_store_set_current_with_parent_on_empty (void)
{
  g_autoptr (MetaMonitorsConfig) parent_config = NULL;
  g_autoptr (MetaMonitorsConfig) child_config1 = NULL;
  g_autoptr (MetaMonitorsConfig) child_config2 = NULL;
  g_autoptr (MetaMonitorsConfig) child_config3 = NULL;
  g_autoptr (MetaMonitorsConfig) linear_config = NULL;
  g_autoptr (MetaMonitorsConfig) fallback_config = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *old_current;

  parent_config = meta_monitor_config_manager_create_linear (config_manager);

  child_config1 = meta_monitor_config_manager_create_linear (config_manager);
  meta_monitors_config_set_parent_config (child_config1, parent_config);
  old_current = meta_monitor_config_manager_get_current (config_manager);

  g_assert_null (old_current);
  g_assert_nonnull (child_config1);

  meta_monitor_config_manager_set_current (config_manager, child_config1);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            child_config1);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));

  child_config2 = meta_monitor_config_manager_create_linear (config_manager);
  meta_monitors_config_set_parent_config (child_config2, parent_config);
  g_assert (child_config2->parent_config == parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, child_config2);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            child_config2);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));

  child_config3 = meta_monitor_config_manager_create_linear (config_manager);
  meta_monitors_config_set_parent_config (child_config3, child_config2);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, child_config3);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            child_config3);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));

  linear_config = meta_monitor_config_manager_create_linear (config_manager);
  g_assert_null (linear_config->parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, linear_config);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            linear_config);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);
  g_assert (meta_monitor_config_manager_get_previous (config_manager) ==
            child_config3);

  fallback_config =
    meta_monitor_config_manager_create_fallback (config_manager);
  g_assert_null (fallback_config->parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_null (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, fallback_config);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            fallback_config);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);

  g_assert (meta_monitor_config_manager_get_previous (config_manager) ==
            linear_config);
  g_assert (meta_monitor_config_manager_pop_previous (config_manager) ==
            linear_config);
  g_assert (meta_monitor_config_manager_get_previous (config_manager) ==
            child_config3);
  g_assert (meta_monitor_config_manager_pop_previous (config_manager) ==
            child_config3);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
meta_test_monitor_config_store_set_current (void)
{
  g_autoptr (MetaMonitorsConfig) linear_config = NULL;
  g_autoptr (MetaMonitorsConfig) fallback_config = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *old_current;

  fallback_config =
    meta_monitor_config_manager_create_fallback (config_manager);
  linear_config = meta_monitor_config_manager_create_linear (config_manager);

  g_assert_nonnull (linear_config);
  g_assert_nonnull (fallback_config);

  meta_monitor_config_manager_set_current (config_manager, fallback_config);
  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            fallback_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  meta_monitor_config_manager_set_current (config_manager, linear_config);

  g_assert (old_current != linear_config);
  g_assert_nonnull (old_current);
  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            linear_config);
  g_assert (meta_monitor_config_manager_get_previous (config_manager) ==
            old_current);
  g_assert (meta_monitor_config_manager_pop_previous (config_manager) ==
            old_current);

  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
meta_test_monitor_config_store_set_current_with_parent (void)
{
  g_autoptr (MetaMonitorsConfig) child_config = NULL;
  g_autoptr (MetaMonitorsConfig) other_child = NULL;
  g_autoptr (MetaMonitorsConfig) linear_config = NULL;
  g_autoptr (MetaMonitorsConfig) fallback_config = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *old_current;

  linear_config = meta_monitor_config_manager_create_linear (config_manager);
  g_assert_null (linear_config->parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_null (old_current);
  meta_monitor_config_manager_set_current (config_manager, linear_config);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            linear_config);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));

  fallback_config = meta_monitor_config_manager_create_fallback (config_manager);
  g_assert_nonnull (fallback_config);
  g_assert_null (fallback_config->parent_config);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current);
  g_assert_null (old_current->parent_config);
  meta_monitor_config_manager_set_current (config_manager, fallback_config);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            fallback_config);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);
  g_assert (meta_monitor_config_manager_get_previous (config_manager) ==
            old_current);

  child_config = meta_monitor_config_manager_create_linear (config_manager);
  old_current = meta_monitor_config_manager_get_current (config_manager);
  meta_monitors_config_set_parent_config (child_config, old_current);

  g_assert_nonnull (child_config);
  g_assert_nonnull (old_current);
  g_assert (old_current == fallback_config);
  g_assert_null (old_current->parent_config);

  meta_monitor_config_manager_set_current (config_manager, child_config);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            child_config);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);
  g_assert (meta_monitor_config_manager_get_previous (config_manager) ==
            linear_config);

  other_child = meta_monitor_config_manager_create_linear (config_manager);
  meta_monitors_config_set_parent_config (other_child, old_current);

  old_current = meta_monitor_config_manager_get_current (config_manager);
  g_assert_nonnull (old_current->parent_config);
  g_assert (old_current == child_config);
  meta_monitor_config_manager_set_current (config_manager, other_child);

  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            other_child);
  g_assert (meta_monitor_config_manager_get_current (config_manager) !=
            old_current);
  g_assert (meta_monitor_config_manager_get_previous (config_manager) ==
            linear_config);
  g_assert (meta_monitor_config_manager_pop_previous (config_manager) ==
            linear_config);

  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
meta_test_monitor_config_store_set_current_max_size (void)
{
  /* Keep this in sync with CONFIG_HISTORY_MAX_SIZE */
  const unsigned int config_history_max_size = 3;
  g_autolist (MetaMonitorsConfig) added = NULL;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *previous = NULL;
  MetaMonitorsConfig *config;
  unsigned int i;

  for (i = 0; i < config_history_max_size; i++)
    {
      g_autoptr (MetaMonitorsConfig) linear_config = NULL;

      linear_config = meta_monitor_config_manager_create_linear (config_manager);
      g_assert_nonnull (linear_config);
      g_assert (!g_list_find (added, linear_config));

      if (i > 0)
        {
          g_assert (previous !=
                    meta_monitor_config_manager_get_current (config_manager));
        }

      previous = meta_monitor_config_manager_get_current (config_manager);
      meta_monitor_config_manager_set_current (config_manager, linear_config);
      added = g_list_prepend (added, g_object_ref (linear_config));

      g_assert (meta_monitor_config_manager_get_current (config_manager)
                == linear_config);

      g_assert (meta_monitor_config_manager_get_previous (config_manager)
                == previous);
    }

  for (i = 0; i < config_history_max_size - 1; i++)
    {
      g_autoptr (MetaMonitorsConfig) fallback = NULL;

      fallback = meta_monitor_config_manager_create_fallback (config_manager);
      g_assert_nonnull (fallback);

      meta_monitor_config_manager_set_current (config_manager, fallback);
      added = g_list_prepend (added, g_steal_pointer (&fallback));
    }

  g_assert_cmpuint (g_list_length (added), >, config_history_max_size);

  config = meta_monitor_config_manager_get_current (config_manager);
  g_assert (config == g_list_nth_data (added, 0));

  for (i = 0; i < config_history_max_size; i++)
    {
      config = meta_monitor_config_manager_get_previous (config_manager);
      g_assert_nonnull (config);
      g_assert (meta_monitor_config_manager_pop_previous (config_manager)
                == config);
      g_assert (config == g_list_nth_data (added, i + 1));
    }

  config = meta_monitor_config_manager_get_previous (config_manager);
  g_assert_null (config);
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
  g_assert (config != g_list_nth_data (added, config_history_max_size));
  g_assert_nonnull (g_list_nth_data (added, config_history_max_size + 1));
}

static void
meta_test_monitor_config_store_set_current_null (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorsConfig *previous;

  previous = meta_monitor_config_manager_get_current (config_manager);
  g_assert_null (previous);

  meta_monitor_config_manager_set_current (config_manager, NULL);

  g_assert_null (meta_monitor_config_manager_get_current (config_manager));
  g_assert_null (meta_monitor_config_manager_get_previous (config_manager));
  g_assert_null (meta_monitor_config_manager_pop_previous (config_manager));
}

static void
meta_test_monitor_one_disconnected_linear_config (void)
{
  MonitorTestCase test_case = initial_test_case;
  MetaMonitorTestSetup *test_setup;

  test_case.setup.n_outputs = 1;

  test_case.expect = (MonitorTestCaseExpect) {
    .monitors = {
      {
        .outputs = { 0 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 0,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 222,
        .height_mm = 125
      }
    },
    .n_monitors = 1,
    .logical_monitors = {
      {
        .monitors = { 0 },
        .n_monitors = 1,
        .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      },
    },
    .n_logical_monitors = 1,
    .primary_logical_monitor = 0,
    .n_outputs = 1,
    .crtcs = {
      {
        .current_mode = 0,
      },
      {
        .current_mode = -1,
      }
    },
    .n_crtcs = 2,
    .screen_width = 1024,
    .screen_height = 768
  };

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_one_off_linear_config (void)
{
  MonitorTestCase test_case;
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseOutput outputs[] = {
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
      .crtc = -1,
      .modes = { 0 },
      .n_modes = 1,
      .preferred_mode = 0,
      .possible_crtcs = { 1 },
      .n_possible_crtcs = 1,
      .width_mm = 224,
      .height_mm = 126
    }
  };

  test_case = initial_test_case;

  memcpy (&test_case.setup.outputs, &outputs, sizeof (outputs));
  test_case.setup.n_outputs = G_N_ELEMENTS (outputs);

  test_case.setup.crtcs[1].current_mode = -1;

  test_case.expect = (MonitorTestCaseExpect) {
    .monitors = {
      {
        .outputs = { 0 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 0,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 222,
        .height_mm = 125
      },
      {
        .outputs = { 1 },
        .n_outputs = 1,
        .modes = {
          {
            .width = 1024,
            .height = 768,
            .refresh_rate = 60.0,
            .crtc_modes = {
              {
                .output = 1,
                .crtc_mode = 0
              }
            }
          }
        },
        .n_modes = 1,
        .current_mode = 0,
        .width_mm = 224,
        .height_mm = 126
      }
    },
    .n_monitors = 2,
    .logical_monitors = {
      {
        .monitors = { 0 },
        .n_monitors = 1,
        .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      },
      {
        .monitors = { 1 },
        .n_monitors = 1,
        .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
        .scale = 1
      },
    },
    .n_logical_monitors = 2,
    .primary_logical_monitor = 0,
    .n_outputs = 2,
    .crtcs = {
      {
        .current_mode = 0,
      },
      {
        .current_mode = 0,
        .x = 1024,
      }
    },
    .n_crtcs = 2,
    .screen_width = 1024 * 2,
    .screen_height = 768
  };

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_preferred_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
        {
          .width = 1280,
          .height = 720,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 3,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1, 2 },
          .n_modes = 3,
          .preferred_mode = 1,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            },
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1
                }
              }
            },
            {
              .width = 1280,
              .height = 720,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 2
                }
              }
            }
          },
          .n_modes = 3,
          .current_mode = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 1,
      .screen_width = 1024,
      .screen_height = 768,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_tiled_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.0
        },
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                },
                {
                  .output = 1,
                  .crtc_mode = 0,
                }
              }
            },
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
          .x = 400,
          .y = 0
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_tiled_non_preferred_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 640,
          .height = 480,
          .refresh_rate = 60.0
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 512,
          .height = 768,
          .refresh_rate = 120.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
      },
      .n_modes = 4,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 2 },
          .n_modes = 2,
          .preferred_mode = 1,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 512,
            .tile_h = 768
          }
        },
        {
          .crtc = -1,
          .modes = { 1, 2, 3 },
          .n_modes = 3,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 512,
            .tile_h = 768
          }
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 120.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 2
                },
                {
                  .output = 1,
                  .crtc_mode = 2,
                }
              }
            },
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = -1
                },
                {
                  .output = 1,
                  .crtc_mode = 1,
                }
              }
            },
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = -1
                },
                {
                  .output = 1,
                  .crtc_mode = 3,
                }
              }
            },
          },
          .n_modes = 3,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 2,
        },
        {
          .current_mode = 2,
          .x = 512
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 1024,
      .screen_height = 768,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_tiled_non_main_origin_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 30.0
        },
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          }
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0,
                },
                {
                  .output = 1,
                  .crtc_mode = 0,
                }
              }
            },
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 30.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1
                },
                {
                  .output = 1,
                  .crtc_mode = -1,
                }
              }
            },
          },
          .n_modes = 2,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .x = 400,
          .y = 0
        },
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_hidpi_linear_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1280,
          .height = 720,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
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
          /* These will result in DPI of about 216" */
          .width_mm = 150,
          .height_mm = 85,
          .scale = 2,
        },
        {
          .crtc = 1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .scale = 1,
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1280,
              .height = 720,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            },
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 150,
          .height_mm = 85
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            },
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 640, .height = 360 },
          .scale = 2
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 640, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 1,
          .x = 640,
        }
      },
      .n_crtcs = 2,
      .screen_width = 640 + 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_suggested_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
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
          .hotplug_mode = TRUE,
          .suggested_x = 1024,
          .suggested_y = 758,
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
          .hotplug_mode = TRUE,
          .suggested_x = 0,
          .suggested_y = 0,
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      /*
       * Logical monitors expectations altered to correspond to the
       * "suggested_x/y" changed further below.
       */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 758, .width = 800, .height = 600 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 1,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .x = 1024,
          .y = 758,
        },
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024 + 800,
      .screen_height = 1358
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);

  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_suggested_config_overlapping (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
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
          .hotplug_mode = TRUE,
          .suggested_x = 800,
          .suggested_y = 600,
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
          .hotplug_mode = TRUE,
          .suggested_x = 0,
          .suggested_y = 0,
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      /*
       * Logical monitors expectations altered to correspond to the
       * "suggested_x/y" defined above.
       */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 1,
      .n_outputs = 2,
      .crtcs = {
        {
          .x = 1024,
          .y = 0,
          .current_mode = 0,
        },
        {
          .x = 0,
          .y = 0,
          .current_mode = 1,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024 + 800,
      .screen_height = MAX (768, 600)
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Suggested monitor config has overlapping region, "
                         "rejecting");
  emulate_hotplug (test_setup);
  g_test_assert_expected_messages ();

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
}

static void
meta_test_monitor_suggested_config_not_adjacent (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
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
          .hotplug_mode = TRUE,
          .suggested_x = 1920,
          .suggested_y = 1080,
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
          .hotplug_mode = TRUE,
          .suggested_x = 0,
          .suggested_y = 0,
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      /*
       * Logical monitors expectations follow fallback linear configuration
       */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 1,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .x = 1024,
          .y = 0,
        },
        {
          .current_mode = 1,
          .x = 0,
          .y = 0,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024 + 800,
      .screen_height = MAX (768, 600)
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Suggested monitor config has monitors with no "
                         "neighbors, rejecting");
  emulate_hotplug (test_setup);
  g_test_assert_expected_messages ();

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
}

static void
meta_test_monitor_suggested_config_multi_dpi (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 4096,
          .height = 2160,
          .refresh_rate = 60.0
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
          .hotplug_mode = TRUE,
          .suggested_x = 4096,
          .suggested_y = 2160,
        },
        {
          .crtc = 1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 350,
          .height_mm = 180,
          .scale = 2,
          .hotplug_mode = TRUE,
          .suggested_x = 0,
          .suggested_y = 0,
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 4096,
              .height = 2160,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 350,
          .height_mm = 180,
        }
      },
      .n_monitors = 2,
      /*
       * Logical monitors expectations altered to correspond to the
       * "suggested_x/y" changed further below.
       */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 4096/2, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 4096/2, .height = 2160/2 },
          .scale = 2
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 1,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .x = 2048,
        },
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 4096/2 + 800,
      .screen_height = 2160/2
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Suggested monitor config has monitors with no "
                         "neighbors, rejecting");
  emulate_hotplug (test_setup);
  g_test_assert_expected_messages();

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
}

static void
meta_test_monitor_limited_crtcs (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = -1,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Failed to use linear *");

  emulate_hotplug (test_setup);
  g_test_assert_expected_messages ();

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_lid_switch_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_laptop_panel = TRUE,
          .serial = "0x123456a",
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
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
          .current_mode = 0
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024 * 2,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  test_case.expect.logical_monitors[0] = (MonitorTestCaseLogicalMonitor) {
    .monitors = { 1 },
    .n_monitors = 1,
    .layout = {.x = 0, .y = 0, .width = 1024, .height = 768 },
    .scale = 1
  };
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 1024;
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].x = 0;

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  test_case.expect.logical_monitors[0] = (MonitorTestCaseLogicalMonitor) {
    .monitors = { 0 },
    .n_monitors = 1,
    .layout = {.x = 0, .y = 0, .width = 1024, .height = 768 },
    .scale = 1
  };
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 * 2;
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.primary_logical_monitor = 0;

  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_lid_opened_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_laptop_panel = TRUE,
          .serial = "0x123456a",
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
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
          .current_mode = 0
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = -1,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1, /* Second one checked after lid opened. */
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1,
        },
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 * 2;
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[0].x = 1024;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 0;

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_lid_closed_no_external (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_laptop_panel = TRUE
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        },
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_lid_closed_with_hotplugged_external (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .is_laptop_panel = TRUE
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_outputs = 1, /* Second is hotplugged later */
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 1, /* Second is hotplugged later */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1, /* Second is hotplugged later */
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = -1,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);

  /*
   * The first part of this test emulate the following:
   *  1) Start with the lid open
   *  2) Connect external monitor
   *  3) Close lid
   */

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);

  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  g_test_message ("External monitor connected");
  test_case.setup.n_outputs = 2;
  test_case.expect.n_outputs = 2;
  test_case.expect.n_monitors = 2;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  g_test_message ("Lid closed");
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.logical_monitors[0].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].x = 0;
  test_case.expect.screen_width = 1024;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  /*
   * The second part of this test emulate the following:
   *  1) Open lid
   *  2) Disconnect external monitor
   *  3) Close lid
   *  4) Open lid
   */

  g_test_message ("Lid opened");
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.logical_monitors[0].monitors[0] = 0,
  test_case.expect.logical_monitors[1].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  g_test_message ("External monitor disconnected");
  test_case.setup.n_outputs = 1;
  test_case.expect.n_outputs = 1;
  test_case.expect.n_monitors = 1;
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.crtcs[1].current_mode = -1;
  test_case.expect.screen_width = 1024;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  g_test_message ("Lid closed");
  test_case.expect.logical_monitors[0].monitors[0] = 0,
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 1024;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  g_test_message ("Lid opened");
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_lid_scaled_closed_opened (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .is_laptop_panel = TRUE,
	  .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1920,
              .height = 1080,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 960, .height = 540 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 960,
      .screen_height = 540
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "lid-scale.xml");
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_no_outputs (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .n_modes = 0,
      .n_outputs = 0,
      .n_crtcs = 0
    },

    .expect = {
      .n_monitors = 0,
      .n_logical_monitors = 0,
      .primary_logical_monitor = -1,
      .n_outputs = 0,
      .n_crtcs = 0,
      .n_tiled_monitors = 0,
      .screen_width = META_MONITOR_MANAGER_MIN_SCREEN_WIDTH,
      .screen_height = META_MONITOR_MANAGER_MIN_SCREEN_HEIGHT
    }
  };
  MetaMonitorTestSetup *test_setup;
  GError *error = NULL;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);

  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  if (!meta_test_client_do (x11_monitor_test_client, &error,
                            "resize", X11_TEST_CLIENT_WINDOW,
                            "123", "210",
                            NULL))
    g_error ("Failed to resize X11 window: %s", error->message);

  if (!meta_test_client_do (wayland_monitor_test_client, &error,
                            "resize", WAYLAND_TEST_CLIENT_WINDOW,
                            "123", "210",
                            NULL))
    g_error ("Failed to resize Wayland window: %s", error->message);

  check_monitor_test_clients_state ();

  /* Also check that we handle going headless -> headless */
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);

  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_underscanning_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_underscanning = TRUE,
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
          .is_underscanning = TRUE,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_max_bpc_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .max_bpc = 8,
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
          .max_bpc = 8,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_rgb_range_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .rgb_range = META_OUTPUT_RGB_RANGE_FULL,
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
          .rgb_range = META_OUTPUT_RGB_RANGE_FULL,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_preferred_non_first_mode (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0,
          .flags = META_CRTC_MODE_FLAG_NHSYNC,
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0,
          .flags = META_CRTC_MODE_FLAG_PHSYNC,
        },
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 1,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1
                }
              }
            },
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_non_upright_panel (void)
{
  MonitorTestCase test_case = initial_test_case;
  MetaMonitorTestSetup *test_setup;

  test_case.setup.modes[1] = (MonitorTestCaseMode) {
    .width = 768,
    .height = 1024,
    .refresh_rate = 60.0,
  };
  test_case.setup.n_modes = 2;  
  test_case.setup.outputs[0].modes[0] = 1;
  test_case.setup.outputs[0].preferred_mode = 1;
  test_case.setup.outputs[0].panel_orientation_transform =
    META_MONITOR_TRANSFORM_90;
  /*
   * Note we do not swap outputs[0].width_mm and height_mm, because these get
   * swapped for rotated panels inside the xrandr / kms code and we directly
   * create a dummy output here, skipping this code.
   */
  test_case.setup.crtcs[0].current_mode = 1;

  test_case.expect.monitors[0].modes[0].crtc_modes[0].crtc_mode = 1;
  test_case.expect.crtcs[0].current_mode = 1;
  test_case.expect.crtcs[0].transform = META_MONITOR_TRANSFORM_90;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_switch_external_without_external (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_laptop_panel = TRUE
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .is_laptop_panel = TRUE
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
          .x = 1024,
        },
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 2048,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_EXTERNAL);
  while (g_main_context_iteration (NULL, FALSE));
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));

  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_switch_config_remember_scale (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
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
          .height_mm = 125,
          .is_laptop_panel = TRUE,
          .serial = "0x1000",
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 220,
          .height_mm = 124,
          .serial = "0x1001",
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1920,
              .height = 1080,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1920,
              .height = 1080,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1920, .height = 1080 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1920, .y = 0, .width = 1920, .height = 1080 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
          .x = 1920,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1920 * 2,
      .screen_height = 1080
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (test_backend);

  /*
   * Check that default configuration is non-scaled linear.
   */

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  /*
   * Reconfigure to both monitors having scale 2.
   */

  test_case.expect.logical_monitors[0] = (MonitorTestCaseLogicalMonitor) {
    .monitors = { 0 },
    .layout = {.x = 0, .y = 0, .width = 960, .height = 540 },
    .scale = 2,
    .n_monitors = 1,
  };
  test_case.expect.logical_monitors[1] = (MonitorTestCaseLogicalMonitor) {
    .monitors = { 1 },
    .layout = {.x = 960, .y = 0, .width = 960, .height = 540 },
    .scale = 2,
    .n_monitors = 1,
  };
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[1].x = 960;
  test_case.expect.screen_width = 960 * 2;
  test_case.expect.screen_height = 540;

  meta_set_custom_monitor_config (test_context, "switch-remember-scale.xml");
  meta_monitor_manager_reconfigure (monitor_manager);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  /*
   * Check that switch to 'builtin' uses scale 2.
   */

  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 960;
  test_case.expect.monitors[1].current_mode = -1;
  test_case.expect.crtcs[1].current_mode = -1;

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_BUILTIN);
  while (g_main_context_iteration (NULL, FALSE));
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  /*
   * Check that switch to 'external' uses scale 2.
   */

  test_case.expect.logical_monitors[0].monitors[0] = 1;
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 960;
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.monitors[1].current_mode = 0;
  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 0;

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_EXTERNAL);
  while (g_main_context_iteration (NULL, FALSE));
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  /*
   * Check that switch to 'linear' uses scale 2 for both.
   */

  test_case.expect.logical_monitors[0].monitors[0] = 1;
  test_case.expect.logical_monitors[1].monitors[0] = 0;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 960 * 2;
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[0].x = 960;

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_ALL_LINEAR);
  while (g_main_context_iteration (NULL, FALSE));
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  /*
   * Check that switch to 'mirror' uses scale 2 for both.
   */

  test_case.expect.logical_monitors[0].monitors[0] = 0;
  test_case.expect.logical_monitors[0].monitors[1] = 1;
  test_case.expect.logical_monitors[0].n_monitors = 2;
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 960;
  test_case.expect.crtcs[0].x = 0;

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_ALL_MIRROR);
  while (g_main_context_iteration (NULL, FALSE));
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
check_monitor_configuration_per_orientation (MonitorTestCase *test_case,
                                             unsigned int     monitor_index,
                                             MetaOrientation  orientation,
                                             int              width,
                                             int              height)
{
  MetaMonitorTransform transform;
  MetaMonitorTransform output_transform;
  MonitorTestCaseExpect expect = test_case->expect;
  MonitorTestCaseSetup *setup = &test_case->setup;
  int i = 0;

  transform = meta_monitor_transform_from_orientation (orientation);
  output_transform = setup->outputs[monitor_index].panel_orientation_transform;
  expect.logical_monitors[monitor_index].transform =
    meta_monitor_transform_transform (transform,
      meta_monitor_transform_invert (output_transform));
  expect.crtcs[monitor_index].transform = transform;

  if (meta_monitor_transform_is_rotated (transform))
    {
      expect.logical_monitors[monitor_index].layout.width = height;
      expect.logical_monitors[monitor_index].layout.height = width;
    }
  else
    {
      expect.logical_monitors[monitor_index].layout.width = width;
      expect.logical_monitors[monitor_index].layout.height = height;
    }

  expect.screen_width = 0;
  expect.screen_height = 0;

  for (i = 0; i < expect.n_logical_monitors; ++i)
    {
      MonitorTestCaseLogicalMonitor *monitor =
        &expect.logical_monitors[i];
      int right_edge;
      int bottom_edge;

      g_debug ("Got monitor %dx%d : %dx%d", monitor->layout.x,
               monitor->layout.y, monitor->layout.width, monitor->layout.height);

      right_edge = (monitor->layout.width + monitor->layout.x);
      if (right_edge > expect.screen_width)
        expect.screen_width = right_edge;

      bottom_edge = (monitor->layout.height + monitor->layout.y);
      if (bottom_edge > expect.screen_height)
        expect.screen_height = bottom_edge;
    }

  meta_check_monitor_configuration (test_context,
                                    &expect);
  check_monitor_test_clients_state ();
}

typedef MetaSensorsProxyMock MetaSensorsProxyAutoResetMock;
static void
meta_sensors_proxy_reset (MetaSensorsProxyMock *proxy)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);

  g_test_message ("Resetting proxy");
  meta_sensors_proxy_mock_set_orientation (proxy,
                                           META_ORIENTATION_NORMAL);
  meta_wait_for_orientation (orientation_manager, META_ORIENTATION_NORMAL, NULL);
  g_object_unref (proxy);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaSensorsProxyAutoResetMock,
                               meta_sensors_proxy_reset)

typedef ClutterInputDevice ClutterAutoRemoveInputDevice;
static void
input_device_test_remove (ClutterAutoRemoveInputDevice *device)
{
  MetaBackend *backend = meta_context_get_backend (test_context);

  meta_backend_test_remove_device (META_BACKEND_TEST (backend), device);
  g_object_unref (device);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterAutoRemoveInputDevice,
                               input_device_test_remove)

static void
meta_test_monitor_orientation_is_managed (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .height_mm = 125,
          .is_laptop_panel = FALSE,
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        },
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);

  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  emulate_hotplug (test_setup);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);
  check_monitor_test_clients_state ();

  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  g_assert_null (meta_monitor_manager_get_laptop_panel (monitor_manager));
  test_case.setup.outputs[0].is_laptop_panel = TRUE;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  g_assert_nonnull (meta_monitor_manager_get_laptop_panel (monitor_manager));

  g_assert_false (clutter_seat_get_touch_mode (seat));
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       "test-touchscreen",
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  g_assert_true (clutter_seat_get_touch_mode (seat));
  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  orientation_mock = meta_sensors_proxy_mock_get ();
  g_assert_false (
    meta_orientation_manager_has_accelerometer (orientation_manager));
  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_sensors_proxy_mock_set_property (orientation_mock, "HasAccelerometer",
                                        g_variant_new_boolean (TRUE));

  while (!meta_orientation_manager_has_accelerometer (orientation_manager))
    g_main_context_iteration (NULL, FALSE);

  g_assert_true (
    meta_orientation_manager_has_accelerometer (orientation_manager));
  g_assert_true (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  test_case.setup.outputs[0].is_laptop_panel = FALSE;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  g_assert_null (meta_monitor_manager_get_laptop_panel (monitor_manager));
  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  test_case.setup.outputs[0].is_laptop_panel = TRUE;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  g_assert_nonnull (meta_monitor_manager_get_laptop_panel (monitor_manager));
  g_assert_true (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_sensors_proxy_mock_set_property (orientation_mock, "HasAccelerometer",
                                        g_variant_new_boolean (FALSE));

  while (meta_orientation_manager_has_accelerometer (orientation_manager))
    g_main_context_iteration (NULL, FALSE);

  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_sensors_proxy_mock_set_property (orientation_mock, "HasAccelerometer",
                                        g_variant_new_boolean (TRUE));

  while (!meta_orientation_manager_has_accelerometer (orientation_manager))
    g_main_context_iteration (NULL, FALSE);

  g_assert_true (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_backend_test_remove_device (META_BACKEND_TEST (backend), touch_device);
  g_clear_object (&touch_device);

  g_assert_false (clutter_seat_get_touch_mode (seat));
  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       "test-touchscreen",
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  g_assert_true (clutter_seat_get_touch_mode (seat));
  g_assert_true (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));
}

static void
meta_test_monitor_orientation_initial_rotated (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .is_laptop_panel = TRUE
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  MetaOrientation orientation;
  unsigned int times_signalled = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       "test-touchscreen",
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);
  orientation = META_ORIENTATION_LEFT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);
  meta_wait_for_orientation (orientation_manager, orientation,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, orientation, 1024, 768));
}

static void
meta_test_monitor_orientation_initial_rotated_no_touch_mode (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .is_laptop_panel = TRUE
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  MetaOrientation orientation;
  unsigned int times_signalled = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  orientation = META_ORIENTATION_LEFT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);
  meta_wait_for_orientation (orientation_manager, orientation,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL, 1024, 768));
}

static void
meta_test_monitor_orientation_initial_stored_rotated (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .is_laptop_panel = TRUE,
          .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1920,
              .height = 1080,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 960, .height = 540 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 960,
      .screen_height = 540
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  MetaOrientation orientation;
  unsigned int times_signalled = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       "test-touchscreen",
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);
  orientation = META_ORIENTATION_RIGHT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);
  meta_wait_for_orientation (orientation_manager, orientation,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "lid-scale.xml");
  emulate_hotplug (test_setup);


  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, orientation, 960, 540));

  g_test_message ("Closing lid");
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);


  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, orientation, 960, 540));

  g_test_message ("Rotating to left-up");
  orientation = META_ORIENTATION_LEFT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);
  meta_wait_for_orientation (orientation_manager, orientation,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);


  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, orientation, 960, 540));

  /* When no touch device is available, the orientation change is ignored */
  g_test_message ("Removing touch device");
  meta_backend_test_remove_device (META_BACKEND_TEST (backend), touch_device);

  g_test_message ("Rotating to right-up");
  orientation = META_ORIENTATION_RIGHT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);
  meta_wait_for_orientation (orientation_manager, orientation,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_LEFT_UP,
                        960, 540));
}

static void
meta_test_monitor_orientation_initial_stored_rotated_no_touch (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .is_laptop_panel = TRUE,
          .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1920,
              .height = 1080,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 960, .height = 540 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 960,
      .screen_height = 540
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  MetaOrientation orientation;
  unsigned int times_signalled = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  orientation = META_ORIENTATION_RIGHT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);
  meta_wait_for_orientation (orientation_manager, orientation,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "lid-scale.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        960, 540));

  g_test_message ("Closing lid");
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);


  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        960, 540));
}

static void
meta_test_monitor_orientation_changes (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .is_laptop_panel = TRUE
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  g_autoptr (MetaMonitorsConfig) initial_config = NULL;
  g_autoptr (MetaMonitorsConfig) previous_config = NULL;
  gboolean got_monitors_changed = FALSE;
  MetaOrientation i;
  unsigned int times_signalled = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       "test-touchscreen",
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  g_set_object (&previous_config,
                meta_monitor_config_manager_get_previous (config_manager));
  g_set_object (&initial_config,
                meta_monitor_config_manager_get_current (config_manager));
  g_signal_connect_swapped (monitor_manager, "monitors-changed",
                            G_CALLBACK (on_monitors_changed),
                            &got_monitors_changed);

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_UNDEFINED);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      MetaMonitorsConfig *current;
      MetaMonitorsConfig *previous;

      got_monitors_changed = FALSE;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      meta_wait_for_orientation (orientation_manager, i, &times_signalled);
      g_assert_cmpuint (times_signalled, <=, 1);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));

      current = meta_monitor_config_manager_get_current (config_manager);
      previous = meta_monitor_config_manager_get_previous (config_manager);

      g_assert_true (got_monitors_changed);
      g_assert (previous == previous_config);
      g_assert (current != initial_config);
      g_assert_true (meta_monitors_config_key_equal (current->key,
                                                     initial_config->key));
    }

  /* Ensure applying the current orientation doesn't change the config */
  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  g_set_object (&initial_config,
                meta_monitor_config_manager_get_current (config_manager));

  got_monitors_changed = FALSE;
  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);
  meta_wait_for_orientation (orientation_manager, META_ORIENTATION_NORMAL,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, ==, 0);
  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        1024, 768));

  g_assert_false (got_monitors_changed);
  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            initial_config);

  /* When no touch device is available, the orientation changes are ignored */
  g_test_message ("Removing touch device");
  meta_backend_test_remove_device (META_BACKEND_TEST (backend), touch_device);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      MetaMonitorsConfig *current;
      MetaMonitorsConfig *previous;

      got_monitors_changed = FALSE;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      meta_wait_for_orientation (orientation_manager, i, &times_signalled);
      g_assert_cmpuint (times_signalled, <=, 1);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, META_ORIENTATION_NORMAL,
                            1024, 768));

      current = meta_monitor_config_manager_get_current (config_manager);
      previous = meta_monitor_config_manager_get_previous (config_manager);

      g_assert (previous == previous_config);
      g_assert (current == initial_config);
      g_assert_false (got_monitors_changed);
    }

  g_signal_handlers_disconnect_by_data (monitor_manager, &got_monitors_changed);
}

static void
meta_test_monitor_orientation_changes_for_transformed_panel (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .is_laptop_panel = TRUE,
          .panel_orientation_transform = META_MONITOR_TRANSFORM_90,
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 768,
              .height = 1024,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  g_autoptr (MetaMonitorsConfig) initial_config = NULL;
  g_autoptr (MetaMonitorsConfig) previous_config = NULL;
  gboolean got_monitors_changed = FALSE;
  MetaOrientation i;
  unsigned int times_signalled = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       "test-touchscreen",
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  g_set_object (&previous_config,
                meta_monitor_config_manager_get_previous (config_manager));
  g_set_object (&initial_config,
                meta_monitor_config_manager_get_current (config_manager));
  g_signal_connect_swapped (monitor_manager, "monitors-changed",
                            G_CALLBACK (on_monitors_changed),
                            &got_monitors_changed);

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_UNDEFINED);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      MetaMonitorsConfig *current;
      MetaMonitorsConfig *previous;

      got_monitors_changed = FALSE;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      meta_wait_for_orientation (orientation_manager, i, &times_signalled);
      g_assert_cmpuint (times_signalled, <=, 1);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));

      current = meta_monitor_config_manager_get_current (config_manager);
      previous = meta_monitor_config_manager_get_previous (config_manager);

      g_assert_true (got_monitors_changed);
      g_assert (previous == previous_config);
      g_assert (current != initial_config);
      g_assert_true (meta_monitors_config_key_equal (current->key,
                                                     initial_config->key));
    }

  /* Ensure applying the current orientation doesn't change the config */
  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  g_set_object (&initial_config,
                meta_monitor_config_manager_get_current (config_manager));

  got_monitors_changed = FALSE;
  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);
  meta_wait_for_orientation (orientation_manager, META_ORIENTATION_NORMAL,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, ==, 0);
  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        1024, 768));

  g_assert_false (got_monitors_changed);
  g_assert (meta_monitor_config_manager_get_current (config_manager) ==
            initial_config);

  /* When no touch device is available, the orientation changes are ignored */
  g_test_message ("Removing touch device");
  meta_backend_test_remove_device (META_BACKEND_TEST (backend), touch_device);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      MetaMonitorsConfig *current;
      MetaMonitorsConfig *previous;

      got_monitors_changed = FALSE;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      meta_wait_for_orientation (orientation_manager, i, &times_signalled);
      g_assert_cmpuint (times_signalled, <=, 1);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, META_ORIENTATION_NORMAL,
                            1024, 768));

      current = meta_monitor_config_manager_get_current (config_manager);
      previous = meta_monitor_config_manager_get_previous (config_manager);

      g_assert (previous == previous_config);
      g_assert (current == initial_config);
      g_assert_false (got_monitors_changed);
    }

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       "test-touchscreen",
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);
  got_monitors_changed = FALSE;
  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_RIGHT_UP);
  meta_wait_for_orientation (orientation_manager,
                             META_ORIENTATION_RIGHT_UP,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);
  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_RIGHT_UP,
                        1024, 768));
  g_assert_true (got_monitors_changed);

  g_signal_handlers_disconnect_by_data (monitor_manager, &got_monitors_changed);
}

static void
meta_test_monitor_orientation_changes_with_hotplugging (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .is_laptop_panel = TRUE
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_outputs = 1, /* Second is hotplugged later */
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 1, /* Second is hotplugged later */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_NORMAL,
        }
      },
      .n_logical_monitors = 1, /* Second is hotplugged later */
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = -1,
          .transform = META_MONITOR_TRANSFORM_NORMAL,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  MetaOrientation i;
  unsigned int times_signalled = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       "test-touchscreen",
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  /*
   * The first part of this test emulate the following:
   *  1) Start with the lid open
   *  2) Rotate the device in all directions
   *  3) Connect external monitor
   *  4) Rotate the device in all directions
   *  5) Close lid
   */

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);

  emulate_hotplug (test_setup);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      meta_wait_for_orientation (orientation_manager, i, &times_signalled);
      g_assert_cmpuint (times_signalled, <=, 1);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));
    }

  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);
  meta_wait_for_orientation (orientation_manager, META_ORIENTATION_NORMAL,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  g_test_message ("External monitor connected");
  test_case.setup.n_outputs = 2;
  test_case.expect.n_outputs = 2;
  test_case.expect.n_monitors = 2;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  /* Rotate the monitor in all the directions */
  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      meta_wait_for_orientation (orientation_manager, i, &times_signalled);
      g_assert_cmpuint (times_signalled, <=, 1);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));
    }

  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);
  meta_wait_for_orientation (orientation_manager, META_ORIENTATION_NORMAL,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  g_test_message ("Lid closed");
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.logical_monitors[0].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].x = 0;
  test_case.expect.screen_width = 1024;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  emulate_hotplug (test_setup);

  /* Rotate the monitor in all the directions */
  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      meta_wait_for_orientation (orientation_manager, i, &times_signalled);
      g_assert_cmpuint (times_signalled, <=, 1);
      meta_check_monitor_configuration (test_context,
                                        &test_case.expect);
    }

  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);
  meta_wait_for_orientation (orientation_manager, META_ORIENTATION_NORMAL,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);

  /*
   * The second part of this test emulate the following at each device rotation:
   *  1) Open lid
   *  2) Close lid
   *  3) Change orientation
   *  4) Reopen the lid
   *  2) Disconnect external monitor
   */

  g_test_message ("Lid opened");
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.logical_monitors[0].monitors[0] = 0,
  test_case.expect.logical_monitors[1].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  emulate_hotplug (test_setup);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      g_test_message ("Closing lid");
      test_case.expect.monitors[0].current_mode = -1;
      test_case.expect.logical_monitors[0].monitors[0] = 1,
      test_case.expect.n_logical_monitors = 1;
      test_case.expect.crtcs[0].current_mode = -1;
      test_case.expect.crtcs[1].x = 0;
      test_case.expect.screen_width = 1024;

      test_setup = meta_create_monitor_test_setup (test_backend,
                                                   &test_case.setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
      emulate_hotplug (test_setup);

      /* Change orientation */
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      meta_wait_for_orientation (orientation_manager, i, &times_signalled);
      g_assert_cmpuint (times_signalled, <=, 1);
      meta_check_monitor_configuration (test_context,
                                        &test_case.expect);

      g_test_message ("Opening lid");
      test_case.expect.monitors[0].current_mode = 0;
      test_case.expect.logical_monitors[0].monitors[0] = 0,
      test_case.expect.logical_monitors[1].monitors[0] = 1,
      test_case.expect.n_logical_monitors = 2;
      test_case.expect.crtcs[0].current_mode = 0;
      test_case.expect.crtcs[1].x = 1024;

      test_setup = meta_create_monitor_test_setup (test_backend,
                                                   &test_case.setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
      emulate_hotplug (test_setup);

      /* We don't actually expect the orientation to change here, so we
       * just wait for a moment (so that if the orientation *did* change,
       * mutter has had a chance to process it), and then continue. */
      meta_wait_for_possible_orientation_change (orientation_manager,
                                                 &times_signalled);
      g_assert_cmpuint (times_signalled, ==, 0);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));

      g_test_message ("External monitor disconnected");
      test_case.setup.n_outputs = 1;
      test_case.expect.n_outputs = 1;
      test_case.expect.n_monitors = 1;
      test_case.expect.n_logical_monitors = 1;
      test_case.expect.crtcs[1].current_mode = -1;

      test_setup = meta_create_monitor_test_setup (test_backend,
                                                   &test_case.setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      emulate_hotplug (test_setup);
      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));

      g_test_message ("External monitor connected");
      test_case.setup.n_outputs = 2;
      test_case.expect.n_outputs = 2;
      test_case.expect.n_monitors = 2;
      test_case.expect.n_logical_monitors = 2;
      test_case.expect.crtcs[1].current_mode = 0;
      test_case.expect.crtcs[1].x = 1024;

      test_setup = meta_create_monitor_test_setup (test_backend,
                                                   &test_case.setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      emulate_hotplug (test_setup);
      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));
    }

  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);
  meta_wait_for_orientation (orientation_manager, META_ORIENTATION_NORMAL,
                             &times_signalled);
  g_assert_cmpuint (times_signalled, <=, 1);
}

static void
meta_test_monitor_custom_vertical_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 800,
          .height = 600,
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
          .current_mode = 0
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 768, .width = 800, .height = 600 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 1,
          .y = 768,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768 + 600
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "vertical.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_primary_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 800,
          .height = 600,
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
          .current_mode = 0
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 1,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 1,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024 + 800,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "primary.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_underscanning_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
          .is_underscanning = TRUE,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "underscanning.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_scale_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1920,
              .height = 1080,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 960, .height = 540 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 960,
      .screen_height = 540
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "scale.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_fractional_scale_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1200,
          .height = 900,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1200,
              .height = 900,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1.5
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 800,
      .screen_height = 600
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "fractional-scale.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_high_precision_fractional_scale_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 744, .height = 558 },
          .scale = 1024.0/744.0 /* 1.3763440847396851 */
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 744,
      .screen_height = 558
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context,
                                  "high-precision-fractional-scale.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_tiled_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          },
          .serial = "0x123456",
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          },
          .serial = "0x123456",
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0,
                },
                {
                  .output = 1,
                  .crtc_mode = 0,
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 400, .height = 300 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
          .x = 200,
          .y = 0
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 400,
      .screen_height = 300
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "tiled.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_tiled_custom_resolution_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 400,
          .height = 600,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 640,
          .height = 480,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          },
          .serial = "0x123456",
        },
        {
          .crtc = -1,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          },
          .serial = "0x123456",
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0,
                },
                {
                  .output = 1,
                  .crtc_mode = 0,
                }
              }
            },
            {
              .width = 640,
              .height = 480,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1,
                },
                {
                  .output = 1,
                  .crtc_mode = -1,
                }
              }
            }
          },
          .n_modes = 2,
          .current_mode = 1,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 320, .height = 240 },
          .scale = 2
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 1,
        },
        {
          .current_mode = -1,
          .x = 400,
          .y = 0,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 320,
      .screen_height = 240
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "tiled-custom-resolution.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_tiled_non_preferred_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 640,
          .height = 480,
          .refresh_rate = 60.0
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 512,
          .height = 768,
          .refresh_rate = 120.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
      },
      .n_modes = 4,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 2 },
          .n_modes = 2,
          .preferred_mode = 1,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 512,
            .tile_h = 768
          },
          .serial = "0x123456",
        },
        {
          .crtc = -1,
          .modes = { 1, 2, 3 },
          .n_modes = 3,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 512,
            .tile_h = 768
          },
          .serial = "0x123456",
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0, 1 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 120.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 2
                },
                {
                  .output = 1,
                  .crtc_mode = 2,
                }
              }
            },
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = -1
                },
                {
                  .output = 1,
                  .crtc_mode = 1,
                }
              }
            },
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = -1
                },
                {
                  .output = 1,
                  .crtc_mode = 3,
                }
              }
            },
          },
          .n_modes = 3,
          .current_mode = 1,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1,
        },
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context,
                                  "non-preferred-tiled-custom-resolution.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_mirrored_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .serial = "0x123456a",
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
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
          .current_mode = 0
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0, 1 },
          .n_monitors = 2,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 800,
      .screen_height = 600
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "mirrored.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_first_rotated_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .serial = "0x123456a",
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456b",
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 768, .height = 1024 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_270
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 768, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_270
        },
        {
          .current_mode = 0,
          .x = 768,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768 + 1024,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "first-rotated.xml");
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_second_rotated_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .serial = "0x123456a",
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456b",
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 256, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 768, .height = 1024 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .y = 256,
        },
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_90,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768 + 1024,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "second-rotated.xml");
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_second_rotated_tiled_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 400,
          .height = 600,
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
          .crtc = -1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1, 2 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          },
          .serial = "0x123456b",
        },
        {
          .crtc = -1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1, 2 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          },
          .serial = "0x123456b",
        }
      },
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 3
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1, 2 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1,
                },
                {
                  .output = 2,
                  .crtc_mode = 1,
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 256, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 600, .height = 800 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = 0,
          .y = 256,
        },
        {
          .current_mode = 1,
          .transform = META_MONITOR_TRANSFORM_90,
          .x = 1024,
          .y = 0,
        },
        {
          .current_mode = 1,
          .transform = META_MONITOR_TRANSFORM_90,
          .x = 1024,
          .y = 400,
        }
      },
      .n_crtcs = 3,
      .n_tiled_monitors = 1,
      .screen_width = 1024 + 600,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    TRUE);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "second-rotated-tiled.xml");
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_second_rotated_nonnative_tiled_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 400,
          .height = 600,
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
          .crtc = -1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1, 2 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 0,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          },
          .serial = "0x123456b",
        },
        {
          .crtc = -1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
          .possible_crtcs = { 1, 2 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .tile_info = {
            .group_id = 1,
            .max_h_tiles = 2,
            .max_v_tiles = 1,
            .loc_h_tile = 1,
            .loc_v_tile = 0,
            .tile_w = 400,
            .tile_h = 600
          },
          .serial = "0x123456b",
        }
      },
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 3
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1, 2 },
          .n_outputs = 2,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 1,
                },
                {
                  .output = 2,
                  .crtc_mode = 1,
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 256, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 600, .height = 800 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = 0,
          .y = 256,
        },
        {
          .current_mode = 1,
          .transform = META_MONITOR_TRANSFORM_90,
          .x = 1024,
          .y = 0,
        },
        {
          .current_mode = 1,
          .transform = META_MONITOR_TRANSFORM_90,
          .x = 1024,
          .y = 400,
        }
      },
      .n_crtcs = 3,
      .n_tiled_monitors = 1,
      .screen_width = 1024 + 600,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    FALSE);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "second-rotated-tiled.xml");
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_second_rotated_nonnative_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
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
          .height_mm = 125,
          .serial = "0x123456a",
        },
        {
          .crtc = 1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456b",
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
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 256, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 768, .height = 1024 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_logical_monitors = 2,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
          .y = 256,
        },
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_90,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768 + 1024,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    FALSE);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "second-rotated.xml");
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_interlaced_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531,
          .flags = META_CRTC_MODE_FLAG_INTERLACE,
        }
      },
      .n_modes = 2,
      .outputs = {
        {
          .crtc = 0,
          .modes = { 0, 1 },
          .n_modes = 2,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .flags = META_CRTC_MODE_FLAG_NONE,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0,
                },
              }
            },
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .flags = META_CRTC_MODE_FLAG_INTERLACE,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1,
                }
              }
            }
          },
          .n_modes = 2,
          .current_mode = 1,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 1,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "interlaced.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_oneoff (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456",
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0, 1 },
          .n_possible_crtcs = 2,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x654321"
        }
      },
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = -1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 2,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_NORMAL
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 2,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = -1,
        }
      },
      .n_crtcs = 2,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "oneoff.xml");
  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_lid_switch_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .is_laptop_panel = TRUE,
          .serial = "0x123456a",
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456b",
        }
      },
      .n_outputs = 1, /* Second one hot plugged later */
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1, /* Second one hot plugged later */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 768, .height = 1024 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_270
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 768, .height = 1024 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1, /* Second one hot plugged later */
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_270
        },
        {
          .current_mode = -1,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "lid-switch.xml");
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  /* External monitor connected */

  test_case.setup.n_outputs = 2;
  test_case.expect.n_monitors = 2;
  test_case.expect.n_outputs = 2;
  test_case.expect.crtcs[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.crtcs[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.logical_monitors[0].layout =
    (MtkRectangle) { .width = 1024, .height = 768 };
  test_case.expect.logical_monitors[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.logical_monitors[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 + 768;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  /* Lid was closed */

  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].transform = META_MONITOR_TRANSFORM_90;
  test_case.expect.crtcs[1].x = 0;
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.logical_monitors[0].layout =
    (MtkRectangle) { .width = 768, .height = 1024 };
  test_case.expect.logical_monitors[0].monitors[0] = 1;
  test_case.expect.logical_monitors[0].transform = META_MONITOR_TRANSFORM_90;
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 768;
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  /* Lid was opened */

  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.logical_monitors[0].layout =
    (MtkRectangle) { .width = 1024, .height = 768 };
  test_case.expect.logical_monitors[0].monitors[0] = 0;
  test_case.expect.logical_monitors[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.logical_monitors[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 + 768;
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();
}

static void
meta_test_monitor_migrated_rotated (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456",
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 600, .height = 800 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_270
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_270
        }
      },
      .n_crtcs = 1,
      .screen_width = 600,
      .screen_height = 800,
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  g_autofree char *migrated_path = NULL;
  const char *old_config_path;
  g_autoptr (GFile) old_config_file = NULL;
  GError *error = NULL;
  const char *expected_path;
  g_autofree char *migrated_data = NULL;
  g_autofree char *expected_data = NULL;
  g_autoptr (GFile) migrated_file = NULL;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);
  if (!meta_monitor_config_store_set_custom (config_store,
                                             "/dev/null",
                                             migrated_path,
                                             META_MONITORS_CONFIG_FLAG_NONE,
                                             &error))
    g_error ("Failed to set custom config store files: %s", error->message);

  old_config_path = g_test_get_filename (G_TEST_DIST,
                                         "tests", "migration",
                                         "rotated-old.xml",
                                         NULL);
  old_config_file = g_file_new_for_path (old_config_path);
  if (!meta_migrate_old_monitors_config (config_store,
                                         old_config_file,
                                         &error))
    g_error ("Failed to migrate config: %s", error->message);

  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "tests", "migration",
                                       "rotated-new-finished.xml",
                                       NULL);
  expected_data = meta_read_file (expected_path);
  migrated_data = meta_read_file (migrated_path);

  g_assert_nonnull (expected_data);
  g_assert_nonnull (migrated_data);

  g_assert (strcmp (expected_data, migrated_data) == 0);

  migrated_file = g_file_new_for_path (migrated_path);
  if (!g_file_delete (migrated_file, NULL, &error))
    g_error ("Failed to remove test data output file: %s", error->message);
}

static void
meta_test_monitor_migrated_wiggle_discard (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 59.0
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456",
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 59.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 800, .height = 600 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_NORMAL
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .screen_width = 800,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  g_autofree char *migrated_path = NULL;
  const char *old_config_path;
  g_autoptr (GFile) old_config_file = NULL;
  GError *error = NULL;
  const char *expected_path;
  g_autofree char *migrated_data = NULL;
  g_autofree char *expected_data = NULL;
  g_autoptr (GFile) migrated_file = NULL;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);
  if (!meta_monitor_config_store_set_custom (config_store,
                                             "/dev/null",
                                             migrated_path,
                                             META_MONITORS_CONFIG_FLAG_NONE,
                                             &error))
    g_error ("Failed to set custom config store files: %s", error->message);

  old_config_path = g_test_get_filename (G_TEST_DIST,
                                         "tests", "migration",
                                         "wiggle-old.xml",
                                         NULL);
  old_config_file = g_file_new_for_path (old_config_path);
  if (!meta_migrate_old_monitors_config (config_store,
                                         old_config_file,
                                         &error))
    g_error ("Failed to migrate config: %s", error->message);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Failed to finish monitors config migration: "
                         "Mode not available on monitor");
  emulate_hotplug (test_setup);
  g_test_assert_expected_messages ();

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "tests", "migration",
                                       "wiggle-new-discarded.xml",
                                       NULL);
  expected_data = meta_read_file (expected_path);
  migrated_data = meta_read_file (migrated_path);

  g_assert_nonnull (expected_data);
  g_assert_nonnull (migrated_data);

  g_assert (strcmp (expected_data, migrated_data) == 0);

  migrated_file = g_file_new_for_path (migrated_path);
  if (!g_file_delete (migrated_file, NULL, &error))
    g_error ("Failed to remove test data output file: %s", error->message);
}

static gboolean
quit_main_loop (gpointer data)
{
  GMainLoop *loop = data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static void
dispatch (void)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaLaters *laters = meta_compositor_get_laters (compositor);
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);
  meta_laters_add (laters, META_LATER_BEFORE_REDRAW,
                   quit_main_loop,
                   loop,
                   NULL);
  g_main_loop_run (loop);
}

static MetaTestClient *
create_test_window (MetaContext *context,
                    const char  *window_name)
{
  MetaTestClient *test_client;
  static int client_count = 0;
  g_autofree char *client_name = NULL;
  g_autoptr (GError) error = NULL;

  client_name = g_strdup_printf ("test_client_%d", client_count++);
  test_client = meta_test_client_new (context,
                                      client_name, META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  if (!test_client)
    g_error ("Failed to launch test client: %s", error->message);

  if (!meta_test_client_do (test_client, &error,
                            "create", window_name,
                            NULL))
    g_error ("Failed to create window: %s", error->message);

  return test_client;
}

static void
meta_test_monitor_wm_tiling (void)
{
  MetaContext *context = test_context;
  MonitorTestCase test_case = initial_test_case;
  MetaMonitorTestSetup *test_setup;
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  /*
   * 1) Start with two monitors connected.
   * 2) Tile it on the second monitor.
   * 3) Unplug both monitors.
   * 4) Replug in first monitor.
   */

  const char *test_window_name= "window1";
  test_client = create_test_window (context, test_window_name);

  if (!meta_test_client_do (test_client, &error,
                            "show", test_window_name,
                            NULL))
    g_error ("Failed to show the window: %s", error->message);

  MetaWindow *test_window =
    meta_test_client_find_window (test_client,
                                  test_window_name,
                                  &error);
  if (!test_window)
    g_error ("Failed to find the window: %s", error->message);
  meta_test_client_wait_for_window_shown (test_client, test_window);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);
  meta_window_move_to_monitor (test_window, 1);
  check_test_client_state (test_client);

  test_case.setup.n_outputs = 0;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  test_case.setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  dispatch ();

  /*
   * 1) Start with two monitors connected.
   * 2) Tile a window on the second monitor.
   * 3) Untile window.
   * 4) Unplug monitor.
   * 5) Tile window again.
   */

  test_case.setup.n_outputs = 2;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  meta_window_move_to_monitor (test_window, 1);
  meta_window_tile (test_window, META_TILE_NONE);

  test_case.setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);

  meta_test_client_destroy (test_client);
}

static void
meta_test_monitor_migrated_wiggle (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456",
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 600, .height = 800 },
          .scale = 1,
          .transform = META_MONITOR_TRANSFORM_90
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
          .transform = META_MONITOR_TRANSFORM_90
        }
      },
      .n_crtcs = 1,
      .screen_width = 600,
      .screen_height = 800,
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  g_autofree char *migrated_path = NULL;
  const char *old_config_path;
  g_autoptr (GFile) old_config_file = NULL;
  GError *error = NULL;
  const char *expected_path;
  g_autofree char *migrated_data = NULL;
  g_autofree char *expected_data = NULL;
  g_autoptr (GFile) migrated_file = NULL;

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);
  if (!meta_monitor_config_store_set_custom (config_store,
                                             "/dev/null",
                                             migrated_path,
                                             META_MONITORS_CONFIG_FLAG_NONE,
                                             &error))
    g_error ("Failed to set custom config store files: %s", error->message);

  old_config_path = g_test_get_filename (G_TEST_DIST,
                                         "tests", "migration",
                                         "wiggle-old.xml",
                                         NULL);
  old_config_file = g_file_new_for_path (old_config_path);
  if (!meta_migrate_old_monitors_config (config_store,
                                         old_config_file,
                                         &error))
    g_error ("Failed to migrate config: %s", error->message);

  emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  check_monitor_test_clients_state ();

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "tests", "migration",
                                       "wiggle-new-finished.xml",
                                       NULL);
  expected_data = meta_read_file (expected_path);
  migrated_data = meta_read_file (migrated_path);

  g_assert_nonnull (expected_data);
  g_assert_nonnull (migrated_data);

  g_assert (strcmp (expected_data, migrated_data) == 0);

  migrated_file = g_file_new_for_path (migrated_path);
  if (!g_file_delete (migrated_file, NULL, &error))
    g_error ("Failed to remove test data output file: %s", error->message);
}

static void
meta_test_monitor_supported_integer_scales (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .n_modes = 21,
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
        {
          .width = 1280,
          .height = 720,
          .refresh_rate = 60.0,
        },
        {
          .width = 1280,
          .height = 800,
          .refresh_rate = 60.0,
        },
        {
          .width = 1280,
          .height = 1024,
          .refresh_rate = 60.0,
        },
        {
          .width = 1366,
          .height = 768,
          .refresh_rate = 60.0,
        },
        {
          .width = 1440,
          .height = 900,
          .refresh_rate = 60.0,
        },
        {
          .width = 1400,
          .height = 1050,
          .refresh_rate = 60.0,
        },
        {
          .width = 1600,
          .height = 900,
          .refresh_rate = 60.0,
        },
        {
          .width = 1920,
          .height = 1080,
          .refresh_rate = 60.0,
        },
        {
          .width = 1920,
          .height = 1200,
          .refresh_rate = 60.0,
        },
        {
          .width = 2650,
          .height = 1440,
          .refresh_rate = 60.0,
        },
        {
          .width = 2880,
          .height = 1800,
          .refresh_rate = 60.0,
        },
        {
          .width = 3200,
          .height = 1800,
          .refresh_rate = 60.0,
        },
        {
          .width = 3200,
          .height = 2048,
          .refresh_rate = 60.0,
        },
        {
          .width = 3840,
          .height = 2160,
          .refresh_rate = 60.0,
        },
        {
          .width = 3840,
          .height = 2400,
          .refresh_rate = 60.0,
        },
        {
          .width = 4096,
          .height = 2160,
          .refresh_rate = 60.0,
        },
        {
          .width = 4096,
          .height = 3072,
          .refresh_rate = 60.0,
        },
        {
          .width = 5120,
          .height = 2880,
          .refresh_rate = 60.0,
        },
        {
          .width = 7680,
          .height = 4320,
          .refresh_rate = 60.0,
        },
      },
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                     17, 18, 19, 20 },
          .n_modes = 21,
          .preferred_mode = 5,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .n_monitors = 1,
      .monitors = {
        {
          .n_modes = 21,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .n_scales = 1,
              .scales = { 1.0 },
            },
            {
              .width = 1024,
              .height = 768,
              .n_scales = 1,
              .scales = { 1.0 },
            },
            {
              .width = 1280,
              .height = 720,
              .n_scales = 1,
              .scales = { 1.0 },
            },
            {
              .width = 1280,
              .height = 800,
              .n_scales = 1,
              .scales = { 1.0 },
            },
            {
              .width = 1280,
              .height = 1024,
              .n_scales = 1,
              .scales = { 1.0 },
            },
            {
              .width = 1366,
              .height = 768,
              .n_scales = 1,
              .scales = { 1.0 },
            },
            {
              .width = 1440,
              .height = 900,
              .n_scales = 1,
              .scales = { 1.0 },
            },
            {
              .width = 1400,
              .height = 1050,
              .n_scales = 1,
              .scales = { 1.0 },
            },
            {
              .width = 1600,
              .height = 900,
              .n_scales = 1,
              .scales = { 1.0 },
            },
            {
              .width = 1920,
              .height = 1080,
              .n_scales = 2,
              .scales = { 1.0, 2.0 },
            },
            {
              .width = 1920,
              .height = 1200,
              .n_scales = 2,
              .scales = { 1.0, 2.0 },
            },
            {
              .width = 2650,
              .height = 1440,
              .n_scales = 3,
              .scales = { 1.0, 2.0, 3.0 },
            },
            {
              .width = 2880,
              .height = 1800,
              .n_scales = 3,
              .scales = { 1.0, 2.0, 3.0 },
            },
            {
              .width = 3200,
              .height = 1800,
              .n_scales = 3,
              .scales = { 1.0, 2.0, 3.0 },
            },
            {
              .width = 3200,
              .height = 2048,
              .n_scales = 4,
              .scales = { 1.0, 2.0, 3.0, 4.0 },
            },
            {
              .width = 3840,
              .height = 2160,
              .n_scales = 4,
              .scales = { 1.0, 2.0, 3.0, 4.0 },
            },
            {
              .width = 3840,
              .height = 2400,
              .n_scales = 4,
              .scales = { 1.0, 2.0, 3.0, 4.0 },
            },
            {
              .width = 4096,
              .height = 2160,
              .n_scales = 4,
              .scales = { 1.0, 2.0, 3.0, 4.0 },
            },
            {
              .width = 4096,
              .height = 3072,
              .n_scales = 4,
              .scales = { 1.0, 2.0, 3.0, 4.0 },
            },
            {
              .width = 5120,
              .height = 2880,
              .n_scales = 4,
              .scales = { 1.0, 2.0, 3.0, 4.0 },
            },
            {
              .width = 7680,
              .height = 4320,
              .n_scales = 4,
              .scales = { 1.0, 2.0, 3.0, 4.0 },
            },
          },
        },
      },
    },
  };

  MetaMonitorTestSetup *test_setup;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor scaling values",
                      meta_check_monitor_scales (test_context,
                                                 &test_case.expect,
                                                 META_MONITOR_SCALES_CONSTRAINT_NO_FRAC));
}

static void
meta_test_monitor_supported_fractional_scales (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .n_modes = 21,
      .modes = {
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
        {
          .width = 1280,
          .height = 720,
          .refresh_rate = 60.0,
        },
        {
          .width = 1280,
          .height = 800,
          .refresh_rate = 60.0,
        },
        {
          .width = 1280,
          .height = 1024,
          .refresh_rate = 60.0,
        },
        {
          .width = 1366,
          .height = 768,
          .refresh_rate = 60.0,
        },
        {
          .width = 1440,
          .height = 900,
          .refresh_rate = 60.0,
        },
        {
          .width = 1400,
          .height = 1050,
          .refresh_rate = 60.0,
        },
        {
          .width = 1600,
          .height = 900,
          .refresh_rate = 60.0,
        },
        {
          .width = 1920,
          .height = 1080,
          .refresh_rate = 60.0,
        },
        {
          .width = 1920,
          .height = 1200,
          .refresh_rate = 60.0,
        },
        {
          .width = 2650,
          .height = 1440,
          .refresh_rate = 60.0,
        },
        {
          .width = 2880,
          .height = 1800,
          .refresh_rate = 60.0,
        },
        {
          .width = 3200,
          .height = 1800,
          .refresh_rate = 60.0,
        },
        {
          .width = 3200,
          .height = 2048,
          .refresh_rate = 60.0,
        },
        {
          .width = 3840,
          .height = 2160,
          .refresh_rate = 60.0,
        },
        {
          .width = 3840,
          .height = 2400,
          .refresh_rate = 60.0,
        },
        {
          .width = 4096,
          .height = 2160,
          .refresh_rate = 60.0,
        },
        {
          .width = 4096,
          .height = 3072,
          .refresh_rate = 60.0,
        },
        {
          .width = 5120,
          .height = 2880,
          .refresh_rate = 60.0,
        },
        {
          .width = 7680,
          .height = 4320,
          .refresh_rate = 60.0,
        },
      },
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                     17, 18, 19, 20 },
          .n_modes = 21,
          .preferred_mode = 5,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .n_monitors = 1,
      .monitors = {
        {
          .n_modes = 21,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .n_scales = 1,
              .scales = { 1.000000 },
            },
            {
              .width = 1024,
              .height = 768,
              .n_scales = 2,
              .scales = { 1.000000, 1.24878049 },
            },
            {
              .width = 1280,
              .height = 720,
              .n_scales = 3,
              .scales = { 1.000000, 1.250000, 1.509434 },
            },
            {
              .width = 1280,
              .height = 800,
              .n_scales = 3,
              .scales = { 1.000000, 1.250000, 1.495327 },
            },
            {
              .width = 1280,
              .height = 1024,
              .n_scales = 4,
              .scales = { 1.000000, 1.248780, 1.497076, 1.753425 },
            },
            {
              .width = 1366,
              .height = 768,
              .n_scales = 1,
              .scales = { 1.000000 },
            },
            {
              .width = 1440,
              .height = 900,
              .n_scales = 4,
              .scales = { 1.000000, 1.250000, 1.500000, 1.747573 },
            },
            {
              .width = 1400,
              .height = 1050,
              .n_scales = 4,
              .scales = { 1.000000, 1.250000, 1.502146, 1.750000 },
            },
            {
              .width = 1600,
              .height = 900,
              .n_scales = 4,
              .scales = { 1.000000, 1.250000, 1.492537, 1.754386 },
            },
            {
              .width = 1920,
              .height = 1080,
              .n_scales = 6,
              .scales = { 1.000000, 1.250000, 1.500000, 1.739130, 2.000000,
                          2.307692 },
            },
            {
              .width = 1920,
              .height = 1200,
              .n_scales = 6,
              .scales = { 1.000000, 1.250000, 1.500000, 1.751825, 2.000000,
                          2.242991 },
            },
            {
              .width = 2650,
              .height = 1440,
              .n_scales = 6,
              .scales = { 1.000000, 1.250000, 1.428571, 1.666667, 2.000000,
                          2.500000
              },
            },
            {
              .width = 2880,
              .height = 1800,
              .n_scales = 11,
              .scales = { 1.000000, 1.250000, 1.500000, 1.747573, 2.000000,
                          2.250000, 2.500000, 2.748092, 3.000000, 3.243243,
                          3.495146
              },
            },
            {
              .width = 3200,
              .height = 1800,
              .n_scales = 12,
              .scales = { 1.000000, 1.250000, 1.503759, 1.754386, 2.000000,
                          2.247191, 2.500000, 2.739726, 2.985075, 3.225806,
                          3.508772, 3.773585
              },
            },
            {
              .width = 3200,
              .height = 2048,
              .n_scales = 13,
              .scales = { 1.000000, 1.254902, 1.505882, 1.753425, 2.000000,
                          2.245614, 2.509804, 2.723404, 2.976744, 3.282051,
                          3.459460, 3.764706, 4.000000,
              },
            },
            {
              .width = 3840,
              .height = 2160,
              .n_scales = 13,
              .scales = { 1.000000, 1.250000, 1.500000, 1.751825, 2.000000,
                          2.201835, 2.500000, 2.758621, 3.000000, 3.243243,
                          3.478261, 3.750000, 4.000000
              },
            },
            {
              .width = 3840,
              .height = 2400,
              .n_scales = 13,
              .scales = { 1.000000, 1.250000, 1.500000, 1.751825, 2.000000,
                          2.253521, 2.500000, 2.742857, 3.000000, 3.243243,
                          3.503650, 3.750000, 4.000000
              },
            },
            {
              .width = 4096,
              .height = 2160,
              .n_scales = 8,
              .scales = { 1.000000, 1.333333, 1.454545, 1.777778, 2.000000,
                          2.666667, 3.200000, 4.000000
              }
            },
            {
              .width = 4096,
              .height = 3072,
              .n_scales = 13,
              .scales = { 1.000000, 1.250305, 1.499268, 1.750427, 2.000000,
                          2.245614, 2.497561, 2.752688, 3.002933, 3.250794,
                          3.494880, 3.750916, 4.000000
              },
            },
            {
              .width = 5120,
              .height = 2880,
              .n_scales = 13,
              .scales = { 1.000000, 1.250000, 1.495327, 1.748634, 2.000000,
                          2.253521, 2.500000, 2.758621, 2.990654, 3.265306,
                          3.516484, 3.764706, 4.000000
              },
            },
            {
              .width = 7680,
              .height = 4320,
              .n_scales = 13,
              .scales = { 1.000000, 1.250000, 1.500000, 1.751825, 2.000000,
                          2.211982, 2.500000, 2.742857, 3.000000, 3.243243,
                          3.503650, 3.750000, 4.000000
              },
            },
          },
        },
      },
    },
  };

  MetaMonitorTestSetup *test_setup;
  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor scaling values",
                      meta_check_monitor_scales (test_context,
                                                 &test_case.expect,
                                                 META_MONITOR_SCALES_CONSTRAINT_NONE));
}

static void
meta_test_monitor_calculate_mode_scale (void)
{
  static MonitorTestCaseSetup base_test_case_setup = {
    .modes = {
      {
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
        .scale = -1,
      }
    },
    .n_outputs = 1,
    .crtcs = {
      {
        .current_mode = 0
      }
    },
    .n_crtcs = 1
  };

  static struct {
    const char *name;
    int width, height;
    int width_mm, height_mm;
    float exp, exp_nofrac;
  } cases[] = {
    {
      .name = "Librem 5",
      .width = 720,
      .height = 1440,
      .width_mm = 65, /* 2:1, 5.7" */
      .height_mm = 129,
      /* Librem 5, when scaled, doesn't have enough logical area to
         fit a full desktop-sized GNOME UI. Thus, Mutter rules out
         scale factors above 1.75. */
      .exp = 1.5,
      .exp_nofrac = 1.0,
    },
    {
       .name = "OnePlus 6",
       .width = 1080,
       .height = 2280,
       .width_mm = 68, /* 19:9, 6.28" */
       .height_mm = 144,
       .exp = 2.5,
       .exp_nofrac = 2.0,
    },
    {
      .name = "Google Pixel 6a",
      .width = 1080,
      .height = 2400,
      .width_mm = 64, /* 20:9, 6.1" */
      .height_mm = 142,
      .exp = 2.5,
      .exp_nofrac = 2.0,
    },
    {
      .name = "13\" MacBook Retina",
      .width = 2560,
      .height = 1600,
      .width_mm = 286, /* 16:10, 13.3" */
      .height_mm = 179,
      .exp = 1.75,
      .exp_nofrac = 2.0,
    },
    {
      .name = "Surface Laptop Studio",
      .width = 2400,
      .height = 1600,
      .width_mm = 303, /* 3:2 @ 14.34" */
      .height_mm = 202,
      .exp = 1.5,
      .exp_nofrac = 1.0,
    },
    {
      .name = "Dell XPS 9320",
      .width = 3840,
      .height = 2400,
      .width_mm = 290,
      .height_mm = 180,
      .exp = 2.5,
      .exp_nofrac = 2.0,
    },
    {
      .name = "Lenovo ThinkPad X1 Yoga Gen 6",
      .width = 3840,
      .height = 2400,
      .width_mm = 300,
      .height_mm = 190,
      .exp = 2.5,
      .exp_nofrac = 2.0,
    },
    {
      .name = "Generic 23\" 1080p",
      .width = 1920,
      .height = 1080,
      .width_mm = 509,
      .height_mm = 286,
      .exp = 1.0,
      .exp_nofrac = 1.0,
    },
    {
      .name = "Generic 23\" 4K",
      .width = 3840,
      .height = 2160,
      .width_mm = 509,
      .height_mm = 286,
      .exp = 1.75,
      .exp_nofrac = 2.0,
    },
    {
      .name = "Generic 27\" 4K",
      .width = 3840,
      .height = 2160,
      .width_mm = 598,
      .height_mm = 336,
      .exp = 1.5,
      .exp_nofrac = 1.0,
    },
    {
      .name = "Generic 32\" 4K",
      .width = 3840,
      .height = 2160,
      .width_mm = 708,
      .height_mm = 398,
      .exp = 1.25,
      .exp_nofrac = 1.0,
    },
  };
  /* Set a rather high scale epsilon, to have "easy" scales as the
   * expectations, while ignoring that the actual scaling factors are slightly
   * different, e.g. 1.74863386 instead of 1.75.
   */
  const float scale_epsilon = 0.2;

  MetaMonitorManager *manager;
  MetaMonitorManagerTest *manager_test;

  manager = meta_backend_get_monitor_manager (test_backend);
  manager_test = META_MONITOR_MANAGER_TEST (manager);

  for (int i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      MonitorTestCaseSetup test_case_setup = base_test_case_setup;
      MetaMonitorTestSetup *test_setup;
      MetaLogicalMonitor *logical_monitor;
      g_autofree char *serial1 = NULL;
      g_autofree char *serial2 = NULL;

      serial1 = g_strdup_printf ("0x120001%x", i * 2);
      test_case_setup.modes[0].width = cases[i].width;
      test_case_setup.modes[0].height = cases[i].height;
      test_case_setup.outputs[0].width_mm = cases[i].width_mm;
      test_case_setup.outputs[0].height_mm = cases[i].height_mm;
      test_case_setup.outputs[0].serial = serial1;
      test_setup = meta_create_monitor_test_setup (test_backend, &test_case_setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);

      g_debug ("Checking default non-fractional scale for %s", cases[i].name);
      meta_monitor_manager_test_set_layout_mode (manager_test,
                                                 META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL);
      emulate_hotplug (test_setup);
      /* Crashes right here because manager->logical_monitors is NULL */
      logical_monitor = manager->logical_monitors->data;
      g_assert_cmpfloat_with_epsilon (logical_monitor->scale, cases[i].exp_nofrac, 0.01);

      g_debug ("Checking default fractional scale for %s", cases[i].name);
      meta_monitor_manager_test_set_layout_mode (manager_test,
                                                 META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL);

      serial2 = g_strdup_printf ("0x120001%x", i * 2 + 1);
      test_case_setup.outputs[0].serial = serial2;
      test_setup = meta_create_monitor_test_setup (test_backend, &test_case_setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      emulate_hotplug (test_setup);
      logical_monitor = manager->logical_monitors->data;
      g_assert_cmpfloat_with_epsilon (logical_monitor->scale, cases[i].exp,
                                      scale_epsilon);
    }
}

static void
meta_test_monitor_policy_system_only (void)
{
  MetaMonitorTestSetup *test_setup;
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 640,
          .height = 480,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 3,
      .outputs = {
         {
          .crtc = 0,
          .modes = { 0, 1, 2 },
          .n_modes = 3,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            },
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1
                }
              }
            },
            {
              .width = 640,
              .height = 480,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 2
                }
              }
            }
          },
          .n_modes = 3,
          .current_mode = 2,
          .width_mm = 222,
          .height_mm = 125
        },
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 640, .height = 480 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 2,
          .x = 0,
        }
      },
      .n_crtcs = 1,
      .screen_width = 640,
      .screen_height = 480,
    }
  };
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);

  test_setup = meta_create_monitor_test_setup (test_backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);

  meta_monitor_config_store_reset (config_store);
  emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
}

static void
test_case_setup (void       **fixture,
                 const void   *data)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    TRUE);
  meta_monitor_config_manager_set_current (config_manager, NULL);
  meta_monitor_config_manager_clear_history (config_manager);
}

static void
add_monitor_test (const char *test_path,
                  GTestFunc   test_func)
{
  g_test_add (test_path, gpointer, NULL,
              test_case_setup,
              (void (* ) (void **, const void *)) test_func,
              NULL);
}

static MetaMonitorTestSetup *
create_initial_test_setup (MetaBackend *backend)
{
  return meta_create_monitor_test_setup (backend,
                                         &initial_test_case.setup,
                                         MONITOR_TEST_FLAG_NO_STORED);
}

static void
init_monitor_tests (void)
{
  meta_init_monitor_test_setup (create_initial_test_setup);

  add_monitor_test ("/backends/monitor/config-store/set_current-on-empty",
                    meta_test_monitor_config_store_set_current_on_empty);
  add_monitor_test ("/backends/monitor/config-store/set_current-with-parent-on-empty",
                    meta_test_monitor_config_store_set_current_with_parent_on_empty);
  add_monitor_test ("/backends/monitor/config-store/set_current",
                    meta_test_monitor_config_store_set_current);
  add_monitor_test ("/backends/monitor/config-store/set_current-with-parent",
                    meta_test_monitor_config_store_set_current_with_parent);
  add_monitor_test ("/backends/monitor/config-store/set_current-max-size",
                    meta_test_monitor_config_store_set_current_max_size);
  add_monitor_test ("/backends/monitor/config-store/set_current-null",
                    meta_test_monitor_config_store_set_current_null);

  add_monitor_test ("/backends/monitor/initial-linear-config",
                    meta_test_monitor_initial_linear_config);
  add_monitor_test ("/backends/monitor/one-disconnected-linear-config",
                    meta_test_monitor_one_disconnected_linear_config);
  add_monitor_test ("/backends/monitor/one-off-linear-config",
                    meta_test_monitor_one_off_linear_config);
  add_monitor_test ("/backends/monitor/preferred-linear-config",
                    meta_test_monitor_preferred_linear_config);
  add_monitor_test ("/backends/monitor/tiled-linear-config",
                    meta_test_monitor_tiled_linear_config);
  add_monitor_test ("/backends/monitor/tiled-non-preferred-linear-config",
                    meta_test_monitor_tiled_non_preferred_linear_config);
  add_monitor_test ("/backends/monitor/tiled-non-main-origin-linear-config",
                    meta_test_monitor_tiled_non_main_origin_linear_config);
  add_monitor_test ("/backends/monitor/hidpi-linear-config",
                    meta_test_monitor_hidpi_linear_config);
  add_monitor_test ("/backends/monitor/suggested-config",
                    meta_test_monitor_suggested_config);
  add_monitor_test ("/backends/monitor/suggested-config-overlapping",
                    meta_test_monitor_suggested_config_overlapping);
  add_monitor_test ("/backends/monitor/suggested-config-not-adjacent",
                    meta_test_monitor_suggested_config_not_adjacent);
  add_monitor_test ("/backends/monitor/suggested-config-multi-dpi",
                    meta_test_monitor_suggested_config_multi_dpi);
  add_monitor_test ("/backends/monitor/limited-crtcs",
                    meta_test_monitor_limited_crtcs);
  add_monitor_test ("/backends/monitor/lid-switch-config",
                    meta_test_monitor_lid_switch_config);
  add_monitor_test ("/backends/monitor/lid-opened-config",
                    meta_test_monitor_lid_opened_config);
  add_monitor_test ("/backends/monitor/lid-closed-no-external",
                    meta_test_monitor_lid_closed_no_external);
  add_monitor_test ("/backends/monitor/lid-closed-with-hotplugged-external",
                    meta_test_monitor_lid_closed_with_hotplugged_external);
  add_monitor_test ("/backends/monitor/lid-scaled-closed-opened",
                    meta_test_monitor_lid_scaled_closed_opened);
  add_monitor_test ("/backends/monitor/no-outputs",
                    meta_test_monitor_no_outputs);
  add_monitor_test ("/backends/monitor/underscanning-config",
                    meta_test_monitor_underscanning_config);
  add_monitor_test ("/backends/monitor/max-bpc-config",
                    meta_test_monitor_max_bpc_config);
  add_monitor_test ("/backends/monitor/rgb-range-config",
                    meta_test_monitor_rgb_range_config);
  add_monitor_test ("/backends/monitor/preferred-non-first-mode",
                    meta_test_monitor_preferred_non_first_mode);
  add_monitor_test ("/backends/monitor/non-upright-panel",
                    meta_test_monitor_non_upright_panel);
  add_monitor_test ("/backends/monitor/switch-external-without-external",
                    meta_test_monitor_switch_external_without_external);
  add_monitor_test ("/backends/monitor/switch-config-remember-scale",
                    meta_test_monitor_switch_config_remember_scale);

  add_monitor_test ("/backends/monitor/orientation/is-managed",
                    meta_test_monitor_orientation_is_managed);
  add_monitor_test ("/backends/monitor/orientation/initial-rotated",
                    meta_test_monitor_orientation_initial_rotated);
  add_monitor_test ("/backends/monitor/orientation/initial-rotated-no-touch",
                    meta_test_monitor_orientation_initial_rotated_no_touch_mode);
  add_monitor_test ("/backends/monitor/orientation/initial-stored-rotated",
                    meta_test_monitor_orientation_initial_stored_rotated);
  add_monitor_test ("/backends/monitor/orientation/initial-stored-rotated-no-touch",
                    meta_test_monitor_orientation_initial_stored_rotated_no_touch);
  add_monitor_test ("/backends/monitor/orientation/changes",
                    meta_test_monitor_orientation_changes);
  add_monitor_test ("/backends/monitor/orientation/changes-transformed-panel",
                    meta_test_monitor_orientation_changes_for_transformed_panel);
  add_monitor_test ("/backends/monitor/orientation/changes-with-hotplugging",
                    meta_test_monitor_orientation_changes_with_hotplugging);

  add_monitor_test ("/backends/monitor/custom/vertical-config",
                    meta_test_monitor_custom_vertical_config);
  add_monitor_test ("/backends/monitor/custom/primary-config",
                    meta_test_monitor_custom_primary_config);
  add_monitor_test ("/backends/monitor/custom/underscanning-config",
                    meta_test_monitor_custom_underscanning_config);
  add_monitor_test ("/backends/monitor/custom/scale-config",
                    meta_test_monitor_custom_scale_config);
  add_monitor_test ("/backends/monitor/custom/fractional-scale-config",
                    meta_test_monitor_custom_fractional_scale_config);
  add_monitor_test ("/backends/monitor/custom/high-precision-fractional-scale-config",
                    meta_test_monitor_custom_high_precision_fractional_scale_config);
  add_monitor_test ("/backends/monitor/custom/tiled-config",
                    meta_test_monitor_custom_tiled_config);
  add_monitor_test ("/backends/monitor/custom/tiled-custom-resolution-config",
                    meta_test_monitor_custom_tiled_custom_resolution_config);
  add_monitor_test ("/backends/monitor/custom/tiled-non-preferred-config",
                    meta_test_monitor_custom_tiled_non_preferred_config);
  add_monitor_test ("/backends/monitor/custom/mirrored-config",
                    meta_test_monitor_custom_mirrored_config);
  add_monitor_test ("/backends/monitor/custom/first-rotated-config",
                    meta_test_monitor_custom_first_rotated_config);
  add_monitor_test ("/backends/monitor/custom/second-rotated-config",
                    meta_test_monitor_custom_second_rotated_config);
  add_monitor_test ("/backends/monitor/custom/second-rotated-tiled-config",
                    meta_test_monitor_custom_second_rotated_tiled_config);
  add_monitor_test ("/backends/monitor/custom/second-rotated-nonnative-tiled-config",
                    meta_test_monitor_custom_second_rotated_nonnative_tiled_config);
  add_monitor_test ("/backends/monitor/custom/second-rotated-nonnative-config",
                    meta_test_monitor_custom_second_rotated_nonnative_config);
  add_monitor_test ("/backends/monitor/custom/interlaced-config",
                    meta_test_monitor_custom_interlaced_config);
  add_monitor_test ("/backends/monitor/custom/oneoff-config",
                    meta_test_monitor_custom_oneoff);
  add_monitor_test ("/backends/monitor/custom/lid-switch-config",
                    meta_test_monitor_custom_lid_switch_config);

  add_monitor_test ("/backends/monitor/migrated/rotated",
                    meta_test_monitor_migrated_rotated);
  add_monitor_test ("/backends/monitor/migrated/wiggle",
                    meta_test_monitor_migrated_wiggle);
  add_monitor_test ("/backends/monitor/migrated/wiggle-discard",
                    meta_test_monitor_migrated_wiggle_discard);

  add_monitor_test ("/backends/monitor/wm/tiling",
                    meta_test_monitor_wm_tiling);

  add_monitor_test ("/backends/monitor/suppported_scales/integer",
                    meta_test_monitor_supported_integer_scales);
  add_monitor_test ("/backends/monitor/suppported_scales/fractional",
                    meta_test_monitor_supported_fractional_scales);
  add_monitor_test ("/backends/monitor/default_scale",
                    meta_test_monitor_calculate_mode_scale);

  add_monitor_test ("/backends/monitor/policy/system-only",
                    meta_test_monitor_policy_system_only);
}

static void
pre_run_monitor_tests (MetaContext *context)
{
  test_backend = meta_context_get_backend (context);
  create_monitor_test_clients (context);
}

static void
finish_monitor_tests (void)
{
  destroy_monitor_test_clients ();
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;
  char *path;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_NESTED,
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  path = g_test_build_filename (G_TEST_DIST,
                                "tests",
                                "monitor-configs",
                                "system",
                                NULL);
  g_setenv ("XDG_CONFIG_DIRS", path, TRUE);
  g_free (path);
  path = g_test_build_filename (G_TEST_DIST,
                                "tests",
                                "monitor-configs",
                                "user",
                                NULL);
  g_setenv ("XDG_CONFIG_HOME", path, TRUE);
  g_free (path);

  test_context = context;

  init_monitor_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (pre_run_monitor_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (finish_monitor_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
