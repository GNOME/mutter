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

#include "tests/monitor-store-unit-tests.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-monitor-manager-private.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/unit-tests.h"

#define MAX_N_MONITORS 10
#define MAX_N_LOGICAL_MONITORS 10
#define MAX_N_CONFIGURATIONS 10

typedef struct _MonitorStoreTestCaseMonitorMode
{
  int width;
  int height;
  float refresh_rate;
  MetaCrtcRefreshRateMode refresh_rate_mode;
  MetaCrtcModeFlag flags;
} MonitorStoreTestCaseMonitorMode;

typedef struct _MonitorStoreTestCaseMonitor
{
  const char *connector;
  const char *vendor;
  const char *product;
  const char *serial;
  MonitorStoreTestCaseMonitorMode mode;
  gboolean is_underscanning;
  unsigned int max_bpc;
  MetaOutputRGBRange rgb_range;
} MonitorStoreTestCaseMonitor;

typedef struct _MonitorStoreTestCaseLogicalMonitor
{
  MtkRectangle layout;
  float scale;
  MetaMonitorTransform transform;
  gboolean is_primary;
  gboolean is_presentation;
  MonitorStoreTestCaseMonitor monitors[MAX_N_MONITORS];
  int n_monitors;
} MonitorStoreTestCaseLogicalMonitor;

typedef struct _MonitorStoreTestConfiguration
{
  MonitorStoreTestCaseLogicalMonitor logical_monitors[MAX_N_LOGICAL_MONITORS];
  int n_logical_monitors;
} MonitorStoreTestConfiguration;

typedef struct _MonitorStoreTestExpect
{
  MonitorStoreTestConfiguration configurations[MAX_N_CONFIGURATIONS];
  int n_configurations;
} MonitorStoreTestExpect;

static MetaMonitorsConfigKey *
create_config_key_from_expect (MonitorStoreTestConfiguration *expect_config)
{
  MetaMonitorsConfigKey *config_key;
  GList *monitor_specs;
  int i;

  monitor_specs = NULL;
  for (i = 0; i < expect_config->n_logical_monitors; i++)
    {
      int j;

      for (j = 0; j < expect_config->logical_monitors[i].n_monitors; j++)
        {
          MetaMonitorSpec *monitor_spec;
          MonitorStoreTestCaseMonitor *test_monitor =
            &expect_config->logical_monitors[i].monitors[j];

          monitor_spec = g_new0 (MetaMonitorSpec, 1);

          monitor_spec->connector = g_strdup (test_monitor->connector);
          monitor_spec->vendor = g_strdup (test_monitor->vendor);
          monitor_spec->product = g_strdup (test_monitor->product);
          monitor_spec->serial = g_strdup (test_monitor->serial);

          monitor_specs = g_list_prepend (monitor_specs, monitor_spec);
        }
    }

  g_assert_nonnull (monitor_specs);

  monitor_specs = g_list_sort (monitor_specs,
                               (GCompareFunc) meta_monitor_spec_compare);

  config_key = g_new0 (MetaMonitorsConfigKey, 1);
  *config_key = (MetaMonitorsConfigKey) {
    .monitor_specs = monitor_specs
  };

  return config_key;
}

static void
check_monitor_store_configuration (MetaMonitorConfigStore        *config_store,
                                   MonitorStoreTestConfiguration *config_expect)
{
  MetaMonitorsConfigKey *config_key;
  MetaMonitorsConfig *config;
  GList *l;
  int i;

  config_key = create_config_key_from_expect (config_expect);
  config = meta_monitor_config_store_lookup (config_store, config_key);
  g_assert_nonnull (config);

  g_assert (meta_monitors_config_key_equal (config->key, config_key));
  meta_monitors_config_key_free (config_key);

  g_assert_cmpuint (g_list_length (config->logical_monitor_configs),
                    ==,
                    config_expect->n_logical_monitors);

  for (l = config->logical_monitor_configs, i = 0; l; l = l->next, i++)
    {
      MetaLogicalMonitorConfig *logical_monitor_config = l->data;
      GList *k;
      int j;

      g_assert (mtk_rectangle_equal (&logical_monitor_config->layout,
                                     &config_expect->logical_monitors[i].layout));
      g_assert_cmpfloat (logical_monitor_config->scale,
                         ==,
                         config_expect->logical_monitors[i].scale);
      g_assert_cmpint (logical_monitor_config->transform,
                       ==,
                       config_expect->logical_monitors[i].transform);
      g_assert_cmpint (logical_monitor_config->is_primary,
                       ==,
                       config_expect->logical_monitors[i].is_primary);
      g_assert_cmpint (logical_monitor_config->is_presentation,
                       ==,
                       config_expect->logical_monitors[i].is_presentation);

      g_assert_cmpint ((int) g_list_length (logical_monitor_config->monitor_configs),
                       ==,
                       config_expect->logical_monitors[i].n_monitors);

      for (k = logical_monitor_config->monitor_configs, j = 0;
           k;
           k = k->next, j++)
        {
          MetaMonitorConfig *monitor_config = k->data;
          MonitorStoreTestCaseMonitor *test_monitor =
            &config_expect->logical_monitors[i].monitors[j];

          g_assert_cmpstr (monitor_config->monitor_spec->connector,
                           ==,
                           test_monitor->connector);
          g_assert_cmpstr (monitor_config->monitor_spec->vendor,
                           ==,
                           test_monitor->vendor);
          g_assert_cmpstr (monitor_config->monitor_spec->product,
                           ==,
                           test_monitor->product);
          g_assert_cmpstr (monitor_config->monitor_spec->serial,
                           ==,
                           test_monitor->serial);

          g_assert_cmpint (monitor_config->mode_spec->width,
                           ==,
                           test_monitor->mode.width);
          g_assert_cmpint (monitor_config->mode_spec->height,
                           ==,
                           test_monitor->mode.height);
          g_assert_cmpfloat (monitor_config->mode_spec->refresh_rate,
                             ==,
                             test_monitor->mode.refresh_rate);
          g_assert_cmpint (monitor_config->mode_spec->refresh_rate_mode,
                           ==,
                           test_monitor->mode.refresh_rate_mode);
          g_assert_cmpint (monitor_config->mode_spec->flags,
                           ==,
                           test_monitor->mode.flags);
          g_assert_cmpint (monitor_config->enable_underscanning,
                           ==,
                           test_monitor->is_underscanning);
          g_assert_cmpint (monitor_config->has_max_bpc,
                           ==,
                           !!test_monitor->max_bpc);
          g_assert_cmpint (monitor_config->max_bpc,
                           ==,
                           test_monitor->max_bpc);
          g_assert_cmpint (monitor_config->rgb_range,
                           ==,
                           test_monitor->rgb_range);
        }
    }
}

static void
check_monitor_store_configurations (MonitorStoreTestExpect *expect)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  int i;

  g_assert_cmpint (meta_monitor_config_store_get_config_count (config_store),
                   ==,
                   expect->n_configurations);

  for (i = 0; i < expect->n_configurations; i++)
    check_monitor_store_configuration (config_store, &expect->configurations[i]);
}

static void
meta_test_monitor_store_single (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1920,
              .height = 1080
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .mode = {
                  .width = 1920,
                  .height = 1080,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          }
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "single.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_vertical (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456a",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          },
          {
            .layout = {
              .x = 0,
              .y = 768,
              .width = 800,
              .height = 600
            },
            .scale = 1,
            .is_primary = FALSE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-2",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456b",
                .mode = {
                  .width = 800,
                  .height = 600,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          }
        },
        .n_logical_monitors = 2
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "vertical.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_primary (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .is_primary = FALSE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456a",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          },
          {
            .layout = {
              .x = 1024,
              .y = 0,
              .width = 800,
              .height = 600
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-2",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456b",
                .mode = {
                  .width = 800,
                  .height = 600,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          }
        },
        .n_logical_monitors = 2
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "primary.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_underscanning (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .is_underscanning = TRUE,
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          },
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "underscanning.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_refresh_rate_mode_fixed (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531,
                  .refresh_rate_mode = META_CRTC_REFRESH_RATE_MODE_FIXED,
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          },
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "refresh-rate-mode-fixed.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_refresh_rate_mode_variable (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531,
                  .refresh_rate_mode = META_CRTC_REFRESH_RATE_MODE_VARIABLE,
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          },
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "refresh-rate-mode-variable.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_max_bpc (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .max_bpc = 12,
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          },
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "max-bpc.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_rgb_range (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_LIMITED,
              }
            },
            .n_monitors = 1,
          },
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "rgb-range.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_scale (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 960,
              .height = 540
            },
            .scale = 2,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .mode = {
                  .width = 1920,
                  .height = 1080,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          }
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "scale.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_fractional_scale (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 800,
              .height = 600
            },
            .scale = 1.5,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .mode = {
                  .width = 1200,
                  .height = 900,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          }
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "fractional-scale.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_high_precision_fractional_scale (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 744,
              .height = 558
            },
            .scale = 1.3763440847396851,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          }
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "high-precision-fractional-scale.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_mirrored (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 800,
              .height = 600
            },
            .scale = 1,
            .is_primary = TRUE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456a",
                .mode = {
                  .width = 800,
                  .height = 600,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              },
              {
                .connector = "DP-2",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456b",
                .mode = {
                  .width = 800,
                  .height = 600,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 2,
          }
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "mirrored.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_first_rotated (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 768,
              .height = 1024
            },
            .scale = 1,
            .transform = META_MONITOR_TRANSFORM_270,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456a",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          },
          {
            .layout = {
              .x = 768,
              .y = 0,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .transform = META_MONITOR_TRANSFORM_NORMAL,
            .is_primary = FALSE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-2",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456b",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          }
        },
        .n_logical_monitors = 2
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "first-rotated.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_second_rotated (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 256,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .transform = META_MONITOR_TRANSFORM_NORMAL,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456a",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          },
          {
            .layout = {
              .x = 1024,
              .y = 0,
              .width = 768,
              .height = 1024
            },
            .scale = 1,
            .transform = META_MONITOR_TRANSFORM_90,
            .is_primary = FALSE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-2",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456b",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          }
        },
        .n_logical_monitors = 2
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "second-rotated.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_interlaced (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1024,
              .height = 768
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .mode = {
                  .width = 1024,
                  .height = 768,
                  .refresh_rate = 60.000495910644531,
                  .flags = META_CRTC_MODE_FLAG_INTERLACE,
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          },
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  meta_set_custom_monitor_config (test_context, "interlaced.xml");

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_unknown_elements (void)
{
  MonitorStoreTestExpect expect = {
    .configurations = {
      {
        .logical_monitors = {
          {
            .layout = {
              .x = 0,
              .y = 0,
              .width = 1920,
              .height = 1080
            },
            .scale = 1,
            .is_primary = TRUE,
            .is_presentation = FALSE,
            .monitors = {
              {
                .connector = "DP-1",
                .vendor = "MetaProduct's Inc.",
                .product = "MetaMonitor",
                .serial = "0x123456",
                .mode = {
                  .width = 1920,
                  .height = 1080,
                  .refresh_rate = 60.000495910644531
                },
                .rgb_range = META_OUTPUT_RGB_RANGE_AUTO,
              }
            },
            .n_monitors = 1,
          }
        },
        .n_logical_monitors = 1
      }
    },
    .n_configurations = 1
  };

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Unknown element <unknownundermonitors> "
                         "under <monitors>, ignoring");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Unknown element <unknownunderconfiguration> "
                         "under <configuration>, ignoring");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Unknown element <unknownunderlogicalmonitor> "
                         "under <logicalmonitor>, ignoring");
  meta_set_custom_monitor_config (test_context, "unknown-elements.xml");
  g_test_assert_expected_messages ();

  check_monitor_store_configurations (&expect);
}

static void
meta_test_monitor_store_policy_not_allowed (void)
{
  g_test_expect_message ("libmutter-test", G_LOG_LEVEL_WARNING,
                         "*Policy can only be defined in system level "
                         "configurations*");
  meta_set_custom_monitor_config (test_context, "policy.xml");
  g_test_assert_expected_messages ();
}

static void
meta_test_monitor_store_policy (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  GList *stores_policy;

  meta_set_custom_monitor_system_config (test_context, "policy.xml");
  stores_policy = meta_monitor_config_store_get_stores_policy (config_store);
  g_assert_cmpuint (g_list_length (stores_policy), ==, 1);
  g_assert_cmpint (GPOINTER_TO_INT (stores_policy->data),
                   ==,
                   META_CONFIG_STORE_SYSTEM);
}

static void
meta_test_monitor_store_policy_empty (void)
{
  g_test_expect_message ("libmutter-test", G_LOG_LEVEL_WARNING,
                         "*Invalid store*");
  meta_set_custom_monitor_system_config (test_context, "policy-empty.xml");
  g_test_assert_expected_messages ();
}

static void
meta_test_monitor_store_policy_duplicate (void)
{
  g_test_expect_message ("libmutter-test", G_LOG_LEVEL_WARNING,
                         "*Multiple identical stores*");
  meta_set_custom_monitor_system_config (test_context, "policy-duplicate.xml");
  g_test_assert_expected_messages ();
}

static void
meta_test_monitor_store_policy_invalid (void)
{
  g_test_expect_message ("libmutter-test", G_LOG_LEVEL_WARNING,
                         "*Invalid store*");
  meta_set_custom_monitor_system_config (test_context, "policy-invalid.xml");
  g_test_assert_expected_messages ();
}

static void
meta_test_monitor_store_policy_multiple (void)
{
  g_test_expect_message ("libmutter-test", G_LOG_LEVEL_WARNING,
                         "*Multiple stores elements under policy*");
  meta_set_custom_monitor_system_config (test_context, "policy-multiple.xml");
  g_test_assert_expected_messages ();
}

static void
meta_test_monitor_store_policy_dbus (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager =
    meta_monitor_manager_get_config_manager (monitor_manager);
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  const MetaMonitorConfigPolicy *policy;

  policy = meta_monitor_config_store_get_policy (config_store);
  g_assert_nonnull (policy);
  g_assert_cmpint (policy->enable_dbus, ==, TRUE);

  meta_set_custom_monitor_system_config (test_context, "policy-dbus.xml");

  policy = meta_monitor_config_store_get_policy (config_store);
  g_assert_nonnull (policy);
  g_assert_cmpint (policy->enable_dbus, ==, FALSE);
}

static void
meta_test_monitor_store_policy_dbus_invalid (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager =
    meta_monitor_manager_get_config_manager (monitor_manager);
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  const MetaMonitorConfigPolicy *policy;

  g_test_expect_message ("libmutter-test", G_LOG_LEVEL_WARNING,
                         "*Multiple dbus elements under policy*");
  meta_set_custom_monitor_system_config (test_context,
                                         "policy-dbus-invalid.xml");
  g_test_assert_expected_messages ();

  policy = meta_monitor_config_store_get_policy (config_store);
  g_assert_nonnull (policy);
  g_assert_cmpint (policy->enable_dbus, ==, FALSE);
}

void
init_monitor_store_tests (void)
{
  g_test_add_func ("/backends/monitor-store/single",
                   meta_test_monitor_store_single);
  g_test_add_func ("/backends/monitor-store/vertical",
                   meta_test_monitor_store_vertical);
  g_test_add_func ("/backends/monitor-store/primary",
                   meta_test_monitor_store_primary);
  g_test_add_func ("/backends/monitor-store/underscanning",
                   meta_test_monitor_store_underscanning);
  g_test_add_func ("/backends/monitor-store/refresh-rate-mode-fixed",
                   meta_test_monitor_store_refresh_rate_mode_fixed);
  g_test_add_func ("/backends/monitor-store/refresh-rate-mode-variable",
                   meta_test_monitor_store_refresh_rate_mode_variable);
  g_test_add_func ("/backends/monitor-store/max-bpc",
                   meta_test_monitor_store_max_bpc);
  g_test_add_func ("/backends/monitor-store/rgb-range",
                   meta_test_monitor_store_rgb_range);
  g_test_add_func ("/backends/monitor-store/scale",
                   meta_test_monitor_store_scale);
  g_test_add_func ("/backends/monitor-store/fractional-scale",
                   meta_test_monitor_store_fractional_scale);
  g_test_add_func ("/backends/monitor-store/high-precision-fractional-scale",
                   meta_test_monitor_store_high_precision_fractional_scale);
  g_test_add_func ("/backends/monitor-store/mirrored",
                   meta_test_monitor_store_mirrored);
  g_test_add_func ("/backends/monitor-store/first-rotated",
                   meta_test_monitor_store_first_rotated);
  g_test_add_func ("/backends/monitor-store/second-rotated",
                   meta_test_monitor_store_second_rotated);
  g_test_add_func ("/backends/monitor-store/interlaced",
                   meta_test_monitor_store_interlaced);
  g_test_add_func ("/backends/monitor-store/unknown-elements",
                   meta_test_monitor_store_unknown_elements);
  g_test_add_func ("/backends/monitor-store/policy-not-allowed",
                   meta_test_monitor_store_policy_not_allowed);
  g_test_add_func ("/backends/monitor-store/policy",
                   meta_test_monitor_store_policy);
  g_test_add_func ("/backends/monitor-store/policy-empty",
                   meta_test_monitor_store_policy_empty);
  g_test_add_func ("/backends/monitor-store/policy-duplicate",
                   meta_test_monitor_store_policy_duplicate);
  g_test_add_func ("/backends/monitor-store/policy-invalid",
                   meta_test_monitor_store_policy_invalid);
  g_test_add_func ("/backends/monitor-store/policy-multiple",
                   meta_test_monitor_store_policy_multiple);
  g_test_add_func ("/backends/monitor-store/dbus",
                   meta_test_monitor_store_policy_dbus);
  g_test_add_func ("/backends/monitor-store/dbus-invalid",
                   meta_test_monitor_store_policy_dbus_invalid);
}
