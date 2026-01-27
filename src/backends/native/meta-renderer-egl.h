/*
 * Copyright (C) 2025 Red Hat Inc.
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

#include <glib-object.h>

#include "cogl/cogl.h"

typedef struct _MetaRendererNative MetaRendererNative;
typedef struct _MetaRendererNativeGpuData MetaRendererNativeGpuData;

#define META_TYPE_RENDERER_EGL (meta_renderer_egl_get_type ())
G_DECLARE_FINAL_TYPE (MetaRendererEgl,
                      meta_renderer_egl,
                      META, RENDERER_EGL,
                      CoglRendererEGL)

MetaRendererEgl *meta_renderer_egl_new (MetaRendererNativeGpuData * renderer_gpu_data);

MetaRendererNativeGpuData * meta_renderer_egl_get_renderer_gpu_data (MetaRendererEgl *renderer_egl);
