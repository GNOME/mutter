/*
 * Copyright (C) 2021 Red Hat Inc.
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

#pragma once

#include <colord.h>
#include <glib-object.h>
#include <lcms2.h>
#include <stdint.h>

#include "backends/meta-backend-types.h"
#include "core/util-private.h"

typedef struct _MetaColorCalibration
{
  gboolean has_vcgt;
  cmsToneCurve *vcgt[3];

  gboolean has_adaptation_matrix;
  CdMat3x3 adaptation_matrix;

  char *brightness_profile;
} MetaColorCalibration;

#define META_TYPE_COLOR_PROFILE (meta_color_profile_get_type ())
G_DECLARE_FINAL_TYPE (MetaColorProfile, meta_color_profile,
                      META, COLOR_PROFILE,
                      GObject)

MetaColorProfile * meta_color_profile_new_from_icc (MetaColorManager     *color_manager,
                                                    CdIcc                *cd_icc,
                                                    GBytes               *raw_bytes,
                                                    MetaColorCalibration *color_calibration);

MetaColorProfile * meta_color_profile_new_from_cd_profile (MetaColorManager     *color_manager,
                                                           CdProfile            *cd_profile,
                                                           CdIcc                *cd_icc,
                                                           GBytes               *raw_bytes,
                                                           MetaColorCalibration *color_calibration);

gboolean meta_color_profile_equals_bytes (MetaColorProfile *color_profile,
                                          GBytes           *bytes);

const uint8_t * meta_color_profile_get_data (MetaColorProfile *color_profile);

size_t meta_color_profile_get_data_size (MetaColorProfile *color_profile);

META_EXPORT_TEST
CdIcc * meta_color_profile_get_cd_icc (MetaColorProfile *color_profile);

CdProfile * meta_color_profile_get_cd_profile (MetaColorProfile *color_profile);

gboolean meta_color_profile_is_ready (MetaColorProfile *color_profile);

META_EXPORT_TEST
const char * meta_color_profile_get_id (MetaColorProfile *color_profile);

const char * meta_color_profile_get_file_path (MetaColorProfile *color_profile);

const char * meta_color_profile_get_brightness_profile (MetaColorProfile *color_profile);

MetaGammaLut * meta_color_profile_generate_gamma_lut (MetaColorProfile *color_profile,
                                                      unsigned int      temperature,
                                                      size_t            lut_size);

META_EXPORT_TEST
const MetaColorCalibration * meta_color_profile_get_calibration (MetaColorProfile *color_profile);

MetaColorCalibration * meta_color_calibration_new (CdIcc          *cd_icc,
                                                   const CdMat3x3 *adaptation_matrix);

void meta_color_calibration_free (MetaColorCalibration *color_calibration);
