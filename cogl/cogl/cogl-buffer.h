/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C)2010 Intel Corporation.
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

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-types.h"

G_BEGIN_DECLS

/**
 * CoglBuffer:
 *
 * Common buffer functions, including data upload APIs
 *
 * The CoglBuffer API provides a common interface to manipulate
 * buffers that have been allocated either via `cogl_pixel_buffer_new()`
 * or `cogl_attribute_buffer_new()`. The API allows you to upload data
 * to these buffers and define usage hints that help Cogl manage your
 * buffer optimally.
 *
 * Data can either be uploaded by supplying a pointer and size so Cogl
 * can copy your data, or you can mmap() a CoglBuffer and then you can
 * copy data to the buffer directly.
 *
 * One of the most common uses for CoglBuffers is to upload texture
 * data asynchronously since the ability to mmap the buffers into
 * the CPU makes it possible for another thread to handle the IO
 * of loading an image file and unpacking it into the mapped buffer
 * without blocking other Cogl operations.
 */
#define COGL_TYPE_BUFFER            (cogl_buffer_get_type ())
#define COGL_BUFFER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_BUFFER, CoglBuffer))
#define COGL_BUFFER_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), COGL_TYPE_BUFFER, CoglBuffer const))
#define COGL_BUFFER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  COGL_TYPE_BUFFER, CoglBufferClass))
#define COGL_IS_BUFFER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), COGL_TYPE_BUFFER))
#define COGL_IS_BUFFER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  COGL_TYPE_BUFFER))
#define COGL_BUFFER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  COGL_TYPE_BUFFER, CoglBufferClass))

typedef struct _CoglBufferClass CoglBufferClass;
typedef struct _CoglBuffer CoglBuffer;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CoglBuffer, g_object_unref)

COGL_EXPORT
GType       cogl_buffer_get_type (void) G_GNUC_CONST;

#define COGL_BUFFER_ERROR (_cogl_buffer_error_domain ())

/**
 * CoglBufferError:
 * @COGL_BUFFER_ERROR_MAP: A buffer could not be mapped either
 *    because the feature isn't supported or because a system
 *    limitation was hit.
 *
 * Error enumeration for #CoglBuffer
 */
typedef enum /*< prefix=COGL_BUFFER_ERROR >*/
{
  COGL_BUFFER_ERROR_MAP
} CoglBufferError;

uint32_t
_cogl_buffer_error_domain (void);

/**
 * cogl_buffer_get_size:
 * @buffer: a buffer object
 *
 * Retrieves the size of buffer
 *
 * Return value: the size of the buffer in bytes
 */
COGL_EXPORT unsigned int
cogl_buffer_get_size (CoglBuffer *buffer);

/**
 * CoglBufferUpdateHint:
 * @COGL_BUFFER_UPDATE_HINT_STATIC: the buffer will not change over time
 * @COGL_BUFFER_UPDATE_HINT_DYNAMIC: the buffer will change from time to time
 * @COGL_BUFFER_UPDATE_HINT_STREAM: the buffer will be used once or a couple of
 *   times
 *
 * The update hint on a buffer allows the user to give some detail on how often
 * the buffer data is going to be updated.
 */
typedef enum /*< prefix=COGL_BUFFER_UPDATE_HINT >*/
{
  COGL_BUFFER_UPDATE_HINT_STATIC,
  COGL_BUFFER_UPDATE_HINT_DYNAMIC,
  COGL_BUFFER_UPDATE_HINT_STREAM
} CoglBufferUpdateHint;

/**
 * cogl_buffer_set_update_hint:
 * @buffer: a buffer object
 * @hint: the new hint
 *
 * Sets the update hint on a buffer. See #CoglBufferUpdateHint for a description
 * of the available hints.
 */
COGL_EXPORT void
cogl_buffer_set_update_hint (CoglBuffer          *buffer,
                             CoglBufferUpdateHint hint);

/**
 * cogl_buffer_get_update_hint:
 * @buffer: a buffer object
 *
 * Retrieves the update hints set using [method@Cogl.Buffer.set_update_hint]
 *
 * Return value: the #CoglBufferUpdateHint currently used by the buffer
 */
COGL_EXPORT CoglBufferUpdateHint
cogl_buffer_get_update_hint (CoglBuffer *buffer);

/**
 * CoglBufferAccess:
 * @COGL_BUFFER_ACCESS_READ: the buffer will be read
 * @COGL_BUFFER_ACCESS_WRITE: the buffer will written to
 * @COGL_BUFFER_ACCESS_READ_WRITE: the buffer will be used for both reading and
 *   writing
 *
 * The access hints for [method@Cogl.Buffer.set_update_hint]
 */
typedef enum /*< prefix=COGL_BUFFER_ACCESS >*/
{
 COGL_BUFFER_ACCESS_READ       = 1 << 0,
 COGL_BUFFER_ACCESS_WRITE      = 1 << 1,
 COGL_BUFFER_ACCESS_READ_WRITE = COGL_BUFFER_ACCESS_READ | COGL_BUFFER_ACCESS_WRITE
} CoglBufferAccess;


/**
 * CoglBufferMapHint:
 * @COGL_BUFFER_MAP_HINT_DISCARD: Tells Cogl that you plan to replace
 *    all the buffer's contents. When this flag is used to map a
 *    buffer, the entire contents of the buffer become undefined, even
 *    if only a subregion of the buffer is mapped.
 * @COGL_BUFFER_MAP_HINT_DISCARD_RANGE: Tells Cogl that you plan to
 *    replace all the contents of the mapped region. The contents of
 *    the region specified are undefined after this flag is used to
 *    map a buffer.
 *
 * Hints to Cogl about how you are planning to modify the data once it
 * is mapped.
 */
typedef enum /*< prefix=COGL_BUFFER_MAP_HINT >*/
{
  COGL_BUFFER_MAP_HINT_DISCARD = 1 << 0,
  COGL_BUFFER_MAP_HINT_DISCARD_RANGE = 1 << 1
} CoglBufferMapHint;

/**
 * CoglBufferBindTarget:
 */
typedef enum /*< prefix=COGL_BUFFER_BIND_TARGET >*/
{
  COGL_BUFFER_BIND_TARGET_PIXEL_PACK,
  COGL_BUFFER_BIND_TARGET_PIXEL_UNPACK,
  COGL_BUFFER_BIND_TARGET_ATTRIBUTE_BUFFER,
  COGL_BUFFER_BIND_TARGET_INDEX_BUFFER,

  COGL_BUFFER_BIND_TARGET_COUNT
} CoglBufferBindTarget;

/**
 * cogl_buffer_map:
 * @buffer: a buffer object
 * @access: how the mapped buffer will be used by the application
 * @hints: A mask of `CoglBufferMapHint`s that tell Cogl how
 *   the data will be modified once mapped.
 *
 * Maps the buffer into the application address space for direct
 * access. This is equivalent to calling [method@Cogl.Buffer.map_range] with
 * zero as the offset and the size of the entire buffer as the size.
 *
 * It is strongly recommended that you pass
 * %COGL_BUFFER_MAP_HINT_DISCARD as a hint if you are going to replace
 * all the buffer's data. This way if the buffer is currently being
 * used by the GPU then the driver won't have to stall the CPU and
 * wait for the hardware to finish because it can instead allocate a
 * new buffer to map.
 *
 * The behaviour is undefined if you access the buffer in a way
 * conflicting with the @access mask you pass. It is also an error to
 * release your last reference while the buffer is mapped.
 *
 * Return value: (transfer none): A pointer to the mapped memory or
 *        %NULL is the call fails
 */
COGL_EXPORT void *
cogl_buffer_map (CoglBuffer *buffer,
                 CoglBufferAccess access,
                 CoglBufferMapHint hints);

/**
 * cogl_buffer_map_range:
 * @buffer: a buffer object
 * @offset: Offset within the buffer to start the mapping
 * @size: The size of data to map
 * @access: how the mapped buffer will be used by the application
 * @hints: A mask of `CoglBufferMapHint`s that tell Cogl how
 *   the data will be modified once mapped.
 * @error: A #GError for catching exceptional errors
 *
 * Maps a sub-region of the buffer into the application's address space
 * for direct access.
 *
 * It is strongly recommended that you pass
 * %COGL_BUFFER_MAP_HINT_DISCARD as a hint if you are going to replace
 * all the buffer's data. This way if the buffer is currently being
 * used by the GPU then the driver won't have to stall the CPU and
 * wait for the hardware to finish because it can instead allocate a
 * new buffer to map. You can pass
 * %COGL_BUFFER_MAP_HINT_DISCARD_RANGE instead if you want the
 * regions outside of the mapping to be retained.
 *
 * The behaviour is undefined if you access the buffer in a way
 * conflicting with the @access mask you pass. It is also an error to
 * release your last reference while the buffer is mapped.
 *
 * Return value: (transfer none): A pointer to the mapped memory or
 *        %NULL is the call fails
 */
COGL_EXPORT void *
cogl_buffer_map_range (CoglBuffer *buffer,
                       size_t offset,
                       size_t size,
                       CoglBufferAccess access,
                       CoglBufferMapHint hints,
                       GError **error);

/**
 * cogl_buffer_unmap:
 * @buffer: a buffer object
 *
 * Unmaps a buffer previously mapped by [method@Cogl.Buffer.map].
 */
COGL_EXPORT void
cogl_buffer_unmap (CoglBuffer *buffer);

/**
 * cogl_buffer_set_data:
 * @buffer: a buffer object
 * @offset: destination offset (in bytes) in the buffer
 * @data: (array) (element-type guint8): a pointer to the data to be copied
 *   into the buffer
 * @size: number of bytes to copy
 *
 * Updates part of the buffer with new data from @data. Where to put this new
 * data is controlled by @offset and @offset + @data should be less than the
 * buffer size.
 *
 * Return value: %TRUE is the operation succeeded, %FALSE otherwise
 */
COGL_EXPORT gboolean
cogl_buffer_set_data (CoglBuffer *buffer,
                      size_t offset,
                      const void *data,
                      size_t size);

G_END_DECLS
