/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2007,2008,2009 Intel Corporation.
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

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

#ifndef __COGL_MATERIAL_H__
#define __COGL_MATERIAL_H__

#include <cogl/cogl-types.h>
#include <cogl/cogl-depth-state.h>
#include <cogl/cogl-macros.h>
#include <cogl/cogl-object.h>

G_BEGIN_DECLS

/**
 * SECTION:cogl-material
 * @short_description: Functions for creating and manipulating materials
 *
 * COGL allows creating and manipulating materials used to fill in
 * geometry. Materials may simply be lighting attributes (such as an
 * ambient and diffuse colour) or might represent one or more textures
 * blended together.
 */

typedef struct _CoglMaterial	      CoglMaterial;
typedef struct _CoglMaterialLayer     CoglMaterialLayer;

#define COGL_TYPE_MATERIAL (cogl_material_get_type ())
COGL_EXPORT
GType cogl_material_get_type (void);

#define COGL_MATERIAL(OBJECT) ((CoglMaterial *)OBJECT)

/**
 * CoglMaterialWrapMode:
 * @COGL_MATERIAL_WRAP_MODE_REPEAT: The texture will be repeated. This
 *   is useful for example to draw a tiled background.
 * @COGL_MATERIAL_WRAP_MODE_CLAMP_TO_EDGE: The coordinates outside the
 *   range 0→1 will sample copies of the edge pixels of the
 *   texture. This is useful to avoid artifacts if only one copy of
 *   the texture is being rendered.
 * @COGL_MATERIAL_WRAP_MODE_AUTOMATIC: Cogl will try to automatically
 *   decide which of the above two to use. For cogl_rectangle(), it
 *   will use repeat mode if any of the texture coordinates are
 *   outside the range 0→1, otherwise it will use clamp to edge. For
 *   cogl_polygon() it will always use repeat mode. For
 *   cogl_vertex_buffer_draw() it will use repeat mode except for
 *   layers that have point sprite coordinate generation enabled. This
 *   is the default value.
 *
 * The wrap mode specifies what happens when texture coordinates
 * outside the range 0→1 are used. Note that if the filter mode is
 * anything but %COGL_MATERIAL_FILTER_NEAREST then texels outside the
 * range 0→1 might be used even when the coordinate is exactly 0 or 1
 * because OpenGL will try to sample neighbouring pixels. For example
 * if you are trying to render the full texture then you may get
 * artifacts around the edges when the pixels from the other side are
 * merged in if the wrap mode is set to repeat.
 *
 * Since: 1.4
 */
/* GL_ALWAYS is just used here as a value that is known not to clash
 * with any valid GL wrap modes
 *
 * XXX: keep the values in sync with the CoglMaterialWrapModeInternal
 * enum so no conversion is actually needed.
 */
typedef enum
{
  COGL_MATERIAL_WRAP_MODE_REPEAT = 0x2901,
  COGL_MATERIAL_WRAP_MODE_CLAMP_TO_EDGE = 0x812F,
  COGL_MATERIAL_WRAP_MODE_AUTOMATIC = 0x0207
} CoglMaterialWrapMode;
/* NB: these values come from the equivalents in gl.h */

/**
 * cogl_material_new:
 *
 * Allocates and initializes a blank white material
 *
 * Return value: a pointer to a new #CoglMaterial
 * Deprecated: 1.16: Use cogl_pipeline_new() instead
 */
COGL_DEPRECATED_FOR (cogl_pipeline_new)
COGL_EXPORT CoglMaterial *
cogl_material_new (void);

/**
 * cogl_material_set_color:
 * @material: A #CoglMaterial object
 * @color: The components of the color
 *
 * Sets the basic color of the material, used when no lighting is enabled.
 *
 * Note that if you don't add any layers to the material then the color
 * will be blended unmodified with the destination; the default blend
 * expects premultiplied colors: for example, use (0.5, 0.0, 0.0, 0.5) for
 * semi-transparent red. See cogl_color_premultiply().
 *
 * The default value is (1.0, 1.0, 1.0, 1.0)
 *
 * Since: 1.0
 * Deprecated: 1.16: Use cogl_pipeline_set_color() instead
 */
COGL_DEPRECATED_FOR (cogl_pipeline_set_color)
COGL_EXPORT void
cogl_material_set_color (CoglMaterial    *material,
                         const CoglColor *color);

/**
 * cogl_material_set_color4ub:
 * @material: A #CoglMaterial object
 * @red: The red component
 * @green: The green component
 * @blue: The blue component
 * @alpha: The alpha component
 *
 * Sets the basic color of the material, used when no lighting is enabled.
 *
 * The default value is (0xff, 0xff, 0xff, 0xff)
 *
 * Since: 1.0
 * Deprecated: 1.16: Use cogl_pipeline_set_color4ub() instead
 */
COGL_DEPRECATED_FOR (cogl_pipeline_set_color4ub)
COGL_EXPORT void
cogl_material_set_color4ub (CoglMaterial *material,
			    uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t alpha);

G_END_DECLS

#endif /* __COGL_MATERIAL_H__ */
