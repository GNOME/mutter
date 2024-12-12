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

/* For an overview of the functionality implemented here, please see
 * cogl-buffer.h, which contains the gtk-doc section overview for the
 * Pixel Buffers API.
 */

#include "cogl-driver-private.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "cogl/cogl-enum-types.h"
#include "cogl/cogl-util.h"
#include "cogl/cogl-context-private.h"
#include "cogl/cogl-pixel-buffer-private.h"


G_DEFINE_ABSTRACT_TYPE (CoglBuffer, cogl_buffer, G_TYPE_OBJECT)

enum
{
  PROP_0,

  PROP_CONTEXT,
  PROP_IMPL,
  PROP_SIZE,
  PROP_DEFAULT_TARGET,
  PROP_UPDATE_HINT,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST];

/*
 * Fallback path, buffer->data points to a malloc'ed buffer.
 */

static void *
malloc_map_range (CoglBuffer *buffer,
                  size_t offset,
                  size_t size,
                  CoglBufferAccess access,
                  CoglBufferMapHint hints,
                  GError **error)
{
  buffer->flags |= COGL_BUFFER_FLAG_MAPPED;
  return buffer->data + offset;
}

static void
malloc_unmap (CoglBuffer *buffer)
{
  buffer->flags &= ~COGL_BUFFER_FLAG_MAPPED;
}

static gboolean
malloc_set_data (CoglBuffer *buffer,
                 unsigned int offset,
                 const void *data,
                 unsigned int size,
                 GError **error)
{
  memcpy (buffer->data + offset, data, size);
  return TRUE;
}

static void
cogl_buffer_dispose (GObject *object)
{
  CoglBuffer *buffer = COGL_BUFFER (object);

  g_return_if_fail (!(buffer->flags & COGL_BUFFER_FLAG_MAPPED));

  if (buffer->flags & COGL_BUFFER_FLAG_BUFFER_OBJECT)
    {
      CoglBufferImplClass *impl_klass = COGL_BUFFER_IMPL_GET_CLASS (buffer->impl);

      impl_klass->destroy (buffer->impl, buffer);
    }
  else
    {
      g_free (buffer->data);
    }

  g_clear_object (&buffer->impl);

  G_OBJECT_CLASS (cogl_buffer_parent_class)->dispose (object);
}

static void
cogl_buffer_set_property (GObject      *gobject,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  CoglBuffer *buffer = COGL_BUFFER (gobject);

  switch (prop_id)
    {
    case PROP_CONTEXT:
      buffer->context = g_value_get_object (value);
      break;

    case PROP_IMPL:
      buffer->impl = g_value_get_object (value);
      break;

    case PROP_SIZE:
      buffer->size = g_value_get_uint64 (value);
      break;

    case PROP_DEFAULT_TARGET:
      {
        gboolean use_malloc = FALSE;

        buffer->last_target = g_value_get_enum (value);
        if (buffer->last_target == COGL_BUFFER_BIND_TARGET_PIXEL_PACK ||
            buffer->last_target == COGL_BUFFER_BIND_TARGET_PIXEL_UNPACK)
          {
            if (!_cogl_has_private_feature (buffer->context, COGL_PRIVATE_FEATURE_PBOS))
              use_malloc = TRUE;
          }

        buffer->use_malloc = use_malloc;
        if (use_malloc)
          {
            buffer->data = g_malloc (buffer->size);
          }
        else
          {
            g_assert (buffer->impl != NULL);
            CoglBufferImplClass *impl_klass = COGL_BUFFER_IMPL_GET_CLASS (buffer->impl);

            impl_klass->create (buffer->impl, buffer);

            buffer->flags |= COGL_BUFFER_FLAG_BUFFER_OBJECT;
          }
      }
      break;

    case PROP_UPDATE_HINT:
      buffer->update_hint = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
cogl_buffer_class_init (CoglBufferClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = cogl_buffer_dispose;
  gobject_class->set_property = cogl_buffer_set_property;

  obj_props[PROP_CONTEXT] =
    g_param_spec_object ("context", NULL, NULL,
                         COGL_TYPE_CONTEXT,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_IMPL] =
    g_param_spec_object ("impl", NULL, NULL,
                         COGL_TYPE_BUFFER_IMPL,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_SIZE] =
    g_param_spec_uint64 ("size", NULL, NULL,
                         0, G_MAXINT64, 0,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_DEFAULT_TARGET] =
    g_param_spec_enum ("default-target", NULL, NULL,
                       COGL_TYPE_BUFFER_BIND_TARGET,
                       COGL_BUFFER_BIND_TARGET_PIXEL_PACK,
                       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);
  obj_props[PROP_UPDATE_HINT] =
    g_param_spec_enum ("update-hint", NULL, NULL,
                       COGL_TYPE_BUFFER_UPDATE_HINT,
                       COGL_BUFFER_UPDATE_HINT_STATIC,
                       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     obj_props);
}

static void
cogl_buffer_init (CoglBuffer *buffer)
{
  buffer->flags = COGL_BUFFER_FLAG_NONE;
  buffer->store_created = FALSE;
  buffer->data = NULL;
}

unsigned int
cogl_buffer_get_size (CoglBuffer *buffer)
{
  g_return_val_if_fail (COGL_IS_BUFFER (buffer), 0);

  return buffer->size;
}

void
cogl_buffer_set_update_hint (CoglBuffer *buffer,
                             CoglBufferUpdateHint hint)
{
  g_return_if_fail (COGL_IS_BUFFER (buffer));

  if (G_UNLIKELY (hint > COGL_BUFFER_UPDATE_HINT_STREAM))
    hint = COGL_BUFFER_UPDATE_HINT_STATIC;

  buffer->update_hint = hint;
}

CoglBufferUpdateHint
cogl_buffer_get_update_hint (CoglBuffer *buffer)
{
  if (!COGL_IS_BUFFER (buffer))
    return FALSE;

  return buffer->update_hint;
}

void *
_cogl_buffer_map (CoglBuffer *buffer,
                  CoglBufferAccess access,
                  CoglBufferMapHint hints,
                  GError **error)
{
  g_return_val_if_fail (COGL_IS_BUFFER (buffer), NULL);

  return cogl_buffer_map_range (buffer, 0, buffer->size, access, hints, error);
}

void *
cogl_buffer_map (CoglBuffer *buffer,
                 CoglBufferAccess access,
                 CoglBufferMapHint hints)
{
  GError *ignore_error = NULL;
  void *ptr =
    cogl_buffer_map_range (buffer, 0, buffer->size, access, hints,
                           &ignore_error);
  g_clear_error (&ignore_error);
  return ptr;
}

void *
cogl_buffer_map_range (CoglBuffer *buffer,
                       size_t offset,
                       size_t size,
                       CoglBufferAccess access,
                       CoglBufferMapHint hints,
                       GError **error)
{
  g_return_val_if_fail (COGL_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (!(buffer->flags & COGL_BUFFER_FLAG_MAPPED), NULL);

  if (buffer->use_malloc)
    {
      buffer->data = malloc_map_range (buffer,
                                       offset,
                                       size,
                                       access,
                                       hints,
                                       error);
    }
  else
    {
      CoglBufferImplClass *impl_klass = COGL_BUFFER_IMPL_GET_CLASS (buffer->impl);

      buffer->data = impl_klass->map_range (buffer->impl,
                                            buffer,
                                            offset,
                                            size,
                                            access,
                                            hints,
                                            error);
    }

  return buffer->data;
}

void
cogl_buffer_unmap (CoglBuffer *buffer)
{
  g_return_if_fail (COGL_IS_BUFFER (buffer));

  if (!(buffer->flags & COGL_BUFFER_FLAG_MAPPED))
    return;

  if (buffer->use_malloc)
    {
      malloc_unmap (buffer);
    }
  else
    {
      CoglBufferImplClass *impl_klass = COGL_BUFFER_IMPL_GET_CLASS (buffer->impl);

      impl_klass->unmap (buffer->impl, buffer);
    }
}

void *
_cogl_buffer_map_range_for_fill_or_fallback (CoglBuffer *buffer,
                                             size_t offset,
                                             size_t size)
{
  CoglContext *ctx = buffer->context;
  void *ret;
  GError *ignore_error = NULL;

  g_return_val_if_fail (!ctx->buffer_map_fallback_in_use, NULL);

  ctx->buffer_map_fallback_in_use = TRUE;

  ret = cogl_buffer_map_range (buffer,
                               offset,
                               size,
                               COGL_BUFFER_ACCESS_WRITE,
                               COGL_BUFFER_MAP_HINT_DISCARD,
                               &ignore_error);

  if (ret)
    return ret;

  g_error_free (ignore_error);

  /* If the map fails then we'll use a temporary buffer to fill
     the data and then upload it using cogl_buffer_set_data when
     the buffer is unmapped. The temporary buffer is shared to
     avoid reallocating it every time */
  g_byte_array_set_size (ctx->buffer_map_fallback_array, size);
  ctx->buffer_map_fallback_offset = offset;

  buffer->flags |= COGL_BUFFER_FLAG_MAPPED_FALLBACK;

  return ctx->buffer_map_fallback_array->data;
}

void
_cogl_buffer_unmap_for_fill_or_fallback (CoglBuffer *buffer)
{
  CoglContext *ctx = buffer->context;

  g_return_if_fail (ctx->buffer_map_fallback_in_use);

  ctx->buffer_map_fallback_in_use = FALSE;

  if ((buffer->flags & COGL_BUFFER_FLAG_MAPPED_FALLBACK))
    {
      /* Note: don't try to catch OOM errors here since the use cases
       * we currently have for this api (the journal and path stroke
       * tessellator) don't have anything particularly sensible they
       * can do in response to a failure anyway so it seems better to
       * simply abort instead.
       *
       * If we find this is a problem for real world applications
       * then in the path tessellation case we could potentially add an
       * explicit cogl_path_tessellate_stroke() api that can throw an
       * error for the app to cache. For the journal we could
       * potentially flush the journal in smaller batches so we use
       * smaller buffers, though that would probably not help for
       * deferred renderers.
       */
      cogl_buffer_set_data (buffer,
                            ctx->buffer_map_fallback_offset,
                            ctx->buffer_map_fallback_array->data,
                            ctx->buffer_map_fallback_array->len);
      buffer->flags &= ~COGL_BUFFER_FLAG_MAPPED_FALLBACK;
    }
  else
    cogl_buffer_unmap (buffer);
}

gboolean
cogl_buffer_set_data (CoglBuffer *buffer,
                      size_t      offset,
                      const void *data,
                      size_t      size)
{
  GError *ignore_error = NULL;
  gboolean status;

  g_return_val_if_fail (COGL_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail ((offset + size) <= buffer->size, FALSE);

  if (buffer->use_malloc)
    {
      status = malloc_set_data (buffer, offset, data, size, &ignore_error);
    }
  else
    {
      CoglBufferImplClass *impl_klass = COGL_BUFFER_IMPL_GET_CLASS (buffer->impl);

      status = impl_klass->set_data (buffer->impl,
                                     buffer,
                                     offset,
                                     data,
                                     size,
                                     &ignore_error);
    }

  g_clear_error (&ignore_error);
  return status;
}
