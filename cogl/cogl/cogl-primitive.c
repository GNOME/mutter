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
 *   Robert Bragg <robert@linux.intel.com>
 */

#include "config.h"

#include "cogl/cogl-util.h"
#include "cogl/cogl-primitive.h"
#include "cogl/cogl-primitive-private.h"
#include "cogl/cogl-attribute-private.h"
#include "cogl/cogl-framebuffer-private.h"

#include <stdarg.h>
#include <string.h>

G_DEFINE_TYPE (CoglPrimitive, cogl_primitive, G_TYPE_OBJECT);

static void
cogl_primitive_dispose (GObject *object)
{
  CoglPrimitive *primitive = COGL_PRIMITIVE (object);

  g_ptr_array_free (primitive->attributes, TRUE);

  if (primitive->indices)
    g_object_unref (primitive->indices);

  G_OBJECT_CLASS (cogl_primitive_parent_class)->dispose (object);
}

static void
cogl_primitive_class_init (CoglPrimitiveClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = cogl_primitive_dispose;
}

static void
cogl_primitive_init (CoglPrimitive *primitive)
{
  primitive->first_vertex = 0;
  primitive->immutable_ref = 0;
  primitive->indices = NULL;
  primitive->attributes = g_ptr_array_new_with_free_func (g_object_unref);
}

CoglPrimitive *
cogl_primitive_new_with_attributes (CoglVerticesMode mode,
                                    int n_vertices,
                                    CoglAttribute **attributes,
                                    int n_attributes)
{
  CoglPrimitive *primitive;
  int i;

  primitive = g_object_new (COGL_TYPE_PRIMITIVE, NULL);
  primitive->mode = mode;
  primitive->n_vertices = n_vertices;

  primitive->n_attributes = n_attributes;
  for (i = 0; i < n_attributes; i++)
    {
      CoglAttribute *attribute = attributes[i];
      g_object_ref (attribute);

      g_return_val_if_fail (COGL_IS_ATTRIBUTE (attribute), NULL);

      g_ptr_array_add (primitive->attributes, attribute);
    }

  return primitive;
}

/* This is just an internal convenience wrapper around
   new_with_attributes that also unrefs the attributes. It is just
   used for the builtin struct constructors */
static CoglPrimitive *
_cogl_primitive_new_with_attributes_unref (CoglVerticesMode mode,
                                           int n_vertices,
                                           CoglAttribute **attributes,
                                           int n_attributes)
{
  CoglPrimitive *primitive;
  int i;

  primitive = cogl_primitive_new_with_attributes (mode,
                                                  n_vertices,
                                                  attributes,
                                                  n_attributes);

  for (i = 0; i < n_attributes; i++)
    g_object_unref (attributes[i]);

  return primitive;
}

CoglPrimitive *
cogl_primitive_new (CoglVerticesMode mode,
                    int n_vertices,
                    ...)
{
  va_list ap;
  int n_attributes;
  CoglAttribute **attributes;
  int i;
  CoglAttribute *attribute;

  va_start (ap, n_vertices);
  for (n_attributes = 0; va_arg (ap, CoglAttribute *); n_attributes++)
    ;
  va_end (ap);

  attributes = g_alloca (sizeof (CoglAttribute *) * n_attributes);

  va_start (ap, n_vertices);
  for (i = 0; (attribute = va_arg (ap, CoglAttribute *)); i++)
    attributes[i] = attribute;
  va_end (ap);

  return cogl_primitive_new_with_attributes (mode, n_vertices,
                                             attributes,
                                             i);
}

CoglPrimitive *
cogl_primitive_new_p2 (CoglContext *ctx,
                       CoglVerticesMode mode,
                       int n_vertices,
                       const CoglVertexP2 *data)
{
  CoglAttributeBuffer *attribute_buffer =
    cogl_attribute_buffer_new (ctx, n_vertices * sizeof (CoglVertexP2), data);
  CoglAttribute *attributes[1];

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (CoglVertexP2),
                                      offsetof (CoglVertexP2, x),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  g_object_unref (attribute_buffer);

  return _cogl_primitive_new_with_attributes_unref (mode, n_vertices,
                                                    attributes,
                                                    1);
}

CoglPrimitive *
cogl_primitive_new_p3 (CoglContext *ctx,
                       CoglVerticesMode mode,
                       int n_vertices,
                       const CoglVertexP3 *data)
{
  CoglAttributeBuffer *attribute_buffer =
    cogl_attribute_buffer_new (ctx, n_vertices * sizeof (CoglVertexP3), data);
  CoglAttribute *attributes[1];

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (CoglVertexP3),
                                      offsetof (CoglVertexP3, x),
                                      3,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  g_object_unref (attribute_buffer);

  return _cogl_primitive_new_with_attributes_unref (mode, n_vertices,
                                                    attributes,
                                                    1);
}

CoglPrimitive *
cogl_primitive_new_p2c4 (CoglContext *ctx,
                         CoglVerticesMode mode,
                         int n_vertices,
                         const CoglVertexP2C4 *data)
{
  CoglAttributeBuffer *attribute_buffer =
    cogl_attribute_buffer_new (ctx, n_vertices * sizeof (CoglVertexP2C4), data);
  CoglAttribute *attributes[2];

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (CoglVertexP2C4),
                                      offsetof (CoglVertexP2C4, x),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[1] = cogl_attribute_new (attribute_buffer,
                                      "cogl_color_in",
                                      sizeof (CoglVertexP2C4),
                                      offsetof (CoglVertexP2C4, r),
                                      4,
                                      COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE);

  g_object_unref (attribute_buffer);

  return _cogl_primitive_new_with_attributes_unref (mode, n_vertices,
                                                    attributes,
                                                    2);
}

CoglPrimitive *
cogl_primitive_new_p3c4 (CoglContext *ctx,
                         CoglVerticesMode mode,
                         int n_vertices,
                         const CoglVertexP3C4 *data)
{
  CoglAttributeBuffer *attribute_buffer =
    cogl_attribute_buffer_new (ctx, n_vertices * sizeof (CoglVertexP3C4), data);
  CoglAttribute *attributes[2];

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (CoglVertexP3C4),
                                      offsetof (CoglVertexP3C4, x),
                                      3,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[1] = cogl_attribute_new (attribute_buffer,
                                      "cogl_color_in",
                                      sizeof (CoglVertexP3C4),
                                      offsetof (CoglVertexP3C4, r),
                                      4,
                                      COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE);

  g_object_unref (attribute_buffer);

  return _cogl_primitive_new_with_attributes_unref (mode, n_vertices,
                                                    attributes,
                                                    2);
}

CoglPrimitive *
cogl_primitive_new_p2t2 (CoglContext *ctx,
                         CoglVerticesMode mode,
                         int n_vertices,
                         const CoglVertexP2T2 *data)
{
  CoglAttributeBuffer *attribute_buffer =
    cogl_attribute_buffer_new (ctx, n_vertices * sizeof (CoglVertexP2T2), data);
  CoglAttribute *attributes[2];

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (CoglVertexP2T2),
                                      offsetof (CoglVertexP2T2, x),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[1] = cogl_attribute_new (attribute_buffer,
                                      "cogl_tex_coord0_in",
                                      sizeof (CoglVertexP2T2),
                                      offsetof (CoglVertexP2T2, s),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  g_object_unref (attribute_buffer);

  return _cogl_primitive_new_with_attributes_unref (mode, n_vertices,
                                                    attributes,
                                                    2);
}

CoglPrimitive *
cogl_primitive_new_p3t2 (CoglContext *ctx,
                         CoglVerticesMode mode,
                         int n_vertices,
                         const CoglVertexP3T2 *data)
{
  CoglAttributeBuffer *attribute_buffer =
    cogl_attribute_buffer_new (ctx, n_vertices * sizeof (CoglVertexP3T2), data);
  CoglAttribute *attributes[2];

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (CoglVertexP3T2),
                                      offsetof (CoglVertexP3T2, x),
                                      3,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[1] = cogl_attribute_new (attribute_buffer,
                                      "cogl_tex_coord0_in",
                                      sizeof (CoglVertexP3T2),
                                      offsetof (CoglVertexP3T2, s),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);

  g_object_unref (attribute_buffer);

  return _cogl_primitive_new_with_attributes_unref (mode, n_vertices,
                                                    attributes,
                                                    2);
}

CoglPrimitive *
cogl_primitive_new_p2t2c4 (CoglContext *ctx,
                           CoglVerticesMode mode,
                           int n_vertices,
                           const CoglVertexP2T2C4 *data)
{
  CoglAttributeBuffer *attribute_buffer =
    cogl_attribute_buffer_new (ctx,
                               n_vertices * sizeof (CoglVertexP2T2C4), data);
  CoglAttribute *attributes[3];

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (CoglVertexP2T2C4),
                                      offsetof (CoglVertexP2T2C4, x),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[1] = cogl_attribute_new (attribute_buffer,
                                      "cogl_tex_coord0_in",
                                      sizeof (CoglVertexP2T2C4),
                                      offsetof (CoglVertexP2T2C4, s),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[2] = cogl_attribute_new (attribute_buffer,
                                      "cogl_color_in",
                                      sizeof (CoglVertexP2T2C4),
                                      offsetof (CoglVertexP2T2C4, r),
                                      4,
                                      COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE);

  g_object_unref (attribute_buffer);

  return _cogl_primitive_new_with_attributes_unref (mode, n_vertices,
                                                    attributes,
                                                    3);
}

CoglPrimitive *
cogl_primitive_new_p3t2c4 (CoglContext *ctx,
                           CoglVerticesMode mode,
                           int n_vertices,
                           const CoglVertexP3T2C4 *data)
{
  CoglAttributeBuffer *attribute_buffer =
    cogl_attribute_buffer_new (ctx,
                               n_vertices * sizeof (CoglVertexP3T2C4), data);
  CoglAttribute *attributes[3];

  attributes[0] = cogl_attribute_new (attribute_buffer,
                                      "cogl_position_in",
                                      sizeof (CoglVertexP3T2C4),
                                      offsetof (CoglVertexP3T2C4, x),
                                      3,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[1] = cogl_attribute_new (attribute_buffer,
                                      "cogl_tex_coord0_in",
                                      sizeof (CoglVertexP3T2C4),
                                      offsetof (CoglVertexP3T2C4, s),
                                      2,
                                      COGL_ATTRIBUTE_TYPE_FLOAT);
  attributes[2] = cogl_attribute_new (attribute_buffer,
                                      "cogl_color_in",
                                      sizeof (CoglVertexP3T2C4),
                                      offsetof (CoglVertexP3T2C4, r),
                                      4,
                                      COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE);

  g_object_unref (attribute_buffer);

  return _cogl_primitive_new_with_attributes_unref (mode, n_vertices,
                                                    attributes,
                                                    3);
}

static void
warn_about_midscene_changes (void)
{
  static gboolean seen = FALSE;
  if (!seen)
    {
      g_warning ("Mid-scene modification of primitives has "
                 "undefined results\n");
      seen = TRUE;
    }
}

int
cogl_primitive_get_first_vertex (CoglPrimitive *primitive)
{
  g_return_val_if_fail (COGL_IS_PRIMITIVE (primitive), 0);

  return primitive->first_vertex;
}

void
cogl_primitive_set_first_vertex (CoglPrimitive *primitive,
                                 int first_vertex)
{
  g_return_if_fail (COGL_IS_PRIMITIVE (primitive));

  if (G_UNLIKELY (primitive->immutable_ref))
    {
      warn_about_midscene_changes ();
      return;
    }

  primitive->first_vertex = first_vertex;
}

int
cogl_primitive_get_n_vertices (CoglPrimitive *primitive)
{
  g_return_val_if_fail (COGL_IS_PRIMITIVE (primitive), 0);

  return primitive->n_vertices;
}

void
cogl_primitive_set_n_vertices (CoglPrimitive *primitive,
                               int n_vertices)
{
  g_return_if_fail (COGL_IS_PRIMITIVE (primitive));

  primitive->n_vertices = n_vertices;
}

CoglVerticesMode
cogl_primitive_get_mode (CoglPrimitive *primitive)
{
  g_return_val_if_fail (COGL_IS_PRIMITIVE (primitive), 0);

  return primitive->mode;
}

void
cogl_primitive_set_mode (CoglPrimitive *primitive,
                         CoglVerticesMode mode)
{
  g_return_if_fail (COGL_IS_PRIMITIVE (primitive));

  if (G_UNLIKELY (primitive->immutable_ref))
    {
      warn_about_midscene_changes ();
      return;
    }

  primitive->mode = mode;
}

void
cogl_primitive_set_indices (CoglPrimitive *primitive,
                            CoglIndices *indices,
                            int n_indices)
{
  g_return_if_fail (COGL_IS_PRIMITIVE (primitive));

  if (G_UNLIKELY (primitive->immutable_ref))
    {
      warn_about_midscene_changes ();
      return;
    }

  if (indices)
    g_object_ref (indices);
  if (primitive->indices)
    g_object_unref (primitive->indices);
  primitive->indices = indices;
  primitive->n_vertices = n_indices;
}

CoglIndices *
cogl_primitive_get_indices (CoglPrimitive *primitive)
{
  return primitive->indices;
}

CoglPrimitive *
cogl_primitive_copy (CoglPrimitive *primitive)
{
  CoglPrimitive *copy;

  copy = cogl_primitive_new_with_attributes (primitive->mode,
                                             primitive->n_vertices,
                                             (CoglAttribute **)primitive->attributes->pdata,
                                             primitive->n_attributes);

  cogl_primitive_set_indices (copy, primitive->indices, primitive->n_vertices);
  cogl_primitive_set_first_vertex (copy, primitive->first_vertex);

  return copy;
}

CoglPrimitive *
_cogl_primitive_immutable_ref (CoglPrimitive *primitive)
{
  int i;

  g_return_val_if_fail (COGL_IS_PRIMITIVE (primitive), NULL);

  primitive->immutable_ref++;

  for (i = 0; i < primitive->n_attributes; i++)
    _cogl_attribute_immutable_ref (primitive->attributes->pdata[i]);

  return primitive;
}

void
_cogl_primitive_immutable_unref (CoglPrimitive *primitive)
{
  int i;

  g_return_if_fail (COGL_IS_PRIMITIVE (primitive));
  g_return_if_fail (primitive->immutable_ref > 0);

  primitive->immutable_ref--;

  for (i = 0; i < primitive->n_attributes; i++)
    _cogl_attribute_immutable_unref (primitive->attributes->pdata[i]);
}

void
cogl_primitive_foreach_attribute (CoglPrimitive *primitive,
                                  CoglPrimitiveAttributeCallback callback,
                                  void *user_data)
{
  int i;
  for (i = 0; i < primitive->n_attributes; i++)
    if (!callback (primitive, primitive->attributes->pdata[i], user_data))
      break;
}

void
_cogl_primitive_draw (CoglPrimitive *primitive,
                      CoglFramebuffer *framebuffer,
                      CoglPipeline *pipeline,
                      CoglDrawFlags flags)
{
  if (primitive->indices)
    _cogl_framebuffer_draw_indexed_attributes (framebuffer,
                                               pipeline,
                                               primitive->mode,
                                               primitive->first_vertex,
                                               primitive->n_vertices,
                                               primitive->indices,
                                               (CoglAttribute **) primitive->attributes->pdata,
                                               primitive->n_attributes,
                                               flags);
  else
    _cogl_framebuffer_draw_attributes (framebuffer,
                                       pipeline,
                                       primitive->mode,
                                       primitive->first_vertex,
                                       primitive->n_vertices,
                                       (CoglAttribute **) primitive->attributes->pdata,
                                       primitive->n_attributes,
                                       flags);
}

void
cogl_primitive_draw (CoglPrimitive *primitive,
                     CoglFramebuffer *framebuffer,
                     CoglPipeline *pipeline)
{
  _cogl_primitive_draw (primitive, framebuffer, pipeline, 0 /* flags */);
}
