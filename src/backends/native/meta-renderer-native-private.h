/*
 * Copyright (C) 2011 Intel Corporation.
 * Copyright (C) 2016-2020 Red Hat
 * Copyright (c) 2018,2019 DisplayLink (UK) Ltd.
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
 *
 */

#ifndef META_RENDERER_NATIVE_PRIVATE_H
#define META_RENDERER_NATIVE_PRIVATE_H

#include "backends/meta-gles3.h"
#include "backends/native/meta-renderer-native.h"

typedef enum _MetaSharedFramebufferCopyMode
{
  /* Zero-copy: primary GPU exports, secondary GPU imports as KMS FB */
  META_SHARED_FRAMEBUFFER_COPY_MODE_ZERO,
  /* the secondary GPU will make the copy */
  META_SHARED_FRAMEBUFFER_COPY_MODE_SECONDARY_GPU,
  /*
   * The copy is made in the primary GPU rendering context, either
   * as a CPU copy through Cogl read-pixels or as primary GPU copy
   * using glBlitFramebuffer.
   */
  META_SHARED_FRAMEBUFFER_COPY_MODE_PRIMARY
} MetaSharedFramebufferCopyMode;

typedef struct _MetaRendererNativeGpuData
{
  MetaRendererNative *renderer_native;

  struct {
    struct gbm_device *device;
  } gbm;

#ifdef HAVE_EGL_DEVICE
  struct {
    EGLDeviceEXT device;
  } egl;
#endif

  MetaRendererNativeMode mode;

  EGLDisplay egl_display;

  /*
   * Fields used for blitting iGPU framebuffer content onto dGPU framebuffers.
   */
  struct {
    MetaSharedFramebufferCopyMode copy_mode;
    gboolean is_hardware_rendering;
    gboolean has_EGL_EXT_image_dma_buf_import_modifiers;

    /* For GPU blit mode */
    EGLContext egl_context;
    EGLConfig egl_config;
  } secondary;
} MetaRendererNativeGpuData;

MetaEgl * meta_renderer_native_get_egl (MetaRendererNative *renderer_native);

MetaGles3 * meta_renderer_native_get_gles3 (MetaRendererNative *renderer_native);

MetaRendererNativeGpuData * meta_renderer_native_get_gpu_data (MetaRendererNative *renderer_native,
                                                               MetaGpuKms         *gpu_kms);

gboolean meta_renderer_native_has_pending_mode_sets (MetaRendererNative *renderer_native);

gboolean meta_renderer_native_has_pending_mode_set (MetaRendererNative *renderer_native);

void meta_renderer_native_notify_mode_sets_reset (MetaRendererNative *renderer_native);

void meta_renderer_native_post_mode_set_updates (MetaRendererNative *renderer_native);

void meta_renderer_native_queue_power_save_page_flip (MetaRendererNative *renderer_native,
                                                      CoglOnscreen       *onscreen);

CoglFramebuffer * meta_renderer_native_create_dma_buf_framebuffer (MetaRendererNative  *renderer_native,
                                                                   int                  dmabuf_fd,
                                                                   uint32_t             width,
                                                                   uint32_t             height,
                                                                   uint32_t             stride,
                                                                   uint32_t             offset,
                                                                   uint64_t             modifier,
                                                                   uint32_t             drm_format,
                                                                   GError             **error);

gboolean meta_renderer_native_pop_pending_mode_set (MetaRendererNative *renderer_native,
                                                    MetaRendererView   *view);

const CoglWinsysVtable * meta_get_renderer_native_parent_vtable (void);

#endif /* META_RENDERER_NATIVE_PRIVATE_H */
