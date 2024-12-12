/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2024 Red Hat.
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

#include <glib.h>

#include "cogl/cogl-buffer.h"

G_BEGIN_DECLS

#define COGL_TYPE_BUFFER_IMPL (cogl_buffer_impl_get_type ())

G_DECLARE_DERIVABLE_TYPE (CoglBufferImpl,
                          cogl_buffer_impl,
                          COGL,
                          BUFFER_IMPL,
                          GObject)

struct _CoglBufferImplClass
{
  GObjectClass parent_class;

  void (* create) (CoglBufferImpl *impl,
                   CoglBuffer     *buffer);

  void (* destroy) (CoglBufferImpl *impl,
                    CoglBuffer     *buffer);

  /* Maps a buffer into the CPU */
  void * (* map_range) (CoglBufferImpl     *impl,
                        CoglBuffer         *buffer,
                        size_t              offset,
                        size_t              size,
                        CoglBufferAccess    access,
                        CoglBufferMapHint   hints,
                        GError            **error);

  /* Unmaps a buffer */
  void (* unmap) (CoglBufferImpl *impl,
                  CoglBuffer     *buffer);

  /* Uploads data to the buffer without needing to map it necessarily
   */
  gboolean (* set_data) (CoglBufferImpl  *impl,
                         CoglBuffer      *buffer,
                         unsigned int     offset,
                         const void      *data,
                         unsigned int     size,
                         GError         **error);
};

G_END_DECLS
