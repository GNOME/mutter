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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef META_CRTC_NATIVE_H
#define META_CRTC_NATIVE_H

#include "backends/meta-crtc.h"

#define META_TYPE_CRTC_NATIVE (meta_crtc_native_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaCrtcNative, meta_crtc_native,
                          META, CRTC_NATIVE,
                          MetaCrtc)

struct _MetaCrtcNativeClass
{
  MetaCrtcClass parent_class;

  gboolean (* is_transform_handled) (MetaCrtcNative       *crtc_native,
                                     MetaMonitorTransform  monitor_transform);
};

gboolean meta_crtc_native_is_transform_handled (MetaCrtcNative       *crtc_native,
                                                MetaMonitorTransform  transform);

#endif /* META_CRTC_NATIVE_H */
