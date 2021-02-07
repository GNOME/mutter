/*
 * Copyright (C) 2016-2020 Red Hat
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
 *
 */

#ifndef META_ONSCREEN_NATIVE_H
#define META_ONSCREEN_NATIVE_H

#include <glib.h>

#include "backends/meta-backend-types.h"
#include "backends/native/meta-backend-native-types.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"

#define META_TYPE_ONSCREEN_NATIVE (meta_onscreen_native_get_type ())
G_DECLARE_FINAL_TYPE (MetaOnscreenNative, meta_onscreen_native,
                      META, ONSCREEN_NATIVE,
                      CoglOnscreenEgl)

void meta_renderer_native_release_onscreen (CoglOnscreen *onscreen);

void meta_onscreen_native_finish_frame (CoglOnscreen *onscreen,
                                        ClutterFrame *frame);

void meta_onscreen_native_dummy_power_save_page_flip (CoglOnscreen *onscreen);

gboolean meta_onscreen_native_is_buffer_scanout_compatible (CoglOnscreen *onscreen,
                                                            uint32_t      drm_format,
                                                            uint64_t      drm_modifier,
                                                            uint32_t      stride);

void meta_onscreen_native_set_view (CoglOnscreen     *onscreen,
                                    MetaRendererView *view);

MetaOnscreenNative * meta_onscreen_native_new (MetaRendererNative *renderer_native,
                                               MetaGpuKms         *render_gpu,
                                               MetaOutput         *output,
                                               MetaCrtc           *crtc,
                                               CoglContext        *cogl_context,
                                               int                 width,
                                               int                 height);

#endif /* META_ONSCREEN_NATIVE_H */
