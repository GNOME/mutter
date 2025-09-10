/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2012 Intel Corporation.
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
 *
 */

#pragma once

#include "cogl/cogl-buffer-impl-private.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-driver.h"
#include "cogl/cogl-offscreen-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-attribute-private.h"
#include "cogl/cogl-sampler-cache-private.h"
#include "cogl/cogl-texture-driver.h"
#include "cogl/cogl-texture-private.h"


struct _CoglDriverClass
{
  GObjectClass parent_class;

  gboolean (* context_init) (CoglDriver  *driver,
                             CoglContext *context);

  const char * (* get_vendor) (CoglDriver *driver);

  gboolean (* is_hardware_accelerated) (CoglDriver *driver);

  CoglGraphicsResetStatus (* get_graphics_reset_status) (CoglDriver *driver);

  gboolean (* update_features) (CoglDriver   *driver,
                                CoglRenderer *renderer,
                                GError      **error);

  gboolean (* format_supports_upload) (CoglDriver      *driver,
                                       CoglPixelFormat  format);

  CoglFramebufferDriver * (* create_framebuffer_driver) (CoglDriver                         *driver,
                                                         CoglFramebuffer                    *framebuffer,
                                                         const CoglFramebufferDriverConfig  *driver_config,
                                                         GError                            **error);

  void (* flush_framebuffer_state) (CoglDriver           *driver,
                                    CoglContext          *context,
                                    CoglFramebuffer      *draw_buffer,
                                    CoglFramebuffer      *read_buffer,
                                    CoglFramebufferState  state);

  /* Prepares for drawing by flushing the journal, framebuffer state,
   * pipeline state and attribute state.
   */
  void (* flush_attributes_state) (CoglDriver           *driver,
                                   CoglFramebuffer      *framebuffer,
                                   CoglPipeline         *pipeline,
                                   CoglFlushLayerState  *layer_state,
                                   CoglDrawFlags         flags,
                                   CoglAttribute       **attributes,
                                   int                   n_attributes);

  /* Flushes the clip stack to the GPU using a combination of the
   * stencil buffer, scissor and clip plane state.
   */
  void
  (* clip_stack_flush) (CoglDriver      *driver,
                        CoglClipStack   *stack,
                        CoglFramebuffer *framebuffer);

  CoglBufferImpl * (* create_buffer_impl) (CoglDriver *driver);

  CoglTextureDriver * (* create_texture_driver) (CoglDriver *driver);

  void (*sampler_init) (CoglDriver            *driver,
                        CoglSamplerCacheEntry *entry);

  void (*sampler_free) (CoglDriver            *driver,
                        CoglSamplerCacheEntry *entry);

  void (* set_uniform) (CoglDriver           *driver,
                        GLint                 location,
                        const CoglBoxedValue *value);
};


CoglBufferImpl * cogl_driver_create_buffer_impl (CoglDriver *driver);

CoglTextureDriver * cogl_driver_create_texture_driver (CoglDriver *driver);

/* Query the GL extensions and lookup the corresponding function
 * pointers. Theoretically the list of extensions can change for
 * different GL contexts so it is the winsys backend's responsibility
 * to know when to re-query the GL extensions. The backend should also
 * check whether the GL context is supported by Cogl. If not it should
 * return FALSE and set @error */
gboolean cogl_driver_update_features (CoglDriver   *driver,
                                      CoglRenderer *renderer,
                                      GError      **error);

#define COGL_DRIVER_ERROR (_cogl_driver_error_quark ())

typedef enum /*< prefix=COGL_DRIVER_ERROR >*/
{
  COGL_DRIVER_ERROR_UNKNOWN_VERSION,
  COGL_DRIVER_ERROR_INVALID_VERSION,
  COGL_DRIVER_ERROR_NO_SUITABLE_DRIVER_FOUND,
  COGL_DRIVER_ERROR_FAILED_TO_LOAD_LIBRARY
} CoglDriverError;

uint32_t
_cogl_driver_error_quark (void);
