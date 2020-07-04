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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <glib.h>

#include "backends/meta-backend-types.h"
#include "backends/native/meta-backend-native-types.h"
#include "clutter/clutter.h"
#include "cogl/cogl.h"
#include "core/util-private.h"

#define META_TYPE_ONSCREEN_NATIVE (meta_onscreen_native_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaOnscreenNative, meta_onscreen_native,
                      META, ONSCREEN_NATIVE,
                      CoglOnscreenEgl)

void meta_renderer_native_release_onscreen (CoglOnscreen *onscreen);

void meta_onscreen_native_prepare_frame (CoglOnscreen *onscreen,
                                         ClutterFrame *frame);

void meta_onscreen_native_before_redraw (CoglOnscreen *onscreen,
                                         ClutterFrame *frame);

void meta_onscreen_native_finish_frame (CoglOnscreen *onscreen,
                                        ClutterFrame *frame);

void meta_onscreen_native_dummy_power_save_page_flip (CoglOnscreen *onscreen);

gboolean meta_onscreen_native_is_buffer_scanout_compatible (CoglOnscreen *onscreen,
                                                            CoglScanout  *scanout);

void meta_onscreen_native_set_view (CoglOnscreen     *onscreen,
                                    MetaRendererView *view);

MetaOnscreenNative * meta_onscreen_native_new (MetaRendererNative *renderer_native,
                                               MetaGpuKms         *render_gpu,
                                               MetaOutput         *output,
                                               MetaCrtc           *crtc,
                                               CoglContext        *cogl_context,
                                               int                 width,
                                               int                 height);

META_EXPORT_TEST
MetaCrtc * meta_onscreen_native_get_crtc (MetaOnscreenNative *onscreen_native);

void meta_onscreen_native_invalidate (MetaOnscreenNative *onscreen_native);

void meta_onscreen_native_detach (MetaOnscreenNative *onscreen_native);

void meta_onscreen_native_request_frame_sync (MetaOnscreenNative *onscreen_native,
                                              gboolean            enabled);

gboolean meta_onscreen_native_is_frame_sync_enabled (MetaOnscreenNative *onscreen_native);
