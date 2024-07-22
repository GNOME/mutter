/*
 * Copyright (C) 2021 Red Hat
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

#include "backends/meta-crtc.h"

#define META_TYPE_CRTC_NATIVE (meta_crtc_native_get_type ())
META_EXPORT_TEST
G_DECLARE_DERIVABLE_TYPE (MetaCrtcNative, meta_crtc_native,
                          META, CRTC_NATIVE,
                          MetaCrtc)

struct _MetaCrtcNativeClass
{
  MetaCrtcClass parent_class;

  gboolean (* is_transform_handled) (MetaCrtcNative      *crtc_native,
                                     MtkMonitorTransform  monitor_transform);
  gboolean (* is_hw_cursor_supported) (MetaCrtcNative *crtc_native);
  int64_t (* get_deadline_evasion) (MetaCrtcNative *crtc_native);
};

gboolean meta_crtc_native_is_transform_handled (MetaCrtcNative      *crtc_native,
                                                MtkMonitorTransform  transform);

gboolean meta_crtc_native_is_hw_cursor_supported (MetaCrtcNative *crtc_native);

int64_t meta_crtc_native_get_deadline_evasion (MetaCrtcNative *crtc_native);
