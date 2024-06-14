/*
 * Copyright (C) 2016-2024 Red Hat, Inc.
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

#include "backends/native/meta-crtc-native.h"

struct _MetaCrtcTest
{
  MetaCrtcNative parent;

  struct {
    size_t size;
    uint16_t *red;
    uint16_t *green;
    uint16_t *blue;
  } gamma;

  gboolean handles_transforms;
};

#define META_TYPE_CRTC_TEST (meta_crtc_test_get_type ())
META_EXPORT
G_DECLARE_FINAL_TYPE (MetaCrtcTest, meta_crtc_test,
                      META, CRTC_TEST,
                      MetaCrtcNative)

META_EXPORT
void meta_crtc_test_disable_gamma_lut (MetaCrtcTest *crtc_test);

void meta_crtc_test_set_is_transform_handled (MetaCrtcTest *crtc_test,
                                              gboolean      handles_transforms);
