/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat
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
 * Written by:
 *     Jonas Ådahl <jadahl@gmail.com>
 */

#pragma once

#include <gbm.h>
#include <glib-object.h>
#include <xf86drmMode.h>

#include "backends/meta-renderer.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-monitor-manager-native.h"

#define META_TYPE_RENDERER_NATIVE (meta_renderer_native_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaRendererNative, meta_renderer_native,
                      META, RENDERER_NATIVE,
                      MetaRenderer)

typedef enum _MetaRendererNativeMode
{
  META_RENDERER_NATIVE_MODE_GBM,
  META_RENDERER_NATIVE_MODE_SURFACELESS,
#ifdef HAVE_EGL_DEVICE
  META_RENDERER_NATIVE_MODE_EGL_DEVICE
#endif
} MetaRendererNativeMode;

MetaRendererNative * meta_renderer_native_new (MetaBackendNative  *backend_native,
                                               GError            **error);

struct gbm_device * meta_gbm_device_from_gpu (MetaGpuKms *gpu_kms);

MetaGpuKms * meta_renderer_native_get_primary_gpu (MetaRendererNative *renderer_native);

MetaDeviceFile * meta_renderer_native_get_primary_device_file (MetaRendererNative *renderer_native);

void meta_renderer_native_prepare_frame (MetaRendererNative *renderer_native,
                                         MetaRendererView   *view,
                                         ClutterFrame       *frame);

void meta_renderer_native_before_redraw (MetaRendererNative *renderer_native,
                                         MetaRendererView   *view,
                                         ClutterFrame       *frame);

void meta_renderer_native_finish_frame (MetaRendererNative *renderer_native,
                                        MetaRendererView   *view,
                                        ClutterFrame       *frame);

void meta_renderer_native_unset_modes (MetaRendererNative *renderer_native);

gboolean meta_renderer_native_send_modifiers (MetaRendererNative *renderer_native);

gboolean meta_renderer_native_use_modifiers (MetaRendererNative *renderer_native);

gboolean meta_renderer_native_has_addfb2 (MetaRendererNative *renderer_native);

MetaRendererNativeMode meta_renderer_native_get_mode (MetaRendererNative *renderer_native);

gboolean meta_renderer_native_choose_gbm_format (MetaKmsPlane    *kms_plane,
                                                 MetaEgl         *egl,
                                                 EGLDisplay       egl_display,
                                                 EGLint          *attributes,
                                                 const uint32_t  *formats,
                                                 size_t           num_formats,
                                                 const char      *purpose,
                                                 EGLConfig       *out_config,
                                                 GError         **error);
