/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010 Intel Corporation.
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
 *
 * Authors:
 *   Damien Lespiau <damien.lespiau@intel.com>
 *   Robert Bragg <robert@linux.intel.com>
 */

#pragma once

#include <glib.h>

#include "cogl/cogl-buffer.h"
#include "cogl/cogl-context.h"
#include "cogl/cogl-gl-header.h"

G_BEGIN_DECLS

typedef enum _CoglBufferFlags
{
  COGL_BUFFER_FLAG_NONE            = 0,
  COGL_BUFFER_FLAG_BUFFER_OBJECT   = 1UL << 0,  /* real openGL buffer object */
  COGL_BUFFER_FLAG_MAPPED          = 1UL << 1,
  COGL_BUFFER_FLAG_MAPPED_FALLBACK = 1UL << 2
} CoglBufferFlags;

struct _CoglBuffer
{
  GObject parent_instance;

  CoglContext *context;

  CoglBufferBindTarget last_target;

  CoglBufferFlags flags;

  GLuint gl_handle; /* OpenGL handle */
  unsigned int size; /* size of the buffer, in bytes */
  CoglBufferUpdateHint update_hint;

  /* points to the mapped memory when the CoglBuffer is a VBO, PBO,
   * ... or points to allocated memory in the fallback paths */
  uint8_t *data;

  int immutable_ref;

  unsigned int store_created : 1;

  void * (* map_range) (CoglBuffer       *buffer,
                        size_t            offset,
                        size_t            size,
                        CoglBufferAccess  access,
                        CoglBufferMapHint hints,
                        GError          **error);

  void (* unmap) (CoglBuffer *buffer);

  gboolean (* set_data) (CoglBuffer  *buffer,
                         unsigned int offset,
                         const void  *data,
                         unsigned int size,
                         GError     **error);
};
struct _CoglBufferClass
{
  GObjectClass parent_class;
};

CoglBuffer *
_cogl_buffer_immutable_ref (CoglBuffer *buffer);

void
_cogl_buffer_immutable_unref (CoglBuffer *buffer);

gboolean
_cogl_buffer_set_data (CoglBuffer *buffer,
                       size_t offset,
                       const void *data,
                       size_t size,
                       GError **error);

void *
_cogl_buffer_map (CoglBuffer *buffer,
                  CoglBufferAccess access,
                  CoglBufferMapHint hints,
                  GError **error);

/* This is a wrapper around cogl_buffer_map_range for internal use
   when we want to map the buffer for write only to replace the entire
   contents. If the map fails then it will fallback to writing to a
   temporary buffer. When _cogl_buffer_unmap_for_fill_or_fallback is
   called the temporary buffer will be copied into the array. Note
   that these calls share a global array so they can not be nested. */
void *
_cogl_buffer_map_range_for_fill_or_fallback (CoglBuffer *buffer,
                                             size_t offset,
                                             size_t size);

void
_cogl_buffer_unmap_for_fill_or_fallback (CoglBuffer *buffer);

G_END_DECLS
