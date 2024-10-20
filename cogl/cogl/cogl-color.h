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

#include "cogl/cogl-types.h"
#include "cogl/cogl-macros.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CoglColor:
 *
 * A generic color definition
 *
 * #CoglColor is a simple structure holding the definition of a color such
 * that it can be efficiently used by GL
 */
struct _CoglColor
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  uint8_t alpha;
};
/**
 * COGL_COLOR_INIT:
 * @r: value for the red channel, between 0 and 255
 * @g: value for the green channel, between 0 and 255
 * @b: value for the blue channel, between 0 and 255
 * @a: value for the alpha channel, between 0 and 255
 *
 * A macro that initializes a #CoglColor, to be used when declaring it.
 */
#define COGL_COLOR_INIT(_r, _g, _b, _a) \
        (CoglColor) { \
          .red = (_r), \
          .green = (_g), \
          .blue = (_b), \
          .alpha = (_a) \
        }

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
 * cogl_color_free: (skip):
 * @color: the color to free
 *
 * Frees the resources allocated by cogl_color_new() and cogl_color_copy()
 */
COGL_EXPORT void
cogl_color_free (CoglColor *color);


/**
 * cogl_color_to_string:
 * @color: a #CoglColor
 *
 * Returns a textual specification of @color in the hexadecimal form
 * `&num;rrggbbaa`, where `r`, `g`, `b` and `a` are
 * hexadecimal digits representing the red, green, blue and alpha components
 * respectively.
 *
 * Return value: (transfer full): a newly-allocated text string
 */
COGL_EXPORT
gchar * cogl_color_to_string (const CoglColor *color);

/**
 * cogl_color_from_string:
 * @color: (out caller-allocates): return location for a #CoglColor
 * @str: a string specifying a color
 *
 * Parses a string definition of a color, filling the #CoglColor.red,
 * #CoglColor.green, #CoglColor.blue and #CoglColor.alpha fields
 * of @color.
 *
 * The @color is not allocated.
 *
 * The format of @str can be either one of:
 *
 *   - an hexadecimal value in the form: `#rgb`, `#rrggbb`, `#rgba`, or `#rrggbbaa`
 *   - a RGB color in the form: `rgb(r, g, b)`
 *   - a RGB color in the form: `rgba(r, g, b, a)`
 *   - a HSL color in the form: `hsl(h, s, l)`
 *    -a HSL color in the form: `hsla(h, s, l, a)`
 *
 * where 'r', 'g', 'b' and 'a' are (respectively) the red, green, blue color
 * intensities and the opacity. The 'h', 's' and 'l' are (respectively) the
 * hue, saturation and luminance values.
 *
 * In the rgb() and rgba() formats, the 'r', 'g', and 'b' values are either
 * integers between 0 and 255, or percentage values in the range between 0%
 * and 100%; the percentages require the '%' character. The 'a' value, if
 * specified, can only be a floating point value between 0.0 and 1.0.
 *
 * In the hls() and hlsa() formats, the 'h' value (hue) is an angle between
 * 0 and 360.0 degrees; the 'l' and 's' values (luminance and saturation) are
 * percentage values in the range between 0% and 100%. The 'a' value, if specified,
 * can only be a floating point value between 0.0 and 1.0.
 *
 * Whitespace inside the definitions is ignored; no leading whitespace
 * is allowed.
 *
 * If the alpha component is not specified then it is assumed to be set to
 * be fully opaque.
 *
 * Return value: %TRUE if parsing succeeded, and %FALSE otherwise
 */
COGL_EXPORT
gboolean cogl_color_from_string (CoglColor   *color,
                                 const gchar *str);

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
 * @v1: (type Cogl.Color): a #CoglColor
 * @v2: (type Cogl.Color): a #CoglColor
 *
 * Compares two `CoglColor`s and checks if they are the same.
 *
 * This function can be passed to g_hash_table_new() as the @key_equal_func
 * parameter, when using `CoglColor`s as keys in a #GHashTable.
 *
 * Return value: %TRUE if the two colors are the same.
 */
COGL_EXPORT gboolean
cogl_color_equal (const void *v1,
                  const void *v2);

/**
 * cogl_color_hash:
 * @v: (type Cogl.Color): a #CoglColor
 *
 * Converts a #CoglColor to a hash value.
 *
 * This function can be passed to g_hash_table_new() as the @hash_func
 * parameter, when using `CoglColor`s as keys in a #GHashTable.
 *
 * Return value: a hash value corresponding to the color
 */
COGL_EXPORT
guint cogl_color_hash (gconstpointer v);

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


#define COGL_TYPE_PARAM_COLOR           (cogl_param_color_get_type ())
#define COGL_PARAM_SPEC_COLOR(pspec)    (G_TYPE_CHECK_INSTANCE_CAST ((pspec), COGL_TYPE_PARAM_COLOR, CoglParamSpecColor))
#define COGL_IS_PARAM_SPEC_COLOR(pspec) (G_TYPE_CHECK_INSTANCE_TYPE ((pspec), COGL_TYPE_PARAM_COLOR))

/**
 * COGL_VALUE_HOLDS_COLOR:
 * @x: a #GValue
 *
 * Evaluates to %TRUE if @x holds a `CoglColor`.
 */
#define COGL_VALUE_HOLDS_COLOR(x)       (G_VALUE_HOLDS ((x), COGL_TYPE_COLOR))

/**
 * CoglParamSpecColor: (skip)
 * @default_value: default color value
 *
 * A #GParamSpec subclass for defining properties holding
 * a #CoglColor.
 */
struct _CoglParamSpecColor
{
  /*< private >*/
  GParamSpec parent_instance;

  /*< public >*/
  CoglColor *default_value;
};

COGL_EXPORT
void cogl_value_set_color (GValue          *value,
                           const CoglColor *color);

COGL_EXPORT
const CoglColor * cogl_value_get_color (const GValue *value);

COGL_EXPORT
GType cogl_param_color_get_type (void) G_GNUC_CONST;


/**
 * cogl_param_spec_color: (skip)
 * @name: name of the property
 * @nick: short name
 * @blurb: description (can be translatable)
 * @default_value: default value
 * @flags: flags for the param spec
 *
 * Creates a #GParamSpec for properties using #CoglColor.
 *
 * Returns: (transfer full): the newly created #GParamSpec
 */
COGL_EXPORT
GParamSpec * cogl_param_spec_color (const gchar     *name,
                                    const gchar     *nick,
                                    const gchar     *blurb,
                                    const CoglColor *default_value,
                                    GParamFlags      flags);

G_END_DECLS
