/*
 * Copyright (C) 2025 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include <glib-object.h>

#include "cogl/cogl-macros.h"
#include "cogl/cogl-pixel-format.h"
#include "cogl/cogl-types.h"
#include "cogl/winsys/cogl-winsys.h"

G_BEGIN_DECLS

typedef enum _CoglRenderDeviceMode
{
  COGL_RENDER_DEVICE_MODE_GBM,
  COGL_RENDER_DEVICE_MODE_SURFACELESS,
} CoglRenderDeviceMode;

#define COGL_TYPE_RENDER_DEVICE (cogl_render_device_get_type ())

COGL_EXPORT
G_DECLARE_DERIVABLE_TYPE (CoglRenderDevice,
                          cogl_render_device,
                          COGL,
                          RENDER_DEVICE,
                          GObject)

struct _CoglRenderDeviceClass
{
  GObjectClass parent_class;

  GArray * (* query_drm_modifiers) (CoglRenderDevice       *render_device,
                                    CoglPixelFormat         format,
                                    CoglDrmModifierFilter   filter,
                                    GError                **error);

  uint64_t (* get_implicit_drm_modifier) (CoglRenderDevice *render_device);

  gboolean (* is_dma_buf_supported) (CoglRenderDevice *render_device);

  CoglDmaBufHandle * (* create_dma_buf) (CoglRenderDevice  *render_device,
                                         CoglPixelFormat    format,
                                         uint64_t          *modifiers,
                                         int                n_modifiers,
                                         int                width,
                                         int                height,
                                         GError           **error);
};

COGL_EXPORT CoglRenderDeviceMode
cogl_render_device_get_mode (CoglRenderDevice *render_device);

/**
 * cogl_render_device_query_drm_modifiers: (skip)
 */
COGL_EXPORT GArray *
cogl_render_device_query_drm_modifiers (CoglRenderDevice       *render_device,
                                        CoglPixelFormat         format,
                                        CoglDrmModifierFilter   filter,
                                        GError                **error);

/**
 * cogl_render_device_get_implicit_drm_modifier: (skip)
 */
COGL_EXPORT uint64_t
cogl_render_device_get_implicit_drm_modifier (CoglRenderDevice *render_device);

/**
 * cogl_render_device_is_dma_buf_supported: (skip)
 */
COGL_EXPORT gboolean
cogl_render_device_is_dma_buf_supported (CoglRenderDevice *render_device);

/**
 * cogl_render_device_create_dma_buf: (skip)
 */
COGL_EXPORT CoglDmaBufHandle *
cogl_render_device_create_dma_buf (CoglRenderDevice  *render_device,
                                   CoglPixelFormat    format,
                                   uint64_t          *modifiers,
                                   int                n_modifiers,
                                   int                width,
                                   int                height,
                                   GError           **error);

G_END_DECLS
