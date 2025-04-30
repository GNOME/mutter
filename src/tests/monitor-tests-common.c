/*
 * Copyright (C) 2016-2025 Red Hat, Inc.
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

#include "tests/monitor-tests-common.h"

#include "backends/meta-monitor-config-store.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-monitor-manager-test.h"
#include "tests/meta-test-utils.h"

MetaContext *test_context;

MonitorTestCase initial_test_case = {
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

MetaTestClient *wayland_monitor_test_client = NULL;
MetaTestClient *x11_monitor_test_client = NULL;

#define WAYLAND_TEST_CLIENT_NAME "wayland_monitor_test_client"
#define WAYLAND_TEST_CLIENT_WINDOW "window1"
#define X11_TEST_CLIENT_NAME "x11_monitor_test_client"
#define X11_TEST_CLIENT_WINDOW "window1"


static MetaMonitorTestSetup *
create_initial_test_setup (MetaBackend *backend)
{
  return meta_create_monitor_test_setup (backend,
                                         &initial_test_case.setup,
                                         MONITOR_TEST_FLAG_NO_STORED);
}

static void
create_monitor_test_clients (void)
{
  GError *error = NULL;

  wayland_monitor_test_client = meta_test_client_new (test_context,
                                                      WAYLAND_TEST_CLIENT_NAME,
                                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                                      &error);
  if (!wayland_monitor_test_client)
    g_error ("Failed to launch Wayland test client: %s", error->message);

  x11_monitor_test_client = meta_test_client_new (test_context,
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
pre_run_monitor_tests (void)
{
  create_monitor_test_clients ();
}

static void
finish_monitor_tests (void)
{
  destroy_monitor_test_clients ();
}

int
meta_monitor_test_main (int     argc,
                        char   *argv[0],
                        void (* init_tests) (void))
{
  g_autoptr (MetaContext) context = NULL;
  char *path;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  path = g_test_build_filename (G_TEST_DIST,
                                "monitor-configs",
                                "system",
                                NULL);
  g_setenv ("XDG_CONFIG_DIRS", path, TRUE);
  g_free (path);
  path = g_test_build_filename (G_TEST_DIST,
                                "monitor-configs",
                                "user",
                                NULL);
  g_setenv ("XDG_CONFIG_HOME", path, TRUE);
  g_free (path);

  test_context = context;

  meta_init_monitor_test_setup (create_initial_test_setup);
  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (pre_run_monitor_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (finish_monitor_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
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

void
meta_add_monitor_test (const char *test_path,
                       GTestFunc   test_func)
{
  g_test_add (test_path, gpointer, NULL,
              test_case_setup,
              (void (* ) (void **, const void *)) test_func,
              NULL);
}
