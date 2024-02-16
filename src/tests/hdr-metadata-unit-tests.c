/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2023 Red Hat Inc.
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
 * Author: Sebastian Wick <sebastian.wick@redhat.com>
 */

#include "config.h"

#include "hdr-metadata-unit-tests.h"

#include "tests/meta-monitor-test-utils.h"

#include "backends/native/meta-kms-connector-private.h"

static void
meta_test_hdr_metadata_equality1 (void)
{
  struct hdr_output_metadata drm_metadata_in, drm_metadata_out;
  MetaOutputHdrMetadata metadata;

  memset (&drm_metadata_in, 0, sizeof (drm_metadata_in));
  memset (&drm_metadata_out, 0, sizeof (drm_metadata_out));

  drm_metadata_in = (struct hdr_output_metadata) {
    .metadata_type = 0,
    .hdmi_metadata_type1 = {
      .eotf = 2,
      .metadata_type = 0,
      .display_primaries[0].x = 27,
      .display_primaries[0].y = 53,
      .display_primaries[1].x = 111,
      .display_primaries[1].y = 43,
      .display_primaries[2].x = 633,
      .display_primaries[2].y = 2,
      .white_point.x = 27,
      .white_point.y = 53,
      .max_display_mastering_luminance = 3333,
      .min_display_mastering_luminance = 1000,
      .max_cll = 392,
      .max_fall = 2,
    },
  };

  g_assert_true (set_output_hdr_metadata (&drm_metadata_in, &metadata));
  meta_set_drm_hdr_metadata (&metadata, &drm_metadata_out);

  g_assert_cmpint (memcmp (&drm_metadata_in,
                           &drm_metadata_out,
                           sizeof (struct hdr_output_metadata)), ==, 0);
}

static void
meta_test_hdr_metadata_equality2 (void)
{
  struct hdr_output_metadata drm_metadata;
  MetaOutputHdrMetadata metadata_in, metadata_out;

  memset (&metadata_in, 0, sizeof (metadata_in));
  memset (&metadata_out, 0, sizeof (metadata_out));

  metadata_in = (MetaOutputHdrMetadata) {
    .active = true,
    .eotf = META_OUTPUT_HDR_METADATA_EOTF_PQ,
    .mastering_display_primaries[0].x = 0.2384,
    .mastering_display_primaries[0].y = 1.0000,
    .mastering_display_primaries[1].x = 0.4,
    .mastering_display_primaries[1].y = 0.002,
    .mastering_display_primaries[2].x = 0.3,
    .mastering_display_primaries[2].y = 0.333,
    .mastering_display_white_point.x = 0.0001,
    .mastering_display_white_point.y = 0.999,
    .mastering_display_max_luminance = 22.22,
    .max_cll = 50.5,
    .max_fall = 12.0,
  };

  meta_set_drm_hdr_metadata (&metadata_in, &drm_metadata);
  g_assert_true (set_output_hdr_metadata (&drm_metadata, &metadata_out));
  metadata_out.active = TRUE;

  g_assert_cmpint (memcmp (&metadata_in,
                           &metadata_out,
                           sizeof (MetaOutputHdrMetadata)), !=, 0);

  g_assert_true (hdr_metadata_equal (&metadata_in, &metadata_out));
}

void
init_hdr_metadata_tests (void)
{
  g_test_add_func ("/backends/native/hdr-metadata-equality1",
                   meta_test_hdr_metadata_equality1);
  g_test_add_func ("/backends/native/hdr-metadata-equality2",
                   meta_test_hdr_metadata_equality2);
}
