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

#include "backends/meta-monitor-config-store.h"
#include "tests/meta-backend-test.h"
#include "tests/monitor-tests-common.h"

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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "vertical.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "primary.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "underscanning.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_refresh_rate_mode_fixed_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531,
          .refresh_rate_mode = META_CRTC_REFRESH_RATE_MODE_FIXED,
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
              .refresh_rate_mode = META_CRTC_REFRESH_RATE_MODE_FIXED,
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "refresh-rate-mode-fixed.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
}


static void
meta_test_monitor_custom_refresh_rate_mode_variable_config (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.000495910644531,
          .refresh_rate_mode = META_CRTC_REFRESH_RATE_MODE_VARIABLE,
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
              .refresh_rate_mode = META_CRTC_REFRESH_RATE_MODE_VARIABLE,
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "refresh-rate-mode-variable.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "scale.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "fractional-scale.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
          .scale = 1024.0f / 744.0f /* 1.3763440847396851 */
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context,
                                  "high-precision-fractional-scale.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "tiled.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "tiled-custom-resolution.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
          .serial = "0x923456",
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
          .serial = "0x923456",
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context,
                                  "non-preferred-tiled-custom-resolution.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "mirrored.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
          .transform = MTK_MONITOR_TRANSFORM_270
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
          .transform = MTK_MONITOR_TRANSFORM_270
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "first-rotated.xml");
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
          .transform = MTK_MONITOR_TRANSFORM_90
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
          .transform = MTK_MONITOR_TRANSFORM_90,
          .x = 1024,
        }
      },
      .n_crtcs = 2,
      .screen_width = 768 + 1024,
      .screen_height = 1024
    }
  };
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "second-rotated.xml");
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
          .transform = MTK_MONITOR_TRANSFORM_90
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
          .transform = MTK_MONITOR_TRANSFORM_90,
          .x = 1024,
          .y = 0,
        },
        {
          .current_mode = 1,
          .transform = MTK_MONITOR_TRANSFORM_90,
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

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "second-rotated-tiled.xml");
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
          .transform = MTK_MONITOR_TRANSFORM_90
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
          .transform = MTK_MONITOR_TRANSFORM_90,
          .x = 1024,
          .y = 0,
        },
        {
          .current_mode = 1,
          .transform = MTK_MONITOR_TRANSFORM_90,
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

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "second-rotated-tiled.xml");
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
          .transform = MTK_MONITOR_TRANSFORM_90
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
          .transform = MTK_MONITOR_TRANSFORM_90,
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

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "second-rotated.xml");
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "interlaced.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
          .transform = MTK_MONITOR_TRANSFORM_NORMAL
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "oneoff.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
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
          .connector_type = META_CONNECTOR_TYPE_eDP,
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
          .transform = MTK_MONITOR_TRANSFORM_270
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
          .transform = MTK_MONITOR_TRANSFORM_270
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

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "lid-switch.xml");
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();

  /* External monitor connected */

  test_case.setup.n_outputs = 2;
  test_case.expect.n_monitors = 2;
  test_case.expect.n_outputs = 2;
  test_case.expect.crtcs[0].transform = MTK_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.crtcs[1].transform = MTK_MONITOR_TRANSFORM_270;
  test_case.expect.logical_monitors[0].layout =
    (MtkRectangle) { .width = 1024, .height = 768 };
  test_case.expect.logical_monitors[0].transform = MTK_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.logical_monitors[1].transform = MTK_MONITOR_TRANSFORM_270;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 + 768;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();

  /* Lid was closed */

  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].transform = MTK_MONITOR_TRANSFORM_90;
  test_case.expect.crtcs[1].x = 0;
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.logical_monitors[0].layout =
    (MtkRectangle) { .width = 768, .height = 1024 };
  test_case.expect.logical_monitors[0].monitors[0] = 1;
  test_case.expect.logical_monitors[0].transform = MTK_MONITOR_TRANSFORM_90;
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.screen_width = 768;
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();

  /* Lid was opened */

  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[0].transform = MTK_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].transform = MTK_MONITOR_TRANSFORM_270;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.logical_monitors[0].layout =
    (MtkRectangle) { .width = 1024, .height = 768 };
  test_case.expect.logical_monitors[0].monitors[0] = 0;
  test_case.expect.logical_monitors[0].transform = MTK_MONITOR_TRANSFORM_NORMAL;
  test_case.expect.logical_monitors[1].transform = MTK_MONITOR_TRANSFORM_270;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.screen_width = 1024 + 768;
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();
}

static void
meta_test_monitor_custom_detached_groups (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  g_autofree char *path = NULL;
  g_autoptr (GError) error = NULL;

  path = g_test_build_filename (G_TEST_DIST, "monitor-configs",
                                "detached-groups.xml", NULL);
  meta_monitor_config_store_set_custom (config_store, path, NULL,
                                        META_MONITORS_CONFIG_FLAG_NONE,
                                        &error);
  g_assert_nonnull (error);
  g_assert_cmpstr (error->message, ==, "Logical monitors not adjacent");
}

static void
meta_test_monitor_custom_for_lease_config (void)
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
          .transform = MTK_MONITOR_TRANSFORM_NORMAL
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *monitors;
  MetaMonitor *first_monitor;
  MetaMonitor *second_monitor;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "forlease.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);

  first_monitor = g_list_nth_data (monitors, 0);
  second_monitor = g_list_nth_data (monitors, 1);

  g_assert_true (meta_monitor_is_active (first_monitor));
  g_assert_false (meta_monitor_is_for_lease (first_monitor));

  g_assert_false (meta_monitor_is_active (second_monitor));
  g_assert_true (meta_monitor_is_for_lease (second_monitor));
}

static void
meta_test_monitor_custom_for_lease_invalid_config (void)
{
  g_test_expect_message ("libmutter-test", G_LOG_LEVEL_WARNING,
                         "*For lease monitor must be explicitly disabled");
  meta_set_custom_monitor_config (test_context, "forlease-invalid.xml");
  g_test_assert_expected_messages ();
}

static void
on_proxy_call_cb (GObject      *object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  GVariant **ret = user_data;

  *ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (object), res, &error);
  g_assert_no_error (error);
  g_assert_nonnull (ret);
}

static void
assert_monitor_state (GVariant   *state,
                      guint       monitor_index,
                      const char *connector,
                      gboolean    is_for_lease)
{
  g_autoptr (GVariant) monitors = NULL;
  g_autoptr (GVariant) monitor = NULL;
  g_autoptr (GVariant) monitor_spec = NULL;
  g_autoptr (GVariant) spec_connector = NULL;
  g_autoptr (GVariant) monitor_properties = NULL;
  g_autoptr (GVariant) for_lease_property = NULL;

  monitors = g_variant_get_child_value (state, 1);
  monitor = g_variant_get_child_value (monitors, monitor_index);

  monitor_spec = g_variant_get_child_value (monitor, 0);
  spec_connector = g_variant_get_child_value (monitor_spec, 0);
  g_assert_cmpstr (g_variant_get_string (spec_connector, NULL), ==, connector);

  monitor_properties = g_variant_get_child_value (monitor, 2);
  for_lease_property = g_variant_lookup_value (monitor_properties,
                                               "is-for-lease",
                                               G_VARIANT_TYPE_BOOLEAN);
  g_assert (g_variant_get_boolean (for_lease_property) == is_for_lease);
}

static void
proxy_ready_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  GDBusProxy *proxy;
  GDBusProxy **display_config_proxy_ptr = user_data;
  g_autoptr (GError) error = NULL;

  proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
  g_assert_nonnull (proxy);
  g_assert_no_error (error);

  *display_config_proxy_ptr = proxy;
}

static void
meta_test_monitor_custom_for_lease_config_dbus (void)
{
  MonitorTestCaseSetup test_case_setup = {
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
  };
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;
  g_autoptr (GDBusProxy) display_config_proxy = NULL;
  g_autoptr (GVariant) state = NULL;
  uint32_t serial;
  GVariantBuilder b;
  g_autoptr (GVariant) apply_config_ret = NULL;
  g_autoptr (GVariant) new_state = NULL;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "forlease.xml");
  meta_emulate_hotplug (test_setup);
  meta_check_monitor_test_clients_state ();

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                            NULL,
                            "org.gnome.Mutter.DisplayConfig",
                            "/org/gnome/Mutter/DisplayConfig",
                            "org.gnome.Mutter.DisplayConfig",
                            NULL,
                            proxy_ready_cb,
                            &display_config_proxy);
  while (!display_config_proxy)
    g_main_context_iteration (NULL, TRUE);

  g_dbus_proxy_call (display_config_proxy,
                     "GetCurrentState",
                     NULL,
                     G_DBUS_CALL_FLAGS_NO_AUTO_START,
                     -1,
                     NULL,
                     on_proxy_call_cb,
                     &state);
  while (!state)
    g_main_context_iteration (NULL, TRUE);

  assert_monitor_state (state, 0, "DP-1", FALSE);
  assert_monitor_state (state, 1, "DP-2", TRUE);

  /* Swap monitor for lease */
  serial = g_variant_get_uint32 (g_variant_get_child_value (state, 0));

  g_variant_builder_init (&b, G_VARIANT_TYPE ("(uua(iiduba(ssa{sv}))a{sv})"));
  g_variant_builder_add (&b, "u", serial); /* Serial from GetCurrentState */
  g_variant_builder_add (&b, "u", 1);      /* Method: Temporary */

  /* Logical monitors */
  g_variant_builder_open (&b, G_VARIANT_TYPE ("a(iiduba(ssa{sv}))"));
  g_variant_builder_open (&b, G_VARIANT_TYPE ("(iiduba(ssa{sv}))"));
  g_variant_builder_add (&b, "i", 0);                        /* x */
  g_variant_builder_add (&b, "i", 0);                        /* y */
  g_variant_builder_add (&b, "d", 1.0);                      /* Scale */
  g_variant_builder_add (&b, "u", 0);                        /* Transform */
  g_variant_builder_add (&b, "b", TRUE);                     /* Primary */
  g_variant_builder_add_parsed (&b, "[(%s, %s, @a{sv} {})]", /* Monitors */
                                "DP-2",
                                "800x600@60.000");
  g_variant_builder_close (&b);
  g_variant_builder_close (&b);

  /* Properties */
  g_variant_builder_open (&b, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add_parsed (&b, "{'monitors-for-lease', <[(%s, %s, %s, %s)]>}",
                                "DP-1",
                                "MetaProduct\'s Inc.",
                                "MetaMonitor",
                                "0x123456");
  g_variant_builder_close (&b);

  g_dbus_proxy_call (display_config_proxy,
                     "ApplyMonitorsConfig",
                     g_variant_builder_end (&b),
                     G_DBUS_CALL_FLAGS_NO_AUTO_START,
                     -1,
                     NULL,
                     on_proxy_call_cb,
                     &apply_config_ret);
  while (!apply_config_ret)
    g_main_context_iteration (NULL, TRUE);

  /* Check that monitors changed */
  g_dbus_proxy_call (display_config_proxy,
                     "GetCurrentState",
                     NULL,
                     G_DBUS_CALL_FLAGS_NO_AUTO_START,
                     -1,
                     NULL,
                     on_proxy_call_cb,
                     &new_state);
  while (!new_state)
    g_main_context_iteration (NULL, TRUE);

  assert_monitor_state (new_state, 0, "DP-1", TRUE);
  assert_monitor_state (new_state, 1, "DP-2", FALSE);
}

static void
init_custom_config_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/custom/vertical-config",
                         meta_test_monitor_custom_vertical_config);
  meta_add_monitor_test ("/backends/monitor/custom/primary-config",
                         meta_test_monitor_custom_primary_config);
  meta_add_monitor_test ("/backends/monitor/custom/underscanning-config",
                         meta_test_monitor_custom_underscanning_config);
  meta_add_monitor_test ("/backends/monitor/custom/refresh-rate-mode-fixed-config",
                         meta_test_monitor_custom_refresh_rate_mode_fixed_config);
  meta_add_monitor_test ("/backends/monitor/custom/refresh-rate-mode-variable-config",
                         meta_test_monitor_custom_refresh_rate_mode_variable_config);
  meta_add_monitor_test ("/backends/monitor/custom/scale-config",
                         meta_test_monitor_custom_scale_config);
  meta_add_monitor_test ("/backends/monitor/custom/fractional-scale-config",
                         meta_test_monitor_custom_fractional_scale_config);
  meta_add_monitor_test ("/backends/monitor/custom/high-precision-fractional-scale-config",
                         meta_test_monitor_custom_high_precision_fractional_scale_config);
  meta_add_monitor_test ("/backends/monitor/custom/tiled-config",
                         meta_test_monitor_custom_tiled_config);
  meta_add_monitor_test ("/backends/monitor/custom/tiled-custom-resolution-config",
                         meta_test_monitor_custom_tiled_custom_resolution_config);
  meta_add_monitor_test ("/backends/monitor/custom/tiled-non-preferred-config",
                         meta_test_monitor_custom_tiled_non_preferred_config);
  meta_add_monitor_test ("/backends/monitor/custom/mirrored-config",
                         meta_test_monitor_custom_mirrored_config);
  meta_add_monitor_test ("/backends/monitor/custom/first-rotated-config",
                         meta_test_monitor_custom_first_rotated_config);
  meta_add_monitor_test ("/backends/monitor/custom/second-rotated-config",
                         meta_test_monitor_custom_second_rotated_config);
  meta_add_monitor_test ("/backends/monitor/custom/second-rotated-tiled-config",
                         meta_test_monitor_custom_second_rotated_tiled_config);
  meta_add_monitor_test ("/backends/monitor/custom/second-rotated-nonnative-tiled-config",
                         meta_test_monitor_custom_second_rotated_nonnative_tiled_config);
  meta_add_monitor_test ("/backends/monitor/custom/second-rotated-nonnative-config",
                         meta_test_monitor_custom_second_rotated_nonnative_config);
  meta_add_monitor_test ("/backends/monitor/custom/interlaced-config",
                         meta_test_monitor_custom_interlaced_config);
  meta_add_monitor_test ("/backends/monitor/custom/oneoff-config",
                         meta_test_monitor_custom_oneoff);
  meta_add_monitor_test ("/backends/monitor/custom/lid-switch-config",
                         meta_test_monitor_custom_lid_switch_config);
  meta_add_monitor_test ("/backends/monitor/custom/detached-groups",
                         meta_test_monitor_custom_detached_groups);
  meta_add_monitor_test ("/backends/monitor/custom/for-lease-config",
                         meta_test_monitor_custom_for_lease_config);
  meta_add_monitor_test ("/backends/monitor/custom/for-lease-invalid-config",
                         meta_test_monitor_custom_for_lease_invalid_config);
  meta_add_monitor_test ("/backends/monitor/custom/for-lease-config-dbus",
                         meta_test_monitor_custom_for_lease_config_dbus);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_custom_config_tests);
}

