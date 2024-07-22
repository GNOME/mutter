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

#include "config.h"

#include "backends/native/meta-crtc-native.h"

G_DEFINE_ABSTRACT_TYPE (MetaCrtcNative, meta_crtc_native,
                        META_TYPE_CRTC)

gboolean
meta_crtc_native_is_transform_handled (MetaCrtcNative      *crtc_native,
                                       MtkMonitorTransform  transform)
{
  MetaCrtcNativeClass *klass = META_CRTC_NATIVE_GET_CLASS (crtc_native);

  return klass->is_transform_handled (crtc_native, transform);
}

gboolean
meta_crtc_native_is_hw_cursor_supported (MetaCrtcNative *crtc_native)
{
  MetaCrtcNativeClass *klass = META_CRTC_NATIVE_GET_CLASS (crtc_native);

  return klass->is_hw_cursor_supported (crtc_native);
}

int64_t
meta_crtc_native_get_deadline_evasion (MetaCrtcNative *crtc_native)
{
  MetaCrtcNativeClass *klass = META_CRTC_NATIVE_GET_CLASS (crtc_native);

  return klass->get_deadline_evasion (crtc_native);
}

static void
meta_crtc_native_init (MetaCrtcNative *crtc_native)
{
}

static void
meta_crtc_native_class_init (MetaCrtcNativeClass *klass)
{
}
