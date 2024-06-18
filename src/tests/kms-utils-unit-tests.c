/*
 * Copyright (C) 2021 Akihiko Odaki <akihiko.odaki@gmail.com>
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

#include <glib.h>

#include "backends/native/meta-kms-utils.h"
#include "backends/native/meta-kms-update.h"

typedef struct {
  drmModeModeInfo drm_mode;
  float expected_refresh_rate;
} RefreshRateTestCase;

static const RefreshRateTestCase refresh_rate_test_cases[] = {
  /* "cvt 640 480" */
  {
    .drm_mode = {
      .clock = 23975,
      .hdisplay = 640,
      .hsync_start = 664,
      .hsync_end = 720,
      .htotal = 800,
      .vdisplay = 480,
      .vsync_start = 483,
      .vsync_end = 487,
      .vtotal = 500,
      .vscan = 0,
      .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
    },
    .expected_refresh_rate = 59.9375f,
  },

  /* "cvt 640 480" with htotal 0 */
  {
    .drm_mode = {
       .clock = 23975,
       .hdisplay = 640,
       .hsync_start = 664,
       .hsync_end = 720,
       .htotal = 0,
       .vdisplay = 480,
       .vsync_start = 483,
       .vsync_end = 487,
       .vtotal = 500,
       .vscan = 0,
       .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
    },
    .expected_refresh_rate = 0.0f,
  },

  /* "cvt 640 480" with vtotal 0 */
  {
    .drm_mode = {
      .clock = 23975,
      .hdisplay = 640,
      .hsync_start = 664,
      .hsync_end = 720,
      .htotal = 800,
      .vdisplay = 480,
      .vsync_start = 483,
      .vsync_end = 487,
      .vtotal = 0,
      .vscan = 0,
      .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
    },
    .expected_refresh_rate = 0.0f,
  },

  /* "cvt 320 240" with doubled clock and vscan 2 */
  {
    .drm_mode = {
      .clock = 12062,
      .hdisplay = 320,
      .hsync_start = 336,
      .hsync_end = 360,
      .htotal = 400,
      .vdisplay = 240,
      .vsync_start = 243,
      .vsync_end = 247,
      .vtotal = 252,
      .vscan = 2,
      .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
    },
    .expected_refresh_rate = 59.8313f,
  },

  /* "cvt 15360 8640 180" */
  {
    .drm_mode = {
      .clock = 37793603,
      .hdisplay = 15360,
      .hsync_start = 16880,
      .hsync_end = 18624,
      .htotal = 21888,
      .vdisplay = 8640,
      .vsync_start = 8643,
      .vsync_end = 8648,
      .vtotal = 9593,
      .vscan = 0,
      .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
    },
    .expected_refresh_rate = 179.9939f,
  },
};

static void
meta_test_kms_refresh_rate (void)
{
  size_t index;

  for (index = 0; index < G_N_ELEMENTS (refresh_rate_test_cases); index++)
    {
      const RefreshRateTestCase *test_case = refresh_rate_test_cases + index;
      float refresh_rate;

      refresh_rate =
        meta_calculate_drm_mode_refresh_rate (&test_case->drm_mode);
      g_assert_cmpfloat_with_epsilon (refresh_rate,
                                      test_case->expected_refresh_rate,
                                      0.0001);
    }
}

typedef struct
{
  drmModeModeInfo drm_mode;
  int64_t expected_vblank_duration_us;
} VblankDurationTestCase;

static const VblankDurationTestCase vblank_duration_test_cases[] = {
  /* "cvt 640 480" */
  {
    .drm_mode = {
      .clock = 23975,
      .hdisplay = 640,
      .hsync_start = 664,
      .hsync_end = 720,
      .htotal = 800,
      .vdisplay = 480,
      .vsync_start = 483,
      .vsync_end = 487,
      .vtotal = 500,
      .vscan = 0,
      .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
    },
    .expected_vblank_duration_us = 668,
  },

  /* "cvt 640 480" with htotal 0 */
  {
    .drm_mode = {
       .clock = 23975,
       .hdisplay = 640,
       .hsync_start = 664,
       .hsync_end = 720,
       .htotal = 0,
       .vdisplay = 480,
       .vsync_start = 483,
       .vsync_end = 487,
       .vtotal = 500,
       .vscan = 0,
       .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
    },
    .expected_vblank_duration_us = 0,
  },

  /* "cvt 640 480" with vtotal 0 */
  {
    .drm_mode = {
      .clock = 23975,
      .hdisplay = 640,
      .hsync_start = 664,
      .hsync_end = 720,
      .htotal = 800,
      .vdisplay = 480,
      .vsync_start = 483,
      .vsync_end = 487,
      .vtotal = 0,
      .vscan = 0,
      .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
    },
    .expected_vblank_duration_us = 0,
  },

  /* "cvt 640 480" with DBLSCAN */
  {
    .drm_mode = {
      .clock = 23975,
      .hdisplay = 640,
      .hsync_start = 664,
      .hsync_end = 720,
      .htotal = 800,
      .vdisplay = 480,
      .vsync_start = 483,
      .vsync_end = 487,
      .vtotal = 500,
      .vscan = 0,
      .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC |
               DRM_MODE_FLAG_DBLSCAN,
    },
    .expected_vblank_duration_us = 1335,
  },
};

static void
meta_test_kms_vblank_duration (void)
{
  size_t index;

  for (index = 0; index < G_N_ELEMENTS (vblank_duration_test_cases); index++)
    {
      const VblankDurationTestCase *test_case = vblank_duration_test_cases + index;
      int64_t vblank_duration_us;

      vblank_duration_us =
        meta_calculate_drm_mode_vblank_duration_us (&test_case->drm_mode);
      g_assert_cmpint (vblank_duration_us,
                       ==,
                       test_case->expected_vblank_duration_us);
    }
}

static void
meta_test_kms_update_fixed16 (void)
{
  g_assert_cmpint (meta_fixed_16_from_int (12345), ==, 809041920);
  g_assert_cmpint (meta_fixed_16_to_int (809041920), ==, 12345);
  g_assert_cmpint (meta_fixed_16_from_int (-12345), ==, -809041920);
  g_assert_cmpint (meta_fixed_16_to_int (-809041920), ==, -12345);
}

static void
init_kms_utils_tests (void)
{
  g_test_add_func ("/backends/native/kms/refresh-rate",
                   meta_test_kms_refresh_rate);
  g_test_add_func ("/backends/native/kms/vblank-duration",
                   meta_test_kms_vblank_duration);
  g_test_add_func ("/backends/native/kms/update/fixed16",
                   meta_test_kms_update_fixed16);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  init_kms_utils_tests ();
  return g_test_run ();
}
