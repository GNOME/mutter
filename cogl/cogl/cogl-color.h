/*
 * Cogl
 *
 * A Low Level GPU Graphics and Utilities API
 *
 * Copyright (C) 2008,2009 Intel Corporation.
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

#if !defined(__COGL_H_INSIDE__) && !defined(COGL_COMPILATION)
#error "Only <cogl/cogl.h> can be included directly."
#endif

/**
 * CoglColor:
 *
 * A generic color definition
 *
 * #CoglColor is a simple structure holding the definition of a color such
 * that it can be efficiently used by GL
 */

#include "cogl/cogl-types.h"
#include "cogl/cogl-macros.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define COGL_TYPE_COLOR (cogl_color_get_type ())

/**
 * cogl_color_get_type:
 *
 * Returns: a #GType that can be used with the GLib type system.
 */
COGL_EXPORT
GType cogl_color_get_type (void);

/**
 * cogl_color_copy:
 * @color: the color to copy
 *
 * Creates a copy of @color
 *
 * Return value: a newly-allocated #CoglColor. Use cogl_color_free()
 *   to free the allocate resources
 */
COGL_EXPORT CoglColor *
cogl_color_copy (const CoglColor *color);

/**
 * cogl_color_free:
 * @color: the color to free
 *
 * Frees the resources allocated by cogl_color_new() and cogl_color_copy()
 */
COGL_EXPORT void
cogl_color_free (CoglColor *color);

/**
 * cogl_color_init_from_4f:
 * @color: A pointer to a #CoglColor to initialize
 * @red: value of the red channel, between 0 and 1.0
 * @green: value of the green channel, between 0 and 1.0
 * @blue: value of the blue channel, between 0 and 1.0
 * @alpha: value of the alpha channel, between 0 and 1.0
 *
 * Sets the values of the passed channels into a #CoglColor
 */
COGL_EXPORT void
cogl_color_init_from_4f (CoglColor *color,
                         float red,
                         float green,
                         float blue,
                         float alpha);

/**
 * cogl_color_get_red:
 * @color: a #CoglColor
 *
 * Retrieves the red channel of @color as a fixed point
 * value between 0 and 1.0.
 *
 * Return value: the red channel of the passed color
 */
COGL_EXPORT float
cogl_color_get_red (const CoglColor *color);

/**
 * cogl_color_get_green:
 * @color: a #CoglColor
 *
 * Retrieves the green channel of @color as a fixed point
 * value between 0 and 1.0.
 *
 * Return value: the green channel of the passed color
 */
COGL_EXPORT float
cogl_color_get_green (const CoglColor *color);

/**
 * cogl_color_get_blue:
 * @color: a #CoglColor
 *
 * Retrieves the blue channel of @color as a fixed point
 * value between 0 and 1.0.
 *
 * Return value: the blue channel of the passed color
 */
COGL_EXPORT float
cogl_color_get_blue (const CoglColor *color);

/**
 * cogl_color_get_alpha:
 * @color: a #CoglColor
 *
 * Retrieves the alpha channel of @color as a fixed point
 * value between 0 and 1.0.
 *
 * Return value: the alpha channel of the passed color
 */
COGL_EXPORT float
cogl_color_get_alpha (const CoglColor *color);

/**
 * cogl_color_premultiply:
 * @color: the color to premultiply
 *
 * Converts a non-premultiplied color to a pre-multiplied color. For
 * example, semi-transparent red is (1.0, 0, 0, 0.5) when non-premultiplied
 * and (0.5, 0, 0, 0.5) when premultiplied.
 */
COGL_EXPORT void
cogl_color_premultiply (CoglColor *color);

/**
 * cogl_color_equal:
 * @v1: a #CoglColor
 * @v2: a #CoglColor
 *
 * Compares two `CoglColor`s and checks if they are the same.
 *
 * This function can be passed to g_hash_table_new() as the @key_equal_func
 * parameter, when using `CoglColor`s as keys in a #GHashTable.
 *
 * Return value: %TRUE if the two colors are the same.
 */
COGL_EXPORT gboolean
cogl_color_equal (const void *v1, const void *v2);

/**
 * cogl_color_to_hsl:
 * @color: a #CoglColor
 * @hue: (out): return location for the hue value or %NULL
 * @saturation: (out): return location for the saturation value or %NULL
 * @luminance: (out): return location for the luminance value or %NULL
 *
 * Converts @color to the HLS format.
 *
 * The @hue value is in the 0 .. 360 range. The @luminance and
 * @saturation values are in the 0 .. 1 range.
 */
COGL_EXPORT void
cogl_color_to_hsl (const CoglColor *color,
                   float           *hue,
                   float           *saturation,
                   float           *luminance);

/**
 * cogl_color_init_from_hsl:
 * @color: (out): return location for a #CoglColor
 * @hue: hue value, in the 0 .. 360 range
 * @saturation: saturation value, in the 0 .. 1 range
 * @luminance: luminance value, in the 0 .. 1 range
 *
 * Converts a color expressed in HLS (hue, luminance and saturation)
 * values into a #CoglColor.
 */
COGL_EXPORT void
cogl_color_init_from_hsl (CoglColor *color,
                          float      hue,
                          float      saturation,
                          float      luminance);

G_END_DECLS
