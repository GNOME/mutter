/*
 * Copyright (C) 2022 Red Hat
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

#include "backends/meta-monitor-private.h"

static void
assert_matches_none (MetaMonitorModeSpec *mode_spec,
                     MetaMonitorModeSpec *mode_specs,
                     int                  n_mode_specs)
{
  int i;

  for (i = 0; i < n_mode_specs; i++)
    {
      g_assert_false (meta_monitor_mode_spec_has_similar_size (mode_spec,
                                                               &mode_specs[i]));
    }
}

static void
meta_test_monitor_mode_spec_similar_size (void)
{
  MetaMonitorModeSpec matching_4k_specs[] = {
    { .width = 4096, .height = 2560 }, /* 16:10 */
    { .width = 4096, .height = 2304 }, /* 16:9 */
    { .width = 3840, .height = 2400 }, /* 16:10 */
    { .width = 3840, .height = 2160 }, /* 16:9 */
  };
  MetaMonitorModeSpec matching_uhd_specs[] = {
    { .width = 1920, .height = 1200 }, /* 16:10 */
    { .width = 1920, .height = 1080 }, /* 16:9 */
    { .width = 2048, .height = 1152 }, /* 16:9 */
  };
  MetaMonitorModeSpec matching_hd_specs[] = {
    { .width = 1366, .height = 768 }, /* ~16:9 */
    { .width = 1280, .height = 720 }, /* 16:9 */
  };
  MetaMonitorModeSpec nonmatching_specs[] = {
    { .width = 1024, .height = 768 },
    { .width = 800, .height = 600 },
    { .width = 640, .height = 480 },
  };
  int i;

  /* Test that 4K modes only matches other 4K modes */

  for (i = 0; i < G_N_ELEMENTS (matching_4k_specs); i++)
    {
      MetaMonitorModeSpec *mode_spec = &matching_4k_specs[i];
      MetaMonitorModeSpec *prev_mode_spec = &matching_4k_specs[i - 1];

      if (i != 0)
        {
          g_assert_true (meta_monitor_mode_spec_has_similar_size (mode_spec,
                                                                  prev_mode_spec));
        }

      assert_matches_none (mode_spec,
                           matching_uhd_specs, G_N_ELEMENTS (matching_uhd_specs));
      assert_matches_none (mode_spec,
                           matching_hd_specs, G_N_ELEMENTS (matching_hd_specs));
      assert_matches_none (mode_spec,
                           nonmatching_specs, G_N_ELEMENTS (nonmatching_specs));
    }

  /* Test that FHD modes only matches other FHD modes */

  for (i = 0; i < G_N_ELEMENTS (matching_uhd_specs); i++)
    {
      MetaMonitorModeSpec *mode_spec = &matching_uhd_specs[i];
      MetaMonitorModeSpec *prev_mode_spec = &matching_uhd_specs[i - 1];

      if (i != 0)
        {
          g_assert_true (meta_monitor_mode_spec_has_similar_size (mode_spec,
                                                                  prev_mode_spec));
        }

      assert_matches_none (mode_spec,
                           matching_hd_specs, G_N_ELEMENTS (matching_hd_specs));
      assert_matches_none (mode_spec,
                           nonmatching_specs, G_N_ELEMENTS (nonmatching_specs));
    }

  /* Test that HD modes only matches other HD modes */

  for (i = 0; i < G_N_ELEMENTS (matching_hd_specs); i++)
    {
      MetaMonitorModeSpec *mode_spec = &matching_hd_specs[i];
      MetaMonitorModeSpec *prev_mode_spec = &matching_hd_specs[i - 1];

      if (i != 0)
        {
          g_assert_true (meta_monitor_mode_spec_has_similar_size (mode_spec,
                                                                  prev_mode_spec));
        }

      assert_matches_none (mode_spec,
                           nonmatching_specs, G_N_ELEMENTS (nonmatching_specs));
    }

  /* Test that the other modes doesn't match each other. */

  for (i = 0; i < G_N_ELEMENTS (nonmatching_specs) - 1; i++)
    {
      MetaMonitorModeSpec *mode_spec = &nonmatching_specs[i];
      MetaMonitorModeSpec *next_mode_spec = &nonmatching_specs[i + 1];

      g_assert_false (meta_monitor_mode_spec_has_similar_size (mode_spec,
                                                               next_mode_spec));
    }
}

static void
meta_test_monitor_parse_mode (void)
{
  const float fallback_refresh_rate = 60.0;
  struct {
    const char *string;

    gboolean expected_pass;
    int expected_width;
    int expected_height;
    float expected_refresh_rate;
  } test_cases[] = {
    {
      .string = "800x600",
      .expected_pass = TRUE,
      .expected_width = 800,
      .expected_height = 600,
      .expected_refresh_rate = fallback_refresh_rate,
    },
    {
      .string = "1280x720@30",
      .expected_pass = TRUE,
      .expected_width = 1280,
      .expected_height = 720,
      .expected_refresh_rate = 30.0,
    },
    {
      .string = "1920x1080@120.50",
      .expected_pass = TRUE,
      .expected_width = 1920,
      .expected_height = 1080,
      .expected_refresh_rate = 120.5,
    },
    {
      .string = "800X600",
      .expected_pass = FALSE,
    },
    {
      .string = "800x",
      .expected_pass = FALSE,
    },
    {
      .string = "800x600@",
      .expected_pass = FALSE,
    },
    {
      .string = "800x600@notanumber",
      .expected_pass = FALSE,
    },
    {
      .string = "nonsense",
      .expected_pass = FALSE,
    },
  };
  int i;

  for (i = 0; i < G_N_ELEMENTS (test_cases); i++)
    {
      int width, height;
      float refresh_rate;

      if (meta_parse_monitor_mode (test_cases[i].string,
                                   &width, &height, &refresh_rate,
                                   fallback_refresh_rate))
        {
          g_assert_true (test_cases[i].expected_pass);
          g_assert_cmpint (width, ==, test_cases[i].expected_width);
          g_assert_cmpint (height, ==, test_cases[i].expected_height);
          g_assert_cmpfloat_with_epsilon (refresh_rate,
                                          test_cases[i].expected_refresh_rate,
                                          FLT_EPSILON);
        }
      else
        {
          g_assert_false (test_cases[i].expected_pass);
        }
    }
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/backends/monitor/spec/similar-size",
                   meta_test_monitor_mode_spec_similar_size);
  g_test_add_func ("/backends/monitor/parse-mode",
                   meta_test_monitor_parse_mode);

  return g_test_run ();
}
