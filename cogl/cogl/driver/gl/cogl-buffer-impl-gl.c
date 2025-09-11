/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2010,2011,2012,2013 Intel Corporation.
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

#include "config.h"

#include "cogl/cogl-context-private.h"
#include "cogl/driver/gl/cogl-buffer-impl-gl-private.h"
#include "cogl/driver/gl/cogl-driver-gl-private.h"

struct _CoglBufferImplGL
{
  CoglBufferImpl parent_instance;

  GLuint gl_handle; /* OpenGL handle */
};

G_DEFINE_FINAL_TYPE (CoglBufferImplGL, cogl_buffer_impl_gl, COGL_TYPE_BUFFER_IMPL)

/*
 * GL/GLES compatibility defines for the buffer API:
 */

#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_RANGE_BIT
#define GL_MAP_INVALIDATE_RANGE_BIT 0x0004
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif

static void
cogl_buffer_impl_gl_create (CoglBufferImpl *impl,
                            CoglBuffer     *buffer)
{
  CoglBufferImplGL *gl_impl = COGL_BUFFER_IMPL_GL (impl);
  CoglDriver *driver = cogl_context_get_driver (buffer->context);

  GE (driver, glGenBuffers (1, &gl_impl->gl_handle));
}

static void
cogl_buffer_impl_gl_destroy (CoglBufferImpl *impl,
                             CoglBuffer     *buffer)
{
  CoglBufferImplGL *gl_impl = COGL_BUFFER_IMPL_GL (impl);
  CoglDriver *driver = cogl_context_get_driver (buffer->context);

  GE (driver, glDeleteBuffers (1, &gl_impl->gl_handle));
}

static GLenum
update_hints_to_gl_enum (CoglBuffer *buffer)
{
  /* usage hint is always DRAW for now */
  switch (buffer->update_hint)
    {
    case COGL_BUFFER_UPDATE_HINT_STATIC:
      return GL_STATIC_DRAW;
    case COGL_BUFFER_UPDATE_HINT_DYNAMIC:
      return GL_DYNAMIC_DRAW;
    case COGL_BUFFER_UPDATE_HINT_STREAM:
      return GL_STREAM_DRAW;
    }

  g_assert_not_reached ();
  return 0;
}

static GLenum
convert_bind_target_to_gl_target (CoglBufferBindTarget target)
{
  switch (target)
    {
      case COGL_BUFFER_BIND_TARGET_PIXEL_PACK:
        return GL_PIXEL_PACK_BUFFER;
      case COGL_BUFFER_BIND_TARGET_PIXEL_UNPACK:
        return GL_PIXEL_UNPACK_BUFFER;
      case COGL_BUFFER_BIND_TARGET_ATTRIBUTE_BUFFER:
        return GL_ARRAY_BUFFER;
      case COGL_BUFFER_BIND_TARGET_INDEX_BUFFER:
        return GL_ELEMENT_ARRAY_BUFFER;
      default:
        g_return_val_if_reached (COGL_BUFFER_BIND_TARGET_PIXEL_UNPACK);
    }
}

static gboolean
recreate_store (CoglBuffer *buffer,
                GError **error)
{
  CoglContext *ctx = buffer->context;
  CoglDriver *driver = cogl_context_get_driver (ctx);
  GLenum gl_target;
  GLenum gl_enum;

  /* This assumes the buffer is already bound */

  gl_target = convert_bind_target_to_gl_target (buffer->last_target);
  gl_enum = update_hints_to_gl_enum (buffer);

  /* Clear any GL errors */
  cogl_driver_gl_clear_gl_errors (COGL_DRIVER_GL (driver));

  GE (driver, glBufferData (gl_target,
                            buffer->size,
                            NULL,
                            gl_enum));

  if (cogl_driver_gl_catch_out_of_memory (COGL_DRIVER_GL (driver), error))
    return FALSE;

  buffer->store_created = TRUE;
  return TRUE;
}

static GLenum
_cogl_buffer_access_to_gl_enum (CoglBufferAccess access)
{
  if ((access & COGL_BUFFER_ACCESS_READ_WRITE) == COGL_BUFFER_ACCESS_READ_WRITE)
    return GL_READ_WRITE;
  else if (access & COGL_BUFFER_ACCESS_WRITE)
    return GL_WRITE_ONLY;
  else
    return GL_READ_ONLY;
}

static void *
cogl_buffer_impl_gl_bind_no_create (CoglBufferImplGL     *gl_impl,
                                    CoglBuffer           *buffer,
                                    CoglBufferBindTarget  target)
{
  CoglContext *ctx = buffer->context;

  g_return_val_if_fail (buffer != NULL, NULL);

  /* Don't allow binding the buffer to multiple targets at the same time */
  g_return_val_if_fail (ctx->current_buffer[buffer->last_target] != buffer,
                        NULL);

  /* Don't allow nesting binds to the same target */
  g_return_val_if_fail (ctx->current_buffer[target] == NULL, NULL);

  buffer->last_target = target;
  ctx->current_buffer[target] = buffer;

  if (buffer->flags & COGL_BUFFER_FLAG_BUFFER_OBJECT)
    {
      GLenum gl_target = convert_bind_target_to_gl_target (buffer->last_target);
      CoglDriver *driver = cogl_context_get_driver (ctx);

      GE (driver, glBindBuffer (gl_target, gl_impl->gl_handle));

      return NULL;
    }
  else
    return buffer->data;
}

static void *
cogl_buffer_impl_gl_map_range (CoglBufferImpl     *impl,
                               CoglBuffer         *buffer,
                               size_t              offset,
                               size_t              size,
                               CoglBufferAccess    access,
                               CoglBufferMapHint   hints,
                               GError            **error)
{
  uint8_t *data;
  CoglBufferBindTarget target;
  GLenum gl_target;
  CoglContext *ctx = buffer->context;
  CoglDriver *driver = cogl_context_get_driver (ctx);

  if (((access & COGL_BUFFER_ACCESS_READ) &&
       !cogl_context_has_feature (ctx, COGL_FEATURE_ID_MAP_BUFFER_FOR_READ)) ||
      ((access & COGL_BUFFER_ACCESS_WRITE) &&
       !cogl_context_has_feature (ctx, COGL_FEATURE_ID_MAP_BUFFER_FOR_WRITE)))
    {
      g_set_error_literal (error,
                           COGL_SYSTEM_ERROR,
                           COGL_SYSTEM_ERROR_UNSUPPORTED,
                           "Tried to map a buffer with unsupported access mode");
      return NULL;
    }

  target = buffer->last_target;
  cogl_buffer_impl_gl_bind_no_create (COGL_BUFFER_IMPL_GL (buffer->impl),
                                      buffer,
                                      target);

  gl_target = convert_bind_target_to_gl_target (target);

  if ((hints & COGL_BUFFER_MAP_HINT_DISCARD_RANGE) &&
      offset == 0 && size >= buffer->size)
    hints |= COGL_BUFFER_MAP_HINT_DISCARD;

  /* If the map buffer range extension is supported then we will
   * always use it even if we are mapping the full range because the
   * normal mapping function doesn't support passing the discard
   * hints */
  if (GE_HAS (driver, glMapBufferRange))
    {
      GLbitfield gl_access = 0;
      gboolean should_recreate_store = !buffer->store_created;

      if ((access & COGL_BUFFER_ACCESS_READ))
        gl_access |= GL_MAP_READ_BIT;
      if ((access & COGL_BUFFER_ACCESS_WRITE))
        gl_access |= GL_MAP_WRITE_BIT;

      if ((hints & COGL_BUFFER_MAP_HINT_DISCARD))
        {
          /* glMapBufferRange generates an error if you pass the
           * discard hint along with asking for read access. However
           * it can make sense to ask for both if write access is also
           * requested so that the application can immediately read
           * back what it just wrote. To work around the restriction
           * in GL we just recreate the buffer storage in that case
           * which is an alternative way to indicate that the buffer
           * contents can be discarded. */
          if ((access & COGL_BUFFER_ACCESS_READ))
            should_recreate_store = TRUE;
          else
            gl_access |= GL_MAP_INVALIDATE_BUFFER_BIT;
        }
      else if ((hints & COGL_BUFFER_MAP_HINT_DISCARD_RANGE) &&
               !(access & COGL_BUFFER_ACCESS_READ))
        gl_access |= GL_MAP_INVALIDATE_RANGE_BIT;

      if (should_recreate_store)
        {
          if (!recreate_store (buffer, error))
            {
              _cogl_buffer_gl_unbind (buffer);
              return NULL;
            }
        }

      /* Clear any GL errors */
      cogl_driver_gl_clear_gl_errors (COGL_DRIVER_GL (driver));

      GE_RET (data, driver, glMapBufferRange (gl_target,
                                              offset,
                                              size,
                                              gl_access));

      if (cogl_driver_gl_catch_out_of_memory (COGL_DRIVER_GL (driver), error))
        {
          _cogl_buffer_gl_unbind (buffer);
          return NULL;
        }

      g_return_val_if_fail (data != NULL, NULL);
    }
  else
    {
      /* create an empty store if we don't have one yet. creating the store
       * lazily allows the user of the CoglBuffer to set a hint before the
       * store is created. */
      if (!buffer->store_created ||
          (hints & COGL_BUFFER_MAP_HINT_DISCARD))
        {
          if (!recreate_store (buffer, error))
            {
              _cogl_buffer_gl_unbind (buffer);
              return NULL;
            }
        }

      /* Clear any GL errors */
      cogl_driver_gl_clear_gl_errors (COGL_DRIVER_GL (driver));

      GE_RET (data, driver, glMapBuffer (gl_target,
                                         _cogl_buffer_access_to_gl_enum (access)));

      if (cogl_driver_gl_catch_out_of_memory (COGL_DRIVER_GL (driver), error))
        {
          _cogl_buffer_gl_unbind (buffer);
          return NULL;
        }

      g_return_val_if_fail (data != NULL, NULL);

      data += offset;
    }

  if (data)
    buffer->flags |= COGL_BUFFER_FLAG_MAPPED;

  _cogl_buffer_gl_unbind (buffer);

  return data;
}

static void
cogl_buffer_impl_gl_unmap (CoglBufferImpl *impl,
                           CoglBuffer     *buffer)
{
  CoglContext *ctx = buffer->context;
  CoglDriver *driver = cogl_context_get_driver (ctx);

  cogl_buffer_impl_gl_bind_no_create (COGL_BUFFER_IMPL_GL (impl),
                                      buffer,
                                      buffer->last_target);

  GE (driver, glUnmapBuffer (convert_bind_target_to_gl_target
                             (buffer->last_target)));
  buffer->flags &= ~COGL_BUFFER_FLAG_MAPPED;

  _cogl_buffer_gl_unbind (buffer);
}

static gboolean
cogl_buffer_impl_gl_set_data (CoglBufferImpl  *impl,
                              CoglBuffer      *buffer,
                              unsigned int     offset,
                              const void      *data,
                              unsigned int     size,
                              GError         **error)
{
  CoglBufferBindTarget target;
  GLenum gl_target;
  CoglContext *ctx = buffer->context;
  CoglDriver *driver = cogl_context_get_driver (ctx);
  gboolean status = TRUE;
  GError *internal_error = NULL;

  target = buffer->last_target;

  _cogl_buffer_gl_bind (buffer, target, &internal_error);

  /* NB: _cogl_buffer_gl_bind() may return NULL in non-error
   * conditions so we have to explicitly check internal_error
   * to see if an exception was thrown.
   */
  if (internal_error)
    {
      g_propagate_error (error, internal_error);
      return FALSE;
    }

  gl_target = convert_bind_target_to_gl_target (target);

  /* Clear any GL errors */
  cogl_driver_gl_clear_gl_errors (COGL_DRIVER_GL (driver));

  GE (driver, glBufferSubData (gl_target, offset, size, data));

  if (cogl_driver_gl_catch_out_of_memory (COGL_DRIVER_GL (driver), error))
    status = FALSE;

  _cogl_buffer_gl_unbind (buffer);

  return status;
}

void *
_cogl_buffer_gl_bind (CoglBuffer *buffer,
                      CoglBufferBindTarget target,
                      GError **error)
{
  void *ret;

  ret = cogl_buffer_impl_gl_bind_no_create (COGL_BUFFER_IMPL_GL (buffer->impl),
                                            buffer,
                                            target);

  /* create an empty store if we don't have one yet. creating the store
   * lazily allows the user of the CoglBuffer to set a hint before the
   * store is created. */
  if ((buffer->flags & COGL_BUFFER_FLAG_BUFFER_OBJECT) &&
      !buffer->store_created)
    {
      if (!recreate_store (buffer, error))
        {
          _cogl_buffer_gl_unbind (buffer);
          return NULL;
        }
    }

  return ret;
}

void
_cogl_buffer_gl_unbind (CoglBuffer *buffer)
{
  CoglContext *ctx = buffer->context;

  g_return_if_fail (buffer != NULL);

  /* the unbind should pair up with a previous bind */
  g_return_if_fail (ctx->current_buffer[buffer->last_target] == buffer);

  if (buffer->flags & COGL_BUFFER_FLAG_BUFFER_OBJECT)
    {
      CoglDriver *driver = cogl_context_get_driver (ctx);

      GLenum gl_target = convert_bind_target_to_gl_target (buffer->last_target);

      GE (driver, glBindBuffer (gl_target, 0));
    }

  ctx->current_buffer[buffer->last_target] = NULL;
}

static void
cogl_buffer_impl_gl_class_init (CoglBufferImplGLClass *klass)
{
  CoglBufferImplClass *buffer_klass = COGL_BUFFER_IMPL_CLASS (klass);

  buffer_klass->create = cogl_buffer_impl_gl_create;
  buffer_klass->destroy = cogl_buffer_impl_gl_destroy;
  buffer_klass->map_range = cogl_buffer_impl_gl_map_range;
  buffer_klass->unmap = cogl_buffer_impl_gl_unmap;
  buffer_klass->set_data = cogl_buffer_impl_gl_set_data;
}

static void
cogl_buffer_impl_gl_init (CoglBufferImplGL *impl)
{
}
