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

#pragma once

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#include "cogl/cogl-attribute-buffer.h"
#include "cogl/cogl-indices.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define COGL_TYPE_ATTRIBUTE (cogl_attribute_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglAttribute,
                      cogl_attribute,
                      COGL,
                      ATTRIBUTE,
                      GObject)

/**
 * cogl_attribute_new: (constructor)
 * @attribute_buffer: The #CoglAttributeBuffer containing the actual
 *                    attribute data
 * @name: The name of the attribute (used to reference it from GLSL)
 * @stride: The number of bytes to jump to get to the next attribute
 *          value for the next vertex. (Usually
 *          `sizeof (MyVertex)`)
 * @offset: The byte offset from the start of @attribute_buffer for
 *          the first attribute value. (Usually
 *          `offsetof (MyVertex, component0)`
 * @components: The number of components (e.g. 4 for an rgba color or
 *              3 for and (x,y,z) position)
 * @type: FIXME
 *
 * Describes the layout for a list of vertex attribute values (For
 * example, a list of texture coordinates or colors).
 *
 * The @name is used to access the attribute inside a GLSL vertex
 * shader and there are some special names you should use if they are
 * applicable:
 *
 * - "cogl_position_in" (used for vertex positions)
 * - "cogl_color_in" (used for vertex colors)
 * - "cogl_tex_coord0_in", "cogl_tex_coord1", ...
 * (used for vertex texture coordinates)
 * - "cogl_normal_in" (used for vertex normals)
 * - "cogl_point_size_in" (used to set the size of points
 *    per-vertex. Note this can only be used if
 *    %COGL_FEATURE_ID_POINT_SIZE_ATTRIBUTE is advertised and
 *    cogl_pipeline_set_per_vertex_point_size() is called on the pipeline.
 *
 * The attribute values corresponding to different vertices can either
 * be tightly packed or interleaved with other attribute values. For
 * example it's common to define a structure for a single vertex like:
 * ```c
 * typedef struct
 * {
 *   float x, y, z; /<!-- -->* position attribute *<!-- -->/
 *   float s, t; /<!-- -->* texture coordinate attribute *<!-- -->/
 * } MyVertex;
 * ```
 *
 * And then create an array of vertex data something like:
 * ```c
 * MyVertex vertices[100] = { .... }
 * ```
 *
 * In this case, to describe either the position or texture coordinate
 * attribute you have to move `sizeof (MyVertex)` bytes to
 * move from one vertex to the next.  This is called the attribute
 * @stride. If you weren't interleving attributes and you instead had
 * a packed array of float x, y pairs then the attribute stride would
 * be `(2 * sizeof (float))`. So the @stride is the number of
 * bytes to move to find the attribute value of the next vertex.
 *
 * Normally a list of attributes starts at the beginning of an array.
 * So for the `MyVertex` example above the @offset is the
 * offset inside the `MyVertex` structure to the first
 * component of the attribute. For the texture coordinate attribute
 * the offset would be `offsetof (MyVertex, s)` or instead of
 * using the offsetof macro you could use `sizeof (float) *
 * 3`.  If you've divided your @array into blocks of non-interleved
 * attributes then you will need to calculate the @offset as the number of
 * bytes in blocks preceding the attribute you're describing.
 *
 * An attribute often has more than one component. For example a color
 * is often comprised of 4 red, green, blue and alpha @components, and a
 * position may be comprised of 2 x and y @components. You should aim
 * to keep the number of components to a minimum as more components
 * means more data needs to be mapped into the GPU which can be a
 * bottleneck when dealing with a large number of vertices.
 *
 * Finally you need to specify the component data type. Here you
 * should aim to use the smallest type that meets your precision
 * requirements. Again the larger the type then more data needs to be
 * mapped into the GPU which can be a bottleneck when dealing with
 * a large number of vertices.
 *
 * Return value: (transfer full): A newly allocated #CoglAttribute
 *          describing the layout for a list of attribute values
 *          stored in @array.
 */
/* XXX: look for a precedent to see if the stride/offset args should
 * have a different order. */
COGL_EXPORT CoglAttribute *
cogl_attribute_new (CoglAttributeBuffer *attribute_buffer,
                    const char *name,
                    size_t stride,
                    size_t offset,
                    int components,
                    CoglAttributeType type);

/**
 * cogl_attribute_set_normalized:
 * @attribute: A #CoglAttribute
 * @normalized: The new value for the normalized property.
 *
 * Sets whether fixed point attribute types are mapped to the range
 * 0→1. For example when this property is TRUE and a
 * %COGL_ATTRIBUTE_TYPE_UNSIGNED_BYTE type is used then the value 255
 * will be mapped to 1.0.
 *
 * The default value of this property depends on the name of the
 * attribute. For the builtin properties cogl_color_in and
 * cogl_normal_in it will default to TRUE and for all other names it
 * will default to FALSE.
 */
COGL_EXPORT void
cogl_attribute_set_normalized (CoglAttribute *attribute,
                               gboolean normalized);

/**
 * cogl_attribute_get_buffer:
 * @attribute: A #CoglAttribute
 *
 * Return value: (transfer none): the #CoglAttributeBuffer that was
 *        set with cogl_attribute_new().
 */
COGL_EXPORT CoglAttributeBuffer *
cogl_attribute_get_buffer (CoglAttribute *attribute);

G_END_DECLS
