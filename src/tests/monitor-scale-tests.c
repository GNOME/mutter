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

#include "backends/meta-logical-monitor-private.h"
#include "tests/monitor-tests-common.h"

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
              .n_scales = 2,
              .scales = { 1.0, 2.0 },
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
              .n_scales = 2,
              .scales = { 1.0, 2.0 },
            },
            {
              .width = 3200,
              .height = 2048,
              .n_scales = 3,
              .scales = { 1.0, 2.0, 4.0 },
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
              .n_scales = 3,
              .scales = { 1.0, 2.0, 4.0 },
            },
            {
              .width = 4096,
              .height = 3072,
              .n_scales = 3,
              .scales = { 1.0, 2.0, 4.0 },
            },
            {
              .width = 5120,
              .height = 2880,
              .n_scales = 3,
              .scales = { 1.0, 2.0, 4.0 },
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
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);
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
              .scales = { 1.000000f },
            },
            {
              .width = 1024,
              .height = 768,
              .n_scales = 2,
              .scales = { 1.000000f, 1.333333f },
            },
            {
              .width = 1280,
              .height = 720,
              .n_scales = 3,
              .scales = { 1.000000f, 1.250000f, 1.333333f },
            },
            {
              .width = 1280,
              .height = 800,
              .n_scales = 3,
              .scales = { 1.000000f, 1.250000f, 1.333333f },
            },
            {
              .width = 1280,
              .height = 1024,
              .n_scales = 2,
              .scales = { 1.000000f, 1.333333f },
            },
            {
              .width = 1366,
              .height = 768,
              .n_scales = 1,
              .scales = { 1.000000f },
            },
            {
              .width = 1440,
              .height = 900,
              .n_scales = 5,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.500000f, 1.666666f },
            },
            {
              .width = 1400,
              .height = 1050,
              .n_scales = 4,
              .scales = { 1.000000f, 1.250000f, 1.666666f, 1.750000f },
            },
            {
              .width = 1600,
              .height = 900,
              .n_scales = 4,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.666666f },
            },
            {
              .width = 1920,
              .height = 1080,
              .n_scales = 6,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.500000f, 1.666666f,
                          2.000000f },
            },
            {
              .width = 1920,
              .height = 1200,
              .n_scales = 6,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.500000f, 1.666666f,
                          2.000000f },
            },
            {
              .width = 2650,
              .height = 1440,
              .n_scales = 5,
              .scales = { 1.000000f, 1.250000f, 1.666667f, 2.000000f, 2.500000f },
            },
            {
              .width = 2880,
              .height = 1800,
              .n_scales = 11,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.500000f, 1.666666f,
                          2.000000f, 2.250000f, 2.500000f, 2.666666f, 3.000000f,
                          3.333333f },
            },
            {
              .width = 3200,
              .height = 1800,
              .n_scales = 8,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.666666f, 2.000000f,
                          2.500000f, 2.666666f, 3.333333f },
            },
            {
              .width = 3200,
              .height = 2048,
              .n_scales = 5,
              .scales = { 1.000000f, 1.333333f, 2.000000f, 2.666666f, 4.000000f },
            },
            {
              .width = 3840,
              .height = 2160,
              .n_scales = 12,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.500000f, 1.666666f,
                          2.000000f, 2.500000f, 2.666666f, 3.000000f, 3.333333f,
                          3.750000f, 4.000000f },
            },
            {
              .width = 3840,
              .height = 2400,
              .n_scales = 12,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.500000f, 1.666666f,
                          2.000000f, 2.500000f, 2.666666f, 3.000000f, 3.333333f,
                          3.750000f, 4.000000f },
            },
            {
              .width = 4096,
              .height = 2160,
              .n_scales = 5,
              .scales = { 1.000000f, 1.333333f, 2.000000f, 2.666666f, 4.000000f }
            },
            {
              .width = 4096,
              .height = 3072,
              .n_scales = 5,
              .scales = { 1.000000f, 1.333333f, 2.000000f, 2.666666f, 4.000000f },
            },
            {
              .width = 5120,
              .height = 2880,
              .n_scales = 9,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.666666f, 2.000000f,
                          2.500000f, 2.666666f, 3.333333f, 4.000000f },
            },
            {
              .width = 7680,
              .height = 4320,
              .n_scales = 12,
              .scales = { 1.000000f, 1.250000f, 1.333333f, 1.500000f, 1.666666f,
                          2.000000f, 2.500000f, 2.666666f, 3.000000f, 3.333333f,
                          3.750000f, 4.000000f },
            },
          },
        },
      },
    },
  };
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);
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
        .dynamic_scale = TRUE,
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
    {
      .name = "Generic 25\" 4K",
      .width = 3840,
      .height = 2160,
      .width_mm = 554,
      .height_mm = 312,
      /* Ideal scale is 1.60, should round to 1.5 and 1.0 */
      .exp = 1.5,
      .exp_nofrac = 1.0,
    },
    {
      .name = "Generic 23.5\" 4K",
      .width = 3840,
      .height = 2160,
      .width_mm = 522,
      .height_mm = 294,
      /* Ideal scale is 1.70, should round to 1.75 and 1.0 */
      .exp = 1.75,
      .exp_nofrac = 2.0,
    },
  };
  /* Set a rather high scale epsilon, to have "easy" scales as the
   * expectations, while ignoring that the actual scaling factors are slightly
   * different, e.g. 1.74863386 instead of 1.75.
   */
  const float scale_epsilon = 0.2f;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *manager_test = META_MONITOR_MANAGER_TEST (manager);

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
      test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);

      g_debug ("Checking default non-fractional scale for %s", cases[i].name);
      meta_monitor_manager_test_set_layout_mode (manager_test,
                                                 META_LOGICAL_MONITOR_LAYOUT_MODE_PHYSICAL);
      meta_emulate_hotplug (test_setup);
      /* Crashes right here because manager->logical_monitors is NULL */
      logical_monitor = manager->logical_monitors->data;
      g_assert_cmpfloat_with_epsilon (logical_monitor->scale, cases[i].exp_nofrac, 0.01);

      g_debug ("Checking default fractional scale for %s", cases[i].name);
      meta_monitor_manager_test_set_layout_mode (manager_test,
                                                 META_LOGICAL_MONITOR_LAYOUT_MODE_LOGICAL);

      serial2 = g_strdup_printf ("0x120001%x", i * 2 + 1);
      test_case_setup.outputs[0].serial = serial2;
      test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      meta_emulate_hotplug (test_setup);
      logical_monitor = manager->logical_monitors->data;
      g_assert_cmpfloat_with_epsilon (logical_monitor->scale, cases[i].exp,
                                      scale_epsilon);
    }
}

static void
init_scale_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/suppported_scales/integer",
                         meta_test_monitor_supported_integer_scales);
  meta_add_monitor_test ("/backends/monitor/suppported_scales/fractional",
                         meta_test_monitor_supported_fractional_scales);
  meta_add_monitor_test ("/backends/monitor/default_scale",
                         meta_test_monitor_calculate_mode_scale);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_scale_tests);
}
