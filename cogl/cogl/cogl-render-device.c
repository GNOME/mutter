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

#include "config.h"

#include <gio/gio.h>

#include "cogl/cogl-render-device.h"

G_DEFINE_ABSTRACT_TYPE (CoglRenderDevice, cogl_render_device, G_TYPE_OBJECT)

static void
cogl_render_device_class_init (CoglRenderDeviceClass *klass)
{
}

static void
cogl_render_device_init (CoglRenderDevice *render_device)
{
}

GArray *
cogl_render_device_query_drm_modifiers (CoglRenderDevice       *render_device,
                                        CoglPixelFormat         format,
                                        CoglDrmModifierFilter   filter,
                                        GError                **error)
{
  CoglRenderDeviceClass *klass = COGL_RENDER_DEVICE_GET_CLASS (render_device);

  if (klass->query_drm_modifiers)
    return klass->query_drm_modifiers (render_device, format, filter, error);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "CoglRenderDevice doesn't support querying drm modifiers");

  return NULL;
}

uint64_t
cogl_render_device_get_implicit_drm_modifier (CoglRenderDevice *render_device)
{
  CoglRenderDeviceClass *klass = COGL_RENDER_DEVICE_GET_CLASS (render_device);

  if (klass->get_implicit_drm_modifier)
    return klass->get_implicit_drm_modifier (render_device);

  return 0;
}

gboolean
cogl_render_device_is_dma_buf_supported (CoglRenderDevice *render_device)
{
  CoglRenderDeviceClass *klass = COGL_RENDER_DEVICE_GET_CLASS (render_device);

  if (klass->is_dma_buf_supported)
    return klass->is_dma_buf_supported (render_device);

  return FALSE;
}

CoglDmaBufHandle *
cogl_render_device_create_dma_buf (CoglRenderDevice  *render_device,
                                   CoglPixelFormat    format,
                                   uint64_t          *modifiers,
                                   int                n_modifiers,
                                   int                width,
                                   int                height,
                                   GError           **error)
{
  CoglRenderDeviceClass *klass = COGL_RENDER_DEVICE_GET_CLASS (render_device);

  if (klass->create_dma_buf)
    return klass->create_dma_buf (render_device, format,
                                  modifiers, n_modifiers,
                                  width, height, error);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "CoglRenderDevice doesn't support creating DMA buffers");

  return NULL;
}
