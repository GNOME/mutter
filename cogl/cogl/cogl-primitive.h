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

#include "cogl/cogl-types.h" /* for CoglVerticesMode */
#include "cogl/cogl-attribute.h"
#include "cogl/cogl-framebuffer.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CoglPrimitive:
 *
 *Functions for creating, manipulating and drawing primitives
 */

#define COGL_TYPE_PRIMITIVE (cogl_primitive_get_type ())

COGL_EXPORT
G_DECLARE_FINAL_TYPE (CoglPrimitive,
                      cogl_primitive,
                      COGL,
                      PRIMITIVE,
                      GObject)
/**
 * CoglVertexP2:
 * @x: The x component of a position attribute
 * @y: The y component of a position attribute
 *
 * A convenience vertex definition that can be used with
 * cogl_primitive_new_p2().
 */
typedef struct {
   float x, y;
} CoglVertexP2;

/**
 * CoglVertexP3:
 * @x: The x component of a position attribute
 * @y: The y component of a position attribute
 * @z: The z component of a position attribute
 *
 * A convenience vertex definition that can be used with
 * cogl_primitive_new_p3().
 */
typedef struct {
   float x, y, z;
} CoglVertexP3;

/**
 * CoglVertexP2C4:
 * @x: The x component of a position attribute
 * @y: The y component of a position attribute
 * @r: The red component of a color attribute
 * @b: The green component of a color attribute
 * @g: The blue component of a color attribute
 * @a: The alpha component of a color attribute
 *
 * A convenience vertex definition that can be used with
 * cogl_primitive_new_p2c4().
 */
typedef struct {
   float x, y;
   uint8_t r, g, b, a;
} CoglVertexP2C4;

/**
 * CoglVertexP2T2:
 * @x: The x component of a position attribute
 * @y: The y component of a position attribute
 * @s: The s component of a texture coordinate attribute
 * @t: The t component of a texture coordinate attribute
 *
 * A convenience vertex definition that can be used with
 * cogl_primitive_new_p2t2().
 */
typedef struct {
   float x, y;
   float s, t;
} CoglVertexP2T2;

/**
 * CoglVertexP3T2:
 * @x: The x component of a position attribute
 * @y: The y component of a position attribute
 * @z: The z component of a position attribute
 * @s: The s component of a texture coordinate attribute
 * @t: The t component of a texture coordinate attribute
 *
 * A convenience vertex definition that can be used with
 * cogl_primitive_new_p3t2().
 */
typedef struct {
   float x, y, z;
   float s, t;
} CoglVertexP3T2;

/**
 * cogl_primitive_new:
 * @mode: A #CoglVerticesMode defining how to draw the vertices
 * @n_vertices: The number of vertices to process when drawing
 * @...: A %NULL terminated list of attributes
 *
 * Combines a set of `CoglAttribute`s with a specific draw @mode
 * and defines a vertex count so a #CoglPrimitive object can be retained and
 * drawn later with no addition information required.
 *
 * The value passed as @n_vertices will simply update the
 * #CoglPrimitive `n_vertices` property as if
 * cogl_primitive_set_n_vertices() were called. This property defines
 * the number of vertices to read when drawing.
 *
 * Return value: (transfer full): A newly allocated #CoglPrimitive object
 */
COGL_EXPORT CoglPrimitive *
cogl_primitive_new (CoglVerticesMode mode,
                    int n_vertices,
                    ...);

/**
 * cogl_primitive_new_with_attributes:
 * @mode: A #CoglVerticesMode defining how to draw the vertices
 * @n_vertices: The number of vertices to process when drawing
 * @attributes: (array length=n_attributes): An array of CoglAttribute
 * @n_attributes: The number of attributes
 *
 * Combines a set of `CoglAttribute`s with a specific draw @mode
 * and defines a vertex count so a #CoglPrimitive object can be retained and
 * drawn later with no addition information required.
 *
 * The value passed as @n_vertices will simply update the
 * #CoglPrimitive `n_vertices` property as if
 * cogl_primitive_set_n_vertices() were called. This property defines
 * the number of vertices to read when drawing.
 *
 * Return value: (transfer full): A newly allocated #CoglPrimitive object
 */
COGL_EXPORT CoglPrimitive *
cogl_primitive_new_with_attributes (CoglVerticesMode mode,
                                    int n_vertices,
                                    CoglAttribute **attributes,
                                    int n_attributes);

/**
 * cogl_primitive_new_p2:
 * @context: A #CoglContext
 * @mode: A #CoglVerticesMode defining how to draw the vertices
 * @n_vertices: The number of vertices to read from @data and also
 *              the number of vertices to read when later drawing.
 * @data: (array length=n_vertices) (element-type Cogl.VertexP2): An array
 *        of #CoglVertexP2 vertices
 *
 * Provides a convenient way to describe a primitive, such as a single
 * triangle strip or a triangle fan, that will internally allocate the
 * necessary #CoglAttributeBuffer storage, describe the position
 * attribute with a #CoglAttribute and upload your data.
 *
 * For example to draw a convex polygon you can do:
 * ```c
 * CoglVertexP2 triangle[] =
 * {
 *   { 0,   300 },
 *   { 150, 0,  },
 *   { 300, 300 }
 * };
 * prim = cogl_primitive_new_p2 (COGL_VERTICES_MODE_TRIANGLE_FAN,
 *                               3, triangle);
 * cogl_primitive_draw (prim);
 * ```
 *
 * The value passed as @n_vertices is initially used to determine how
 * much can be read from @data but it will also be used to update the
 * #CoglPrimitive `n_vertices` property as if
 * cogl_primitive_set_n_vertices() were called. This property defines
 * the number of vertices to read when drawing.

 * The primitive API doesn't support drawing with sliced
 * textures (since switching between slices implies changing state and
 * so that implies multiple primitives need to be submitted). If your
 * hardware doesn't support non-power of two textures (For example you
 * are using GLES 1.1) then you will need to make sure your assets are
 * resized to a power-of-two size (though they don't have to be square)
 *
 *
 * Return value: (transfer full): A newly allocated #CoglPrimitive
 * with a reference of 1. This can be freed using g_object_unref().
 */
COGL_EXPORT CoglPrimitive *
cogl_primitive_new_p2 (CoglContext *context,
                       CoglVerticesMode mode,
                       int n_vertices,
                       const CoglVertexP2 *data);

/**
 * cogl_primitive_new_p3:
 * @context: A #CoglContext
 * @mode: A #CoglVerticesMode defining how to draw the vertices
 * @n_vertices: The number of vertices to read from @data and also
 *              the number of vertices to read when later drawing.
 * @data: (array length=n_vertices) (element-type Cogl.VertexP3): An array of
 *        #CoglVertexP3 vertices
 *
 * Provides a convenient way to describe a primitive, such as a single
 * triangle strip or a triangle fan, that will internally allocate the
 * necessary #CoglAttributeBuffer storage, describe the position
 * attribute with a #CoglAttribute and upload your data.
 *
 * For example to draw a convex polygon you can do:
 * ```c
 * CoglVertexP3 triangle[] =
 * {
 *   { 0,   300, 0 },
 *   { 150, 0,   0 },
 *   { 300, 300, 0 }
 * };
 * prim = cogl_primitive_new_p3 (COGL_VERTICES_MODE_TRIANGLE_FAN,
 *                               3, triangle);
 * cogl_primitive_draw (prim);
 * ```
 *
 * The value passed as @n_vertices is initially used to determine how
 * much can be read from @data but it will also be used to update the
 * #CoglPrimitive `n_vertices` property as if
 * cogl_primitive_set_n_vertices() were called. This property defines
 * the number of vertices to read when drawing.

 * The primitive API doesn't support drawing with sliced
 * textures (since switching between slices implies changing state and
 * so that implies multiple primitives need to be submitted). If your
 * hardware doesn't support non-power of two textures (For example you
 * are using GLES 1.1) then you will need to make sure your assets are
 * resized to a power-of-two size (though they don't have to be square)
 *
 *
 * Return value: (transfer full): A newly allocated #CoglPrimitive
 * with a reference of 1. This can be freed using g_object_unref().
 */
COGL_EXPORT CoglPrimitive *
cogl_primitive_new_p3 (CoglContext *context,
                       CoglVerticesMode mode,
                       int n_vertices,
                       const CoglVertexP3 *data);

/**
 * cogl_primitive_new_p2c4:
 * @context: A #CoglContext
 * @mode: A #CoglVerticesMode defining how to draw the vertices
 * @n_vertices: The number of vertices to read from @data and also
 *              the number of vertices to read when later drawing.
 * @data: (array length=n_vertices) (element-type Cogl.VertexP2C4): An array
 *        of #CoglVertexP2C4 vertices
 *
 * Provides a convenient way to describe a primitive, such as a single
 * triangle strip or a triangle fan, that will internally allocate the
 * necessary #CoglAttributeBuffer storage, describe the position
 * and color attributes with `CoglAttribute`s and upload
 * your data.
 *
 * For example to draw a convex polygon with a linear gradient you
 * can do:
 * ```c
 * CoglVertexP2C4 triangle[] =
 * {
 *   { 0,   300,  0xff, 0x00, 0x00, 0xff },
 *   { 150, 0,    0x00, 0xff, 0x00, 0xff },
 *   { 300, 300,  0xff, 0x00, 0x00, 0xff }
 * };
 * prim = cogl_primitive_new_p2c4 (COGL_VERTICES_MODE_TRIANGLE_FAN,
 *                                 3, triangle);
 * cogl_primitive_draw (prim);
 * ```
 *
 * The value passed as @n_vertices is initially used to determine how
 * much can be read from @data but it will also be used to update the
 * #CoglPrimitive `n_vertices` property as if
 * cogl_primitive_set_n_vertices() were called. This property defines
 * the number of vertices to read when drawing.

 * The primitive API doesn't support drawing with sliced
 * textures (since switching between slices implies changing state and
 * so that implies multiple primitives need to be submitted). If your
 * hardware doesn't support non-power of two textures (For example you
 * are using GLES 1.1) then you will need to make sure your assets are
 * resized to a power-of-two size (though they don't have to be square)
 *
 *
 * Return value: (transfer full): A newly allocated #CoglPrimitive
 * with a reference of 1. This can be freed using g_object_unref().
 */
COGL_EXPORT CoglPrimitive *
cogl_primitive_new_p2c4 (CoglContext *context,
                         CoglVerticesMode mode,
                         int n_vertices,
                         const CoglVertexP2C4 *data);

/**
 * cogl_primitive_new_p2t2:
 * @context: A #CoglContext
 * @mode: A #CoglVerticesMode defining how to draw the vertices
 * @n_vertices: The number of vertices to read from @data and also
 *              the number of vertices to read when later drawing.
 * @data: (array length=n_vertices) (element-type Cogl.VertexP2T2): An array
 *        of #CoglVertexP2T2 vertices
 *
 * Provides a convenient way to describe a primitive, such as a single
 * triangle strip or a triangle fan, that will internally allocate the
 * necessary #CoglAttributeBuffer storage, describe the position and
 * texture coordinate attributes with `CoglAttribute`s and
 * upload your data.
 *
 * For example to draw a convex polygon with texture mapping you can
 * do:
 * ```c
 * CoglVertexP2T2 triangle[] =
 * {
 *   { 0,   300,  0.0, 1.0},
 *   { 150, 0,    0.5, 0.0},
 *   { 300, 300,  1.0, 1.0}
 * };
 * prim = cogl_primitive_new_p2t2 (COGL_VERTICES_MODE_TRIANGLE_FAN,
 *                                 3, triangle);
 * cogl_primitive_draw (prim);
 * ```
 *
 * The value passed as @n_vertices is initially used to determine how
 * much can be read from @data but it will also be used to update the
 * #CoglPrimitive `n_vertices` property as if
 * cogl_primitive_set_n_vertices() were called. This property defines
 * the number of vertices to read when drawing.

 * The primitive API doesn't support drawing with sliced
 * textures (since switching between slices implies changing state and
 * so that implies multiple primitives need to be submitted). If your
 * hardware doesn't support non-power of two textures (For example you
 * are using GLES 1.1) then you will need to make sure your assets are
 * resized to a power-of-two size (though they don't have to be square)
 *
 *
 * Return value: (transfer full): A newly allocated #CoglPrimitive
 * with a reference of 1. This can be freed using g_object_unref().
 */
COGL_EXPORT CoglPrimitive *
cogl_primitive_new_p2t2 (CoglContext *context,
                         CoglVerticesMode mode,
                         int n_vertices,
                         const CoglVertexP2T2 *data);

/**
 * cogl_primitive_new_p3t2:
 * @context: A #CoglContext
 * @mode: A #CoglVerticesMode defining how to draw the vertices
 * @n_vertices: The number of vertices to read from @data and also
 *              the number of vertices to read when later drawing.
 * @data: (array length=n_vertices) (element-type Cogl.VertexP3T2): An array
 *        of #CoglVertexP3T2 vertices
 *
 * Provides a convenient way to describe a primitive, such as a single
 * triangle strip or a triangle fan, that will internally allocate the
 * necessary #CoglAttributeBuffer storage, describe the position and
 * texture coordinate attributes with `CoglAttribute`s and
 * upload your data.
 *
 * For example to draw a convex polygon with texture mapping you can
 * do:
 * ```c
 * CoglVertexP3T2 triangle[] =
 * {
 *   { 0,   300, 0,  0.0, 1.0},
 *   { 150, 0,   0,  0.5, 0.0},
 *   { 300, 300, 0,  1.0, 1.0}
 * };
 * prim = cogl_primitive_new_p3t2 (COGL_VERTICES_MODE_TRIANGLE_FAN,
 *                                 3, triangle);
 * cogl_primitive_draw (prim);
 * ```
 *
 * The value passed as @n_vertices is initially used to determine how
 * much can be read from @data but it will also be used to update the
 * #CoglPrimitive `n_vertices` property as if
 * cogl_primitive_set_n_vertices() were called. This property defines
 * the number of vertices to read when drawing.

 * The primitive API doesn't support drawing with sliced
 * textures (since switching between slices implies changing state and
 * so that implies multiple primitives need to be submitted). If your
 * hardware doesn't support non-power of two textures (For example you
 * are using GLES 1.1) then you will need to make sure your assets are
 * resized to a power-of-two size (though they don't have to be square)
 *
 *
 * Return value: (transfer full): A newly allocated #CoglPrimitive
 * with a reference of 1. This can be freed using g_object_unref().
 */
COGL_EXPORT CoglPrimitive *
cogl_primitive_new_p3t2 (CoglContext *context,
                         CoglVerticesMode mode,
                         int n_vertices,
                         const CoglVertexP3T2 *data);

COGL_EXPORT void
cogl_primitive_set_first_vertex (CoglPrimitive *primitive,
                                 int first_vertex);

/**
 * cogl_primitive_set_n_vertices:
 * @primitive: A #CoglPrimitive object
 * @n_vertices: The number of vertices to read when drawing.
 *
 * Specifies how many vertices should be read when drawing the given
 * @primitive.
 *
 * Usually this value is set implicitly when associating vertex data
 * or indices with a #CoglPrimitive.
 *
 * To be clear; it doesn't refer to the number of vertices - in
 * terms of data - associated with the primitive it's just the number
 * of vertices to read and draw.
 */
COGL_EXPORT void
cogl_primitive_set_n_vertices (CoglPrimitive *primitive,
                               int n_vertices);


/**
 * cogl_primitive_set_indices:
 * @primitive: A #CoglPrimitive
 * @indices: (array length=n_indices): A #CoglIndices array
 * @n_indices: The number of indices to reference when drawing
 *
 * Associates a sequence of #CoglIndices with the given @primitive.
 *
 * #CoglIndices provide a way to virtualize your real vertex data by
 * providing a sequence of indices that index into your real vertex
 * data. The GPU will walk though the index values to indirectly
 * lookup the data for each vertex instead of sequentially walking
 * through the data directly. This lets you save memory by indexing
 * shared data multiple times instead of duplicating the data.
 *
 * The value passed as @n_indices will simply update the
 * #CoglPrimitive `n_vertices` property as if
 * cogl_primitive_set_n_vertices() were called. This property defines
 * the number of vertices to draw or, put another way, how many
 * indices should be read from @indices when drawing.
 *
 * The #CoglPrimitive `first_vertex` property
 * also affects drawing with indices by defining the first entry of the
 * indices to start drawing from.
 */
COGL_EXPORT void
cogl_primitive_set_indices (CoglPrimitive *primitive,
                            CoglIndices *indices,
                            int n_indices);

/**
 * cogl_primitive_draw:
 * @primitive: A #CoglPrimitive geometry object
 * @framebuffer: A destination #CoglFramebuffer
 * @pipeline: A #CoglPipeline state object
 *
 * Draws the given @primitive geometry to the specified destination
 * @framebuffer using the graphics processing state described by @pipeline.
 *
 * This drawing api doesn't support high-level meta texture types such
 * as #CoglTexture2DSliced so it is the user's responsibility to
 * ensure that only low-level textures that can be directly sampled by
 * a GPU such as #CoglTexture2D are associated with layers of the given
 * @pipeline.
 */
COGL_EXPORT void
cogl_primitive_draw (CoglPrimitive *primitive,
                     CoglFramebuffer *framebuffer,
                     CoglPipeline *pipeline);


G_END_DECLS
