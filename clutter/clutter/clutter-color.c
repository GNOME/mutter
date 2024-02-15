/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2006 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <math.h>

#include <pango/pango-attributes.h>

#include "clutter/clutter-interval.h"
#include "clutter/clutter-main.h"
#include "clutter/clutter-color.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-debug.h"

/**
 * clutter_color_to_hls:
 * @color: a #ClutterColor
 * @hue: (out): return location for the hue value or %NULL
 * @luminance: (out): return location for the luminance value or %NULL
 * @saturation: (out): return location for the saturation value or %NULL
 *
 * Converts @color to the HLS format.
 *
 * The @hue value is in the 0 .. 360 range. The @luminance and
 * @saturation values are in the 0 .. 1 range.
 */
void
clutter_color_to_hls (const ClutterColor *color,
		      float              *hue,
		      float              *luminance,
		      float              *saturation)
{
  float red, green, blue;
  float min, max, delta;
  float h, l, s;
  
  g_return_if_fail (color != NULL);

  red   = color->red / 255.0;
  green = color->green / 255.0;
  blue  = color->blue / 255.0;

  if (red > green)
    {
      if (red > blue)
	max = red;
      else
	max = blue;

      if (green < blue)
	min = green;
      else
	min = blue;
    }
  else
    {
      if (green > blue)
	max = green;
      else
	max = blue;

      if (red < blue)
	min = red;
      else
	min = blue;
    }

  l = (max + min) / 2;
  s = 0;
  h = 0;

  if (max != min)
    {
      if (l <= 0.5)
	s = (max - min) / (max + min);
      else
	s = (max - min) / (2.0 - max - min);

      delta = max - min;

      if (red == max)
	h = (green - blue) / delta;
      else if (green == max)
	h = 2.0 + (blue - red) / delta;
      else if (blue == max)
	h = 4.0 + (red - green) / delta;

      h *= 60;

      if (h < 0)
	h += 360.0;
    }

  if (hue)
    *hue = h;

  if (luminance)
    *luminance = l;

  if (saturation)
    *saturation = s;
}

/**
 * clutter_color_from_hls:
 * @color: (out): return location for a #ClutterColor
 * @hue: hue value, in the 0 .. 360 range
 * @luminance: luminance value, in the 0 .. 1 range
 * @saturation: saturation value, in the 0 .. 1 range
 *
 * Converts a color expressed in HLS (hue, luminance and saturation)
 * values into a #ClutterColor.
 */
void
clutter_color_from_hls (ClutterColor *color,
			float         hue,
			float         luminance,
			float         saturation)
{
  float tmp1, tmp2;
  float tmp3[3];
  float clr[3];
  int   i;

  hue /= 360.0;

  if (saturation == 0)
    {
      color->red = color->green = color->blue = (luminance * 255);

      return;
    }

  if (luminance <= 0.5)
    tmp2 = luminance * (1.0 + saturation);
  else
    tmp2 = luminance + saturation - (luminance * saturation);

  tmp1 = 2.0 * luminance - tmp2;

  tmp3[0] = hue + 1.0 / 3.0;
  tmp3[1] = hue;
  tmp3[2] = hue - 1.0 / 3.0;

  for (i = 0; i < 3; i++)
    {
      if (tmp3[i] < 0)
        tmp3[i] += 1.0;

      if (tmp3[i] > 1)
        tmp3[i] -= 1.0;

      if (6.0 * tmp3[i] < 1.0)
        clr[i] = tmp1 + (tmp2 - tmp1) * tmp3[i] * 6.0;
      else if (2.0 * tmp3[i] < 1.0)
        clr[i] = tmp2;
      else if (3.0 * tmp3[i] < 2.0)
        clr[i] = (tmp1 + (tmp2 - tmp1) * ((2.0 / 3.0) - tmp3[i]) * 6.0);
      else
        clr[i] = tmp1;
    }

  color->red   = floorf (clr[0] * 255.0 + 0.5);
  color->green = floorf (clr[1] * 255.0 + 0.5);
  color->blue  = floorf (clr[2] * 255.0 + 0.5);
}

/**
 * clutter_color_to_pixel:
 * @color: a #ClutterColor
 *
 * Converts @color into a packed 32 bit integer, containing
 * all the four 8 bit channels used by #ClutterColor.
 *
 * Return value: a packed color
 */
guint32
clutter_color_to_pixel (const ClutterColor *color)
{
  g_return_val_if_fail (color != NULL, 0);
  
  return (color->alpha       |
          color->blue  << 8  |
          color->green << 16 |
          color->red   << 24);
}

/**
 * clutter_color_from_pixel:
 * @color: (out caller-allocates): return location for a #ClutterColor
 * @pixel: a 32 bit packed integer containing a color
 *
 * Converts @pixel from the packed representation of a four 8 bit channel
 * color to a #ClutterColor.
 */
void
clutter_color_from_pixel (ClutterColor *color,
			  guint32       pixel)
{
  g_return_if_fail (color != NULL);

  color->red   =  pixel >> 24;
  color->green = (pixel >> 16) & 0xff;
  color->blue  = (pixel >> 8)  & 0xff;
  color->alpha =  pixel        & 0xff;
}

static inline void
skip_whitespace (gchar **str)
{
  while (g_ascii_isspace (**str))
    *str += 1;
}

static inline void
parse_rgb_value (gchar   *str,
                 guint8  *color,
                 gchar  **endp)
{
  gdouble number;
  gchar *p;

  skip_whitespace (&str);

  number = g_ascii_strtod (str, endp);

  p = *endp;

  skip_whitespace (&p);

  if (*p == '%')
    {
      *endp = (gchar *) (p + 1);

      *color = CLAMP (number / 100.0, 0.0, 1.0) * 255;
    }
  else
    *color = CLAMP (number, 0, 255);
}

static gboolean
parse_rgba (ClutterColor *color,
            gchar        *str,
            gboolean      has_alpha)
{
  skip_whitespace (&str);

  if (*str != '(')
    return FALSE;

  str += 1;

  /* red */
  parse_rgb_value (str, &color->red, &str);
  skip_whitespace (&str);
  if (*str != ',')
    return FALSE;

  str += 1;

  /* green */
  parse_rgb_value (str, &color->green, &str);
  skip_whitespace (&str);
  if (*str != ',')
    return FALSE;

  str += 1;

  /* blue */
  parse_rgb_value (str, &color->blue, &str);
  skip_whitespace (&str);

  /* alpha (optional); since the alpha channel value can only
   * be between 0 and 1 we don't use the parse_rgb_value()
   * function
   */
  if (has_alpha)
    {
      gdouble number;

      if (*str != ',')
        return FALSE;

      str += 1;

      skip_whitespace (&str);
      number = g_ascii_strtod (str, &str);

      color->alpha = CLAMP (number * 255.0, 0, 255);
    }
  else
    color->alpha = 255;

  skip_whitespace (&str);
  if (*str != ')')
    return FALSE;

  return TRUE;
}

static gboolean
parse_hsla (ClutterColor *color,
            gchar        *str,
            gboolean      has_alpha)
{
  gdouble number;
  gdouble h, l, s;

  skip_whitespace (&str);

  if (*str != '(')
    return FALSE;

  str += 1;

  /* hue */
  skip_whitespace (&str);
  /* we don't do any angle normalization here because
   * clutter_color_from_hls() will do it for us
   */
  number = g_ascii_strtod (str, &str);
  skip_whitespace (&str);
  if (*str != ',')
    return FALSE;

  h = number;

  str += 1;

  /* saturation */
  skip_whitespace (&str);
  number = g_ascii_strtod (str, &str);
  skip_whitespace (&str);
  if (*str != '%')
    return FALSE;

  str += 1;

  s = CLAMP (number / 100.0, 0.0, 1.0);
  skip_whitespace (&str);
  if (*str != ',')
    return FALSE;

  str += 1;

  /* luminance */
  skip_whitespace (&str);
  number = g_ascii_strtod (str, &str);
  skip_whitespace (&str);
  if (*str != '%')
    return FALSE;

  str += 1;

  l = CLAMP (number / 100.0, 0.0, 1.0);
  skip_whitespace (&str);

  /* alpha (optional); since the alpha channel value can only
   * be between 0 and 1 we don't use the parse_rgb_value()
   * function
   */
  if (has_alpha)
    {
      if (*str != ',')
        return FALSE;

      str += 1;

      skip_whitespace (&str);
      number = g_ascii_strtod (str, &str);

      color->alpha = CLAMP (number * 255.0, 0, 255);
    }
  else
    color->alpha = 255;

  skip_whitespace (&str);
  if (*str != ')')
    return FALSE;

  clutter_color_from_hls (color, h, l, s);

  return TRUE;
}

/**
 * clutter_color_from_string:
 * @color: (out caller-allocates): return location for a #ClutterColor
 * @str: a string specifying a color
 *
 * Parses a string definition of a color, filling the #ClutterColor.red,
 * #ClutterColor.green, #ClutterColor.blue and #ClutterColor.alpha fields 
 * of @color.
 *
 * The @color is not allocated.
 *
 * The format of @str can be either one of:
 *
 *   - a standard name (as taken from the X11 rgb.txt file)
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
gboolean
clutter_color_from_string (ClutterColor *color,
                           const gchar  *str)
{
  PangoColor pango_color = { 0, };

  g_return_val_if_fail (color != NULL, FALSE);
  g_return_val_if_fail (str != NULL, FALSE);

  if (strncmp (str, "rgb", 3) == 0)
    {
      gchar *s = (gchar *) str;
      gboolean res;

      if (strncmp (str, "rgba", 4) == 0)
        res = parse_rgba (color, s + 4, TRUE);
      else
        res = parse_rgba (color, s + 3, FALSE);

      return res;
    }

  if (strncmp (str, "hsl", 3) == 0)
    {
      gchar *s = (gchar *) str;
      gboolean res;

      if (strncmp (str, "hsla", 4) == 0)
        res = parse_hsla (color, s + 4, TRUE);
      else
        res = parse_hsla (color, s + 3, FALSE);

      return res;
    }

  /* if the string contains a color encoded using the hexadecimal
   * notations (#rrggbbaa or #rgba) we attempt a rough pass at
   * parsing the color ourselves, as we need the alpha channel that
   * Pango can't retrieve.
   */
  if (str[0] == '#' && str[1] != '\0')
    {
      gsize length = strlen (str + 1);
      gint32 result;

      if (sscanf (str + 1, "%x", &result) == 1)
        {
          switch (length)
            {
            case 8: /* rrggbbaa */
              color->red   = (result >> 24) & 0xff;
              color->green = (result >> 16) & 0xff;
              color->blue  = (result >>  8) & 0xff;

              color->alpha = result & 0xff;

              return TRUE;

            case 6: /* #rrggbb */
              color->red   = (result >> 16) & 0xff;
              color->green = (result >>  8) & 0xff;
              color->blue  = result & 0xff;

              color->alpha = 0xff;

              return TRUE;

            case 4: /* #rgba */
              color->red   = ((result >> 12) & 0xf);
              color->green = ((result >>  8) & 0xf);
              color->blue  = ((result >>  4) & 0xf);
              color->alpha = result & 0xf;

              color->red   = (color->red   << 4) | color->red;
              color->green = (color->green << 4) | color->green;
              color->blue  = (color->blue  << 4) | color->blue;
              color->alpha = (color->alpha << 4) | color->alpha;

              return TRUE;

            case 3: /* #rgb */
              color->red   = ((result >>  8) & 0xf);
              color->green = ((result >>  4) & 0xf);
              color->blue  = result & 0xf;

              color->red   = (color->red   << 4) | color->red;
              color->green = (color->green << 4) | color->green;
              color->blue  = (color->blue  << 4) | color->blue;

              color->alpha = 0xff;

              return TRUE;

            default:
              return FALSE;
            }
        }
    }

  /* fall back to pango for X11-style named colors; see:
   *
   *   http://en.wikipedia.org/wiki/X11_color_names
   *
   * for a list. at some point we might even ship with our own list generated
   * from X11/rgb.txt, like we generate the key symbols.
   */
  if (pango_color_parse (&pango_color, str))
    {
      color->red   = pango_color.red;
      color->green = pango_color.green;
      color->blue  = pango_color.blue;

      color->alpha = 0xff;

      return TRUE;
    }

  return FALSE;
}

/**
 * clutter_color_to_string:
 * @color: a #ClutterColor
 *
 * Returns a textual specification of @color in the hexadecimal form
 * `&num;rrggbbaa`, where `r`, `g`, `b` and `a` are
 * hexadecimal digits representing the red, green, blue and alpha components
 * respectively.
 *
 * Return value: (transfer full): a newly-allocated text string
 */
gchar *
clutter_color_to_string (const ClutterColor *color)
{
  g_return_val_if_fail (color != NULL, NULL);

  return g_strdup_printf ("#%02x%02x%02x%02x",
                          color->red,
                          color->green,
                          color->blue,
                          color->alpha);
}

/**
 * clutter_color_equal:
 * @v1: (type Clutter.Color): a #ClutterColor
 * @v2: (type Clutter.Color): a #ClutterColor
 *
 * Compares two `ClutterColor`s and checks if they are the same.
 *
 * This function can be passed to g_hash_table_new() as the @key_equal_func
 * parameter, when using `ClutterColor`s as keys in a #GHashTable.
 *
 * Return value: %TRUE if the two colors are the same.
 */
gboolean
clutter_color_equal (gconstpointer v1,
                     gconstpointer v2)
{
  const ClutterColor *a, *b;

  g_return_val_if_fail (v1 != NULL, FALSE);
  g_return_val_if_fail (v2 != NULL, FALSE);

  if (v1 == v2)
    return TRUE;

  a = v1;
  b = v2;

  return (a->red   == b->red   &&
          a->green == b->green &&
          a->blue  == b->blue  &&
          a->alpha == b->alpha);
}

/**
 * clutter_color_hash:
 * @v: (type Clutter.Color): a #ClutterColor
 *
 * Converts a #ClutterColor to a hash value.
 *
 * This function can be passed to g_hash_table_new() as the @hash_func
 * parameter, when using `ClutterColor`s as keys in a #GHashTable.
 *
 * Return value: a hash value corresponding to the color
 */
guint
clutter_color_hash (gconstpointer v)
{
  return clutter_color_to_pixel ((const ClutterColor *) v);
}

/**
 * clutter_color_interpolate:
 * @initial: the initial #ClutterColor
 * @final: the final #ClutterColor
 * @progress: the interpolation progress
 * @result: (out): return location for the interpolation
 *
 * Interpolates between @initial and @final `ClutterColor`s
 * using @progress
 */
void
clutter_color_interpolate (const ClutterColor *initial,
                           const ClutterColor *final,
                           gdouble             progress,
                           ClutterColor       *result)
{
  g_return_if_fail (initial != NULL);
  g_return_if_fail (final != NULL);
  g_return_if_fail (result != NULL);

  result->red   = initial->red   + (final->red   - initial->red)   * progress;
  result->green = initial->green + (final->green - initial->green) * progress;
  result->blue  = initial->blue  + (final->blue  - initial->blue)  * progress;
  result->alpha = initial->alpha + (final->alpha - initial->alpha) * progress;
}

static gboolean
clutter_color_progress (const GValue *a,
                        const GValue *b,
                        gdouble       progress,
                        GValue       *retval)
{
  const ClutterColor *a_color = clutter_value_get_color (a);
  const ClutterColor *b_color = clutter_value_get_color (b);
  ClutterColor res = { 0, };

  clutter_color_interpolate (a_color, b_color, progress, &res);
  clutter_value_set_color (retval, &res);

  return TRUE;
}

/**
 * clutter_color_copy:
 * @color: a #ClutterColor
 *
 * Makes a copy of the color structure.  The result must be
 * freed using [method@Clutter.Color.free].
 *
 * Return value: (transfer full): an allocated copy of @color.
 */
ClutterColor *
clutter_color_copy (const ClutterColor *color)
{
  if (G_LIKELY (color != NULL))
    return g_memdup2 (color, sizeof (ClutterColor));

  return NULL;
}

/**
 * clutter_color_free:
 * @color: a #ClutterColor
 *
 * Frees a color structure created with [method@Clutter.Color.copy].
 */
void
clutter_color_free (ClutterColor *color)
{
  if (G_LIKELY (color != NULL))
    g_free (color);
}

/**
 * clutter_color_new:
 * @red: red component of the color, between 0 and 255
 * @green: green component of the color, between 0 and 255
 * @blue: blue component of the color, between 0 and 255
 * @alpha: alpha component of the color, between 0 and 255
 *
 * Creates a new #ClutterColor with the given values.
 *
 * This function is the equivalent of:
 *
 * ```c
 *   clutter_color_init (clutter_color_alloc (), red, green, blue, alpha);
 * ```
 *
 * Return value: (transfer full): the newly allocated color.
 *   Use [method@Clutter.Color.free] when done
 */
ClutterColor *
clutter_color_new (guint8 red,
                   guint8 green,
                   guint8 blue,
                   guint8 alpha)
{
  return clutter_color_init (clutter_color_alloc (),
                             red,
                             green,
                             blue,
                             alpha);
}

/**
 * clutter_color_alloc: (constructor)
 *
 * Allocates a new, transparent black #ClutterColor.
 *
 * Return value: (transfer full): the newly allocated #ClutterColor; use
 *   [method@Clutter.Color.free] to free its resources
 */
ClutterColor *
clutter_color_alloc (void)
{
  return g_new0 (ClutterColor, 1);
}

/**
 * clutter_color_init:
 * @color: a #ClutterColor
 * @red: red component of the color, between 0 and 255
 * @green: green component of the color, between 0 and 255
 * @blue: blue component of the color, between 0 and 255
 * @alpha: alpha component of the color, between 0 and 255
 *
 * Initializes @color with the given values.
 *
 * Return value: (transfer none): the initialized #ClutterColor
 */
ClutterColor *
clutter_color_init (ClutterColor *color,
                    guint8        red,
                    guint8        green,
                    guint8        blue,
                    guint8        alpha)
{
  g_return_val_if_fail (color != NULL, NULL);

  color->red = red;
  color->green = green;
  color->blue = blue;
  color->alpha = alpha;

  return color;
}

static void
clutter_value_transform_color_string (const GValue *src,
                                      GValue       *dest)
{
  const ClutterColor *color = g_value_get_boxed (src);

  if (color)
    {
      gchar *string = clutter_color_to_string (color);

      g_value_take_string (dest, string);
    }
  else
    g_value_set_string (dest, NULL);
}

static void
clutter_value_transform_string_color (const GValue *src,
                                      GValue       *dest)
{
  const char *str = g_value_get_string (src);

  if (str)
    {
      ClutterColor color = { 0, };

      clutter_color_from_string (&color, str);

      clutter_value_set_color (dest, &color);
    }
  else
    clutter_value_set_color (dest, NULL);
}

G_DEFINE_BOXED_TYPE_WITH_CODE (ClutterColor, clutter_color,
                               clutter_color_copy,
                               clutter_color_free,
                               CLUTTER_REGISTER_VALUE_TRANSFORM_TO (G_TYPE_STRING, clutter_value_transform_color_string)
                               CLUTTER_REGISTER_VALUE_TRANSFORM_FROM (G_TYPE_STRING, clutter_value_transform_string_color)
                               CLUTTER_REGISTER_INTERVAL_PROGRESS (clutter_color_progress));

/**
 * clutter_value_set_color:
 * @value: a #GValue initialized to #CLUTTER_TYPE_COLOR
 * @color: the color to set
 *
 * Sets @value to @color.
 */
void
clutter_value_set_color (GValue             *value,
                         const ClutterColor *color)
{
  g_return_if_fail (CLUTTER_VALUE_HOLDS_COLOR (value));

  g_value_set_boxed (value, color);
}

/**
 * clutter_value_get_color:
 * @value: a #GValue initialized to #CLUTTER_TYPE_COLOR
 *
 * Gets the #ClutterColor contained in @value.
 *
 * Return value: (transfer none): the color inside the passed #GValue
 */
const ClutterColor *
clutter_value_get_color (const GValue *value)
{
  g_return_val_if_fail (CLUTTER_VALUE_HOLDS_COLOR (value), NULL);

  return g_value_get_boxed (value);
}

static void
param_color_init (GParamSpec *pspec)
{
  ClutterParamSpecColor *cspec = CLUTTER_PARAM_SPEC_COLOR (pspec);

  cspec->default_value = NULL;
}

static void
param_color_finalize (GParamSpec *pspec)
{
  ClutterParamSpecColor *cspec = CLUTTER_PARAM_SPEC_COLOR (pspec);

  clutter_color_free (cspec->default_value);
}

static void
param_color_set_default (GParamSpec *pspec,
                         GValue     *value)
{
  const ClutterColor *default_value =
    CLUTTER_PARAM_SPEC_COLOR (pspec)->default_value;
  clutter_value_set_color (value, default_value);
}

static gint
param_color_values_cmp (GParamSpec   *pspec,
                        const GValue *value1,
                        const GValue *value2)
{
  const ClutterColor *color1 = g_value_get_boxed (value1);
  const ClutterColor *color2 = g_value_get_boxed (value2);
  int pixel1, pixel2;

  if (color1 == NULL)
    return color2 == NULL ? 0 : -1;

  pixel1 = clutter_color_to_pixel (color1);
  pixel2 = clutter_color_to_pixel (color2);

  if (pixel1 < pixel2)
    return -1;
  else if (pixel1 == pixel2)
    return 0;
  else
    return 1;
}

GType
clutter_param_color_get_type (void)
{
  static GType pspec_type = 0;

  if (G_UNLIKELY (pspec_type == 0))
    {
      const GParamSpecTypeInfo pspec_info = {
        sizeof (ClutterParamSpecColor),
        16,
        param_color_init,
        CLUTTER_TYPE_COLOR,
        param_color_finalize,
        param_color_set_default,
        NULL,
        param_color_values_cmp,
      };

      pspec_type = g_param_type_register_static (I_("ClutterParamSpecColor"),
                                                 &pspec_info);
    }

  return pspec_type;
}

/**
 * clutter_param_spec_color: (skip)
 * @name: name of the property
 * @nick: short name
 * @blurb: description (can be translatable)
 * @default_value: default value
 * @flags: flags for the param spec
 *
 * Creates a #GParamSpec for properties using #ClutterColor.
 *
 * Return value: the newly created #GParamSpec
 */
GParamSpec *
clutter_param_spec_color (const gchar        *name,
                          const gchar        *nick,
                          const gchar        *blurb,
                          const ClutterColor *default_value,
                          GParamFlags         flags)
{
  ClutterParamSpecColor *cspec;

  cspec = g_param_spec_internal (CLUTTER_TYPE_PARAM_COLOR,
                                 name, nick, blurb, flags);

  cspec->default_value = clutter_color_copy (default_value);

  return G_PARAM_SPEC (cspec);
}
