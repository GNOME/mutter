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

#include "tests/monitor-unit-tests.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-config-migration.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-output.h"
#include "core/window-private.h"
#include "meta-backend-test.h"
#include "tests/meta-monitor-manager-test.h"
#include "tests/monitor-test-utils.h"
#include "tests/test-utils.h"
#include "x11/meta-x11-display-private.h"

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

static TestClient *wayland_monitor_test_client = NULL;
static TestClient *x11_monitor_test_client = NULL;

#define WAYLAND_TEST_CLIENT_NAME "wayland_monitor_test_client"
#define WAYLAND_TEST_CLIENT_WINDOW "window1"
#define X11_TEST_CLIENT_NAME "x11_monitor_test_client"
#define X11_TEST_CLIENT_WINDOW "window1"

static gboolean
monitor_tests_alarm_filter (MetaX11Display        *x11_display,
                            XSyncAlarmNotifyEvent *event,
                            gpointer               data)
{
  return test_client_alarm_filter (x11_display, event, x11_monitor_test_client);
}

static void
create_monitor_test_clients (void)
{
  GError *error = NULL;

  test_wait_for_x11_display ();

  meta_x11_display_set_alarm_filter (meta_get_display ()->x11_display,
                                     monitor_tests_alarm_filter, NULL);

  wayland_monitor_test_client = test_client_new (WAYLAND_TEST_CLIENT_NAME,
                                                 META_WINDOW_CLIENT_TYPE_WAYLAND,
                                                 &error);
  if (!wayland_monitor_test_client)
    g_error ("Failed to launch Wayland test client: %s", error->message);

  x11_monitor_test_client = test_client_new (X11_TEST_CLIENT_NAME,
                                             META_WINDOW_CLIENT_TYPE_X11,
                                             &error);
  if (!x11_monitor_test_client)
    g_error ("Failed to launch X11 test client: %s", error->message);

  if (!test_client_do (wayland_monitor_test_client, &error,
                       "create", WAYLAND_TEST_CLIENT_WINDOW,
                       NULL))
    g_error ("Failed to create Wayland window: %s", error->message);

  if (!test_client_do (x11_monitor_test_client, &error,
                       "create", X11_TEST_CLIENT_WINDOW,
                       NULL))
    g_error ("Failed to create X11 window: %s", error->message);

  if (!test_client_do (wayland_monitor_test_client, &error,
                       "show", WAYLAND_TEST_CLIENT_WINDOW,
                       NULL))
    g_error ("Failed to show the window: %s", error->message);

  if (!test_client_do (x11_monitor_test_client, &error,
                       "show", X11_TEST_CLIENT_WINDOW,
                       NULL))
    g_error ("Failed to show the window: %s", error->message);
}

static void
check_test_client_state (TestClient *test_client)
{
  GError *error = NULL;

  if (!test_client_wait (test_client, &error))
    {
      g_error ("Failed to sync test client '%s': %s",
               test_client_get_id (test_client), error->message);
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

  if (!test_client_quit (wayland_monitor_test_client, &error))
    g_error ("Failed to quit Wayland test client: %s", error->message);

  if (!test_client_quit (x11_monitor_test_client, &error))
    g_error ("Failed to quit X11 test client: %s", error->message);

  test_client_destroy (wayland_monitor_test_client);
  test_client_destroy (x11_monitor_test_client);

  meta_x11_display_set_alarm_filter (meta_get_display ()->x11_display,
                                     NULL, NULL);
}

static void
meta_test_monitor_initial_linear_config (void)
{
  check_monitor_configuration (&initial_test_case.expect);
  check_monitor_test_clients_state ();
}

static void
emulate_hotplug (MetaMonitorTestSetup *test_setup)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
  g_usleep (G_USEC_PER_SEC / 100);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "Failed to use linear *");

  emulate_hotplug (test_setup);
  g_test_assert_expected_messages ();

  check_monitor_configuration (&test_case.expect);
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
          .is_laptop_panel = TRUE
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
      .n_tiled_monitors = 0,
      .screen_width = 1024 * 2,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  check_monitor_configuration (&test_case.expect);
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

  check_monitor_configuration (&test_case.expect);
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
          .is_laptop_panel = TRUE
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
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  check_monitor_configuration (&test_case.expect);
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
  MetaBackend *backend = meta_get_backend ();

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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
  MetaBackend *backend = meta_get_backend ();

  /*
   * The first part of this test emulate the following:
   *  1) Start with the lid open
   *  2) Connect external monitor
   *  3) Close lid
   */

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  /* External monitor connected */

  test_case.setup.n_outputs = 2;
  test_case.expect.n_outputs = 2;
  test_case.expect.n_monitors = 2;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  /* Lid closed */

  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.logical_monitors[0].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].x = 0;
  test_case.expect.screen_width = 1024;

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  /*
   * The second part of this test emulate the following:
   *  1) Open lid
   *  2) Disconnect external monitor
   *  3) Close lid
   *  4) Open lid
   */

  /* Lid opened */

  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.logical_monitors[0].monitors[0] = 0,
  test_case.expect.logical_monitors[1].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  /* External monitor disconnected */

  test_case.setup.n_outputs = 1;
  test_case.expect.n_outputs = 1;
  test_case.expect.n_monitors = 1;
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.crtcs[1].current_mode = -1;
  test_case.expect.screen_width = 1024;

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  /* Lid closed */

  test_case.expect.logical_monitors[0].monitors[0] = 0,
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 1024;

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  /* Lid opened */

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("lid-scale.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);

  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  if (!test_client_do (x11_monitor_test_client, &error,
                       "resize", X11_TEST_CLIENT_WINDOW,
                       "123", "210",
                       NULL))
    g_error ("Failed to resize X11 window: %s", error->message);

  if (!test_client_do (wayland_monitor_test_client, &error,
                       "resize", WAYLAND_TEST_CLIENT_WINDOW,
                       "123", "210",
                       NULL))
    g_error ("Failed to resize Wayland window: %s", error->message);

  check_monitor_test_clients_state ();

  /* Also check that we handle going headless -> headless */
  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);

  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();
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
          .height_mm = 125
        },
        {
          .crtc = 1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("vertical.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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
          .height_mm = 125
        },
        {
          .crtc = 1,
          .modes = { 1 },
          .n_modes = 1,
          .preferred_mode = 1,
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("primary.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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
          .height_mm = 125
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("underscanning.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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
          .height_mm = 125
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

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("scale.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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
          .height_mm = 125
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

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("fractional-scale.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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
          .height_mm = 125
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

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("high-precision-fractional-scale.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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
          }
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
          }
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

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("tiled.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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
          }
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

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("tiled-custom-resolution.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("non-preferred-tiled-custom-resolution.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("mirrored.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("first-rotated.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("second-rotated.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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
          }
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
          }
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
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    TRUE);

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("second-rotated-tiled.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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
          }
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
          }
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
          .transform = META_MONITOR_TRANSFORM_NORMAL,
          .x = 1024,
          .y = 0,
        },
        {
          .current_mode = 1,
          .transform = META_MONITOR_TRANSFORM_NORMAL,
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
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    FALSE);

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("second-rotated-tiled.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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
          .transform = META_MONITOR_TRANSFORM_NORMAL,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768 + 1024,
      .screen_height = 1024
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);

  if (!meta_is_stage_views_enabled ())
    {
      g_test_skip ("Not using stage views");
      return;
    }

  meta_monitor_manager_test_set_handles_transforms (monitor_manager_test,
                                                    FALSE);

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("second-rotated.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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
          .height_mm = 125
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("interlaced.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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
          .height_mm = 125
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("oneoff.xml");
  emulate_hotplug (test_setup);

  check_monitor_configuration (&test_case.expect);
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
          .is_laptop_panel = TRUE
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
  MetaBackend *backend = meta_get_backend ();

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  set_custom_monitor_config ("lid-switch.xml");
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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
    (MetaRectangle) { .width = 1024, .height = 768 };
  test_case.expect.logical_monitors[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.logical_monitors[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 + 768;

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  /* Lid was closed */

  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].transform = META_MONITOR_TRANSFORM_90;
  test_case.expect.crtcs[1].x = 0;
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.logical_monitors[0].layout =
    (MetaRectangle) { .width = 768, .height = 1024 };
  test_case.expect.logical_monitors[0].monitors[0] = 1;
  test_case.expect.logical_monitors[0].transform = META_MONITOR_TRANSFORM_90;
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 768;
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  /* Lid was opened */

  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.logical_monitors[0].layout =
    (MetaRectangle) { .width = 1024, .height = 768 };
  test_case.expect.logical_monitors[0].monitors[0] = 0;
  test_case.expect.logical_monitors[0].transform = META_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.logical_monitors[1].transform = META_MONITOR_TRANSFORM_270;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 + 768;
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);
  emulate_hotplug (test_setup);
  check_monitor_configuration (&test_case.expect);
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
  MetaBackend *backend = meta_get_backend ();
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);
  if (!meta_monitor_config_store_set_custom (config_store,
                                             "/dev/null",
                                             migrated_path,
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

  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "tests", "migration",
                                       "rotated-new-finished.xml",
                                       NULL);
  expected_data = read_file (expected_path);
  migrated_data = read_file (migrated_path);

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
  MetaBackend *backend = meta_get_backend ();
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);
  if (!meta_monitor_config_store_set_custom (config_store,
                                             "/dev/null",
                                             migrated_path,
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

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "Failed to finish monitors config migration: "
                         "Mode not available on monitor");
  emulate_hotplug (test_setup);
  g_test_assert_expected_messages ();

  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "tests", "migration",
                                       "wiggle-new-discarded.xml",
                                       NULL);
  expected_data = read_file (expected_path);
  migrated_data = read_file (migrated_path);

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
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);
  meta_later_add (META_LATER_BEFORE_REDRAW,
                  quit_main_loop,
                  loop,
                  NULL);
  g_main_loop_run (loop);
}

static TestClient *
create_test_window (const char *window_name)
{
  TestClient *test_client;
  static int client_count = 0;
  g_autofree char *client_name = NULL;
  g_autoptr (GError) error = NULL;

  client_name = g_strdup_printf ("test_client_%d", client_count++);
  test_client = test_client_new (client_name, META_WINDOW_CLIENT_TYPE_WAYLAND,
                                 &error);
  if (!test_client)
    g_error ("Failed to launch test client: %s", error->message);

  if (!test_client_do (test_client, &error,
                       "create", window_name,
                       NULL))
    g_error ("Failed to create window: %s", error->message);

  return test_client;
}

static void
meta_test_monitor_wm_tiling (void)
{
  MonitorTestCase test_case = initial_test_case;
  MetaMonitorTestSetup *test_setup;
  g_autoptr (GError) error = NULL;

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  /*
   * 1) Start with two monitors connected.
   * 2) Tile it on the second monitor.
   * 3) Unplug both monitors.
   * 4) Replug in first monitor.
   */

  const char *test_window_name= "window1";
  TestClient *test_client = create_test_window (test_window_name);

  if (!test_client_do (test_client, &error,
                       "show", test_window_name,
                       NULL))
    g_error ("Failed to show the window: %s", error->message);

  MetaWindow *test_window =
    test_client_find_window (test_client,
                             test_window_name,
                             &error);
  if (!test_window)
    g_error ("Failed to find the window: %s", error->message);
  test_client_wait_for_window_shown (test_client, test_window);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);
  meta_window_move_to_monitor (test_window, 1);
  check_test_client_state (test_client);

  fprintf(stderr, ":::: %s:%d %s() - UNPLUGGING\n", __FILE__, __LINE__, __func__);

  test_case.setup.n_outputs = 0;
  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);
  test_case.setup.n_outputs = 1;
  test_setup = create_monitor_test_setup (&test_case.setup,
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
  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  meta_window_move_to_monitor (test_window, 1);
  meta_window_tile (test_window, META_TILE_NONE);

  test_case.setup.n_outputs = 1;
  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NO_STORED);
  emulate_hotplug (test_setup);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);

  test_client_destroy (test_client);
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
  MetaBackend *backend = meta_get_backend ();
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

  test_setup = create_monitor_test_setup (&test_case.setup,
                                          MONITOR_TEST_FLAG_NONE);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);
  if (!meta_monitor_config_store_set_custom (config_store,
                                             "/dev/null",
                                             migrated_path,
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

  check_monitor_configuration (&test_case.expect);
  check_monitor_test_clients_state ();

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "tests", "migration",
                                       "wiggle-new-finished.xml",
                                       NULL);
  expected_data = read_file (expected_path);
  migrated_data = read_file (migrated_path);

  g_assert_nonnull (expected_data);
  g_assert_nonnull (migrated_data);

  g_assert (strcmp (expected_data, migrated_data) == 0);

  migrated_file = g_file_new_for_path (migrated_path);
  if (!g_file_delete (migrated_file, NULL, &error))
    g_error ("Failed to remove test data output file: %s", error->message);
}

static void
test_case_setup (void       **fixture,
                 const void   *data)
{
  MetaBackend *backend = meta_get_backend ();
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
create_initial_test_setup (void)
{
  return create_monitor_test_setup (&initial_test_case.setup,
                                    MONITOR_TEST_FLAG_NO_STORED);
}

void
init_monitor_tests (void)
{
  meta_monitor_manager_test_init_test_setup (create_initial_test_setup);

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
  add_monitor_test ("/backends/monitor/preferred-non-first-mode",
                    meta_test_monitor_preferred_non_first_mode);
  add_monitor_test ("/backends/monitor/non-upright-panel",
                    meta_test_monitor_non_upright_panel);

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
}

void
pre_run_monitor_tests (void)
{
  create_monitor_test_clients ();
}

void
finish_monitor_tests (void)
{
  destroy_monitor_test_clients ();
}
