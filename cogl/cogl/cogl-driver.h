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

#include "cogl/cogl-context.h"
#include "cogl/cogl-offscreen-private.h"
#include "cogl/cogl-framebuffer-private.h"
#include "cogl/cogl-attribute-private.h"
#include "cogl/cogl-sampler-cache-private.h"
#include "cogl/cogl-texture-private.h"

typedef struct _CoglDriverVtable CoglDriverVtable;

struct _CoglDriverVtable
{
  gboolean
  (* context_init) (CoglContext *context);

  void
  (* context_deinit) (CoglContext *context);

  const char *
  (* get_vendor) (CoglContext *context);

  gboolean
  (* is_hardware_accelerated) (CoglContext *context);

  CoglGraphicsResetStatus
  (* get_graphics_reset_status) (CoglContext *context);

  /* TODO: factor this out since this is OpenGL specific and
   * so can be ignored by non-OpenGL drivers. */
  CoglPixelFormat
  (* pixel_format_to_gl) (CoglContext *context,
                          CoglPixelFormat format,
                          GLenum *out_glintformat,
                          GLenum *out_glformat,
                          GLenum *out_gltype);

  CoglPixelFormat
  (* get_read_pixels_format) (CoglContext     *context,
                              CoglPixelFormat  from,
                              CoglPixelFormat  to,
                              GLenum          *gl_format_out,
                              GLenum          *gl_type_out);

  gboolean
  (* update_features) (CoglContext *context,
                       GError **error);

  CoglFramebufferDriver *
  (* create_framebuffer_driver) (CoglContext                        *context,
                                 CoglFramebuffer                    *framebuffer,
                                 const CoglFramebufferDriverConfig  *driver_config,
                                 GError                            **error);

  void
  (* flush_framebuffer_state) (CoglContext          *context,
                               CoglFramebuffer      *draw_buffer,
                               CoglFramebuffer      *read_buffer,
                               CoglFramebufferState  state);

  /* Destroys any driver specific resources associated with the given
   * 2D texture. */
  void
  (* texture_2d_free) (CoglTexture2D *tex_2d);

  /* Returns TRUE if the driver can support creating a 2D texture with
   * the given geometry and specified internal format.
   */
  gboolean
  (* texture_2d_can_create) (CoglContext *ctx,
                             int width,
                             int height,
                             CoglPixelFormat internal_format);

  /* Initializes driver private state before allocating any specific
   * storage for a 2D texture, where base texture and texture 2D
   * members will already be initialized before passing control to
   * the driver.
   */
  void
  (* texture_2d_init) (CoglTexture2D *tex_2d);

  /* Allocates (uninitialized) storage for the given texture according
   * to the configured size and format of the texture */
  gboolean
  (* texture_2d_allocate) (CoglTexture *tex,
                           GError **error);

  /* Initialize the specified region of storage of the given texture
   * with the contents of the specified framebuffer region
   */
  void
  (* texture_2d_copy_from_framebuffer) (CoglTexture2D *tex_2d,
                                        int src_x,
                                        int src_y,
                                        int width,
                                        int height,
                                        CoglFramebuffer *src_fb,
                                        int dst_x,
                                        int dst_y,
                                        int level);

  /* If the given texture has a corresponding OpenGL texture handle
   * then return that.
   *
   * This is optional
   */
  unsigned int
  (* texture_2d_get_gl_handle) (CoglTexture2D *tex_2d);

  /* Update all mipmap levels > 0 */
  void
  (* texture_2d_generate_mipmap) (CoglTexture2D *tex_2d);

  /* Initialize the specified region of storage of the given texture
   * with the contents of the specified bitmap region
   *
   * Since this may need to create the underlying storage first
   * it may throw a NO_MEMORY error.
   */
  gboolean
  (* texture_2d_copy_from_bitmap) (CoglTexture2D *tex_2d,
                                   int src_x,
                                   int src_y,
                                   int width,
                                   int height,
                                   CoglBitmap *bitmap,
                                   int dst_x,
                                   int dst_y,
                                   int level,
                                   GError **error);

  gboolean
  (* texture_2d_is_get_data_supported) (CoglTexture2D *tex_2d);

  /* Reads back the full contents of the given texture and write it to
   * @data in the given @format and with the given @rowstride.
   *
   * This is optional
   */
  void
  (* texture_2d_get_data) (CoglTexture2D *tex_2d,
                           CoglPixelFormat format,
                           int rowstride,
                           uint8_t *data);

  /* Prepares for drawing by flushing the journal, framebuffer state,
   * pipeline state and attribute state.
   */
  void
  (* flush_attributes_state) (CoglFramebuffer *framebuffer,
                              CoglPipeline *pipeline,
                              CoglFlushLayerState *layer_state,
                              CoglDrawFlags flags,
                              CoglAttribute **attributes,
                              int n_attributes);

  /* Flushes the clip stack to the GPU using a combination of the
   * stencil buffer, scissor and clip plane state.
   */
  void
  (* clip_stack_flush) (CoglClipStack *stack, CoglFramebuffer *framebuffer);

  /* Enables the driver to create some meta data to represent a buffer
   * but with no corresponding storage allocated yet.
   */
  void
  (* buffer_create) (CoglBuffer *buffer);

  void
  (* buffer_destroy) (CoglBuffer *buffer);

  /* Maps a buffer into the CPU */
  void *
  (* buffer_map_range) (CoglBuffer *buffer,
                        size_t offset,
                        size_t size,
                        CoglBufferAccess access,
                        CoglBufferMapHint hints,
                        GError **error);

  /* Unmaps a buffer */
  void
  (* buffer_unmap) (CoglBuffer *buffer);

  /* Uploads data to the buffer without needing to map it necessarily
   */
  gboolean
  (* buffer_set_data) (CoglBuffer *buffer,
                       unsigned int offset,
                       const void *data,
                       unsigned int size,
                       GError **error);

  void
  (*sampler_init) (CoglContext *context,
                   CoglSamplerCacheEntry *entry);

  void
  (*sampler_free) (CoglContext *context,
                   CoglSamplerCacheEntry *entry);

  void
  (* set_uniform) (CoglContext *ctx,
                   GLint location,
                   const CoglBoxedValue *value);

  CoglTimestampQuery *
  (* create_timestamp_query) (CoglContext *context);

  void
  (* free_timestamp_query) (CoglContext *context,
                            CoglTimestampQuery *query);

  int64_t
  (* timestamp_query_get_time_ns) (CoglContext *context,
                                   CoglTimestampQuery *query);

  int64_t
  (* get_gpu_time_ns) (CoglContext *context);
};

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
