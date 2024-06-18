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

#include "config.h"

#include <string.h>

#include "cogl/cogl-util.h"
#include "cogl/cogl-color.h"

static void
cogl_value_transform_color_string (const GValue *src,
                                   GValue       *dest);
static void
cogl_value_transform_string_color (const GValue *src,
                                   GValue       *dest);

G_DEFINE_BOXED_TYPE_WITH_CODE (CoglColor,
                               cogl_color,
                               cogl_color_copy,
                               cogl_color_free,
                               {
                                  g_value_register_transform_func (g_define_type_id, G_TYPE_STRING, cogl_value_transform_color_string);
                                  g_value_register_transform_func (G_TYPE_STRING, g_define_type_id, cogl_value_transform_string_color);
                               });

CoglColor *
cogl_color_copy (const CoglColor *color)
{
  if (G_LIKELY (color))
    return g_memdup2 (color, sizeof (CoglColor));

  return NULL;
}

void
cogl_color_free (CoglColor *color)
{
  if (G_LIKELY (color))
    g_free (color);
}

void
cogl_color_init_from_4f (CoglColor *color,
                         float red,
                         float green,
                         float blue,
                         float alpha)
{
  g_return_if_fail (color != NULL);

  color->red   = (int) (red * 255);
  color->green = (int) (green * 255);
  color->blue  = (int) (blue * 255);
  color->alpha = (int) (alpha * 255);
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

      *color = (uint8_t) (CLAMP (number / 100.0, 0.0, 1.0) * 255);
    }
  else
    *color = (uint8_t) CLAMP (number, 0, 255);
}

static gboolean
parse_rgba (CoglColor *color,
            gchar     *str,
            gboolean   has_alpha)
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

      color->alpha = (uint8_t) CLAMP (number * 255.0, 0, 255);
    }
  else
    color->alpha = 255;

  skip_whitespace (&str);
  if (*str != ')')
    return FALSE;

  return TRUE;
}

static gboolean
parse_hsla (CoglColor *color,
            gchar     *str,
            gboolean   has_alpha)
{
  gdouble number;
  gdouble h, l, s, alpha;

  skip_whitespace (&str);

  if (*str != '(')
    return FALSE;

  str += 1;

  /* hue */
  skip_whitespace (&str);
  /* we don't do any angle normalization here because
   * cogl_color_from_hls() will do it for us
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

      alpha = CLAMP (number * 255.0, 0, 255);
    }
  else
    alpha = 255;

  skip_whitespace (&str);
  if (*str != ')')
    return FALSE;

  cogl_color_init_from_hsl (color, (float) h, (float) s, (float) l);
  color->alpha = (uint8_t) alpha;

  return TRUE;
}

gboolean
cogl_color_from_string (CoglColor   *color,
                        const gchar *str)
{
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
              color->red = (result >> 24) & 0xff;
              color->green = (result >> 16) & 0xff;
              color->blue = (result >> 8) & 0xff;

              color->alpha = result & 0xff;

              return TRUE;

            case 6: /* #rrggbb */
              color->red = (result >> 16) & 0xff;
              color->green = (result >> 8) & 0xff;
              color->blue = result & 0xff;

              color->alpha = 0xff;

              return TRUE;

            case 4: /* #rgba */
              color->red = ((result >> 12) & 0xf);
              color->green = ((result >> 8) & 0xf);
              color->blue = ((result >> 4) & 0xf);
              color->alpha = result & 0xf;

              color->red = (color->red << 4) | color->red;
              color->green = (color->green << 4) | color->green;
              color->blue = (color->blue << 4) | color->blue;
              color->alpha = (color->alpha << 4) | color->alpha;

              return TRUE;

            case 3: /* #rgb */
              color->red = ((result >> 8) & 0xf);
              color->green = ((result >> 4) & 0xf);
              color->blue = result & 0xf;

              color->red = (color->red << 4) | color->red;
              color->green = (color->green << 4) | color->green;
              color->blue = (color->blue << 4) | color->blue;

              color->alpha = 0xff;

              return TRUE;

            default:
              return FALSE;
            }
        }
    }

  return FALSE;
}

gchar *
cogl_color_to_string (const CoglColor *color)
{
  g_return_val_if_fail (color != NULL, NULL);

  return g_strdup_printf ("#%02x%02x%02x%02x",
                          color->red,
                          color->green,
                          color->blue,
                          color->alpha);
}

float
cogl_color_get_red (const CoglColor *color)
{
  return  ((float) color->red / 255.0f);
}

float
cogl_color_get_green (const CoglColor *color)
{
  return  ((float) color->green / 255.0f);
}

float
cogl_color_get_blue (const CoglColor *color)
{
  return  ((float) color->blue / 255.0f);
}

float
cogl_color_get_alpha (const CoglColor *color)
{
  return  ((float) color->alpha / 255.0f);
}

void
cogl_color_premultiply (CoglColor *color)
{
  color->red = (color->red * color->alpha + 128) / 255;
  color->green = (color->green * color->alpha + 128) / 255;
  color->blue = (color->blue * color->alpha + 128) / 255;
}

gboolean
cogl_color_equal (const void *v1, const void *v2)
{
  const uint32_t *c1 = v1, *c2 = v2;

  g_return_val_if_fail (v1 != NULL, FALSE);
  g_return_val_if_fail (v2 != NULL, FALSE);

  /* XXX: We don't compare the padding */
  return *c1 == *c2 ? TRUE : FALSE;
}

guint
cogl_color_hash (gconstpointer v)
{
  const CoglColor *color;
  g_return_val_if_fail (v != NULL, 0);

  color = v;
  return (color->alpha |
          color->blue << 8 |
          color->green << 16 |
          color->red << 24);
}

void
cogl_color_to_hsl (const CoglColor *color,
                   float           *hue,
                   float           *saturation,
                   float           *luminance)
{
  float red, green, blue;
  float min, max, delta;
  float h, l, s;

  red   = color->red / 255.0f;
  green = color->green / 255.0f;
  blue  = color->blue / 255.0f;

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
	s = (max - min) / (2.0f - max - min);

      delta = max - min;

      if (red == max)
	h = (green - blue) / delta;
      else if (green == max)
	h = 2.0f + (blue - red) / delta;
      else if (blue == max)
	h = 4.0f + (red - green) / delta;

      h *= 60;

      if (h < 0)
	h += 360.0f;
    }

  if (hue)
    *hue = h;

  if (luminance)
    *luminance = l;

  if (saturation)
    *saturation = s;
}

void
cogl_color_init_from_hsl (CoglColor *color,
                          float      hue,
                          float      saturation,
                          float      luminance)
{
  float tmp1, tmp2;
  float tmp3[3];
  float clr[3];
  int   i;

  hue /= 360.0f;

  if (saturation == 0)
    {
      cogl_color_init_from_4f (color, luminance, luminance, luminance, 1.0f);
      return;
    }

  if (luminance <= 0.5f)
    tmp2 = luminance * (1.0f + saturation);
  else
    tmp2 = luminance + saturation - (luminance * saturation);

  tmp1 = 2.0f * luminance - tmp2;

  tmp3[0] = hue + 1.0f / 3.0f;
  tmp3[1] = hue;
  tmp3[2] = hue - 1.0f / 3.0f;

  for (i = 0; i < 3; i++)
    {
      if (tmp3[i] < 0)
        tmp3[i] += 1.0f;

      if (tmp3[i] > 1)
        tmp3[i] -= 1.0f;

      if (6.0f * tmp3[i] < 1.0f)
        clr[i] = tmp1 + (tmp2 - tmp1) * tmp3[i] * 6.0f;
      else if (2.0f * tmp3[i] < 1.0f)
        clr[i] = tmp2;
      else if (3.0f * tmp3[i] < 2.0f)
        clr[i] = (tmp1 + (tmp2 - tmp1) * ((2.0f / 3.0f) - tmp3[i]) * 6.0f);
      else
        clr[i] = tmp1;
    }

  cogl_color_init_from_4f (color, clr[0], clr[1], clr[2], 1.0f);
}

/**
 * cogl_value_set_color:
 * @value: a #GValue initialized to #COGL_TYPE_COLOR
 * @color: the color to set
 *
 * Sets @value to @color.
 */
void
cogl_value_set_color (GValue          *value,
                      const CoglColor *color)
{
  g_return_if_fail (COGL_VALUE_HOLDS_COLOR (value));

  g_value_set_boxed (value, color);
}

/**
 * cogl_value_get_color:
 * @value: a #GValue initialized to #COGL_TYPE_COLOR
 *
 * Gets the #CoglColor contained in @value.
 *
 * Return value: (transfer none): the color inside the passed #GValue
 */
const CoglColor *
cogl_value_get_color (const GValue *value)
{
  g_return_val_if_fail (COGL_VALUE_HOLDS_COLOR (value), NULL);

  return g_value_get_boxed (value);
}

static void
param_color_init (GParamSpec *pspec)
{
  CoglParamSpecColor *cspec = COGL_PARAM_SPEC_COLOR (pspec);

  cspec->default_value = NULL;
}

static void
param_color_finalize (GParamSpec *pspec)
{
  CoglParamSpecColor *cspec = COGL_PARAM_SPEC_COLOR (pspec);

  cogl_color_free (cspec->default_value);
}

static void
param_color_set_default (GParamSpec *pspec,
                         GValue     *value)
{
  const CoglColor *default_value =
    COGL_PARAM_SPEC_COLOR (pspec)->default_value;
  cogl_value_set_color (value, default_value);
}

static gint
param_color_values_cmp (GParamSpec   *pspec,
                        const GValue *value1,
                        const GValue *value2)
{
  const CoglColor *color1 = g_value_get_boxed (value1);
  const CoglColor *color2 = g_value_get_boxed (value2);
  int pixel1, pixel2;

  if (color1 == NULL)
    return color2 == NULL ? 0 : -1;

  pixel1 = cogl_color_hash (color1);
  pixel2 = cogl_color_hash (color2);

  if (pixel1 < pixel2)
    return -1;
  else if (pixel1 == pixel2)
    return 0;
  else
    return 1;
}

GType
cogl_param_color_get_type (void)
{
  static GType pspec_type = 0;

  if (G_UNLIKELY (pspec_type == 0))
    {
      const GParamSpecTypeInfo pspec_info = {
        sizeof (CoglParamSpecColor),
        16,
        param_color_init,
        COGL_TYPE_COLOR,
        param_color_finalize,
        param_color_set_default,
        NULL,
        param_color_values_cmp,
      };

      pspec_type = g_param_type_register_static (g_intern_static_string ("CoglParamSpecColor"),
                                                 &pspec_info);
    }

  return pspec_type;
}

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
GParamSpec *
cogl_param_spec_color (const gchar     *name,
                       const gchar     *nick,
                       const gchar     *blurb,
                       const CoglColor *default_value,
                       GParamFlags      flags)
{
  CoglParamSpecColor *cspec;

  cspec = g_param_spec_internal (COGL_TYPE_PARAM_COLOR,
                                 name, nick, blurb, flags);

  cspec->default_value = cogl_color_copy (default_value);

  return G_PARAM_SPEC (cspec);
}

static void
cogl_value_transform_color_string (const GValue *src,
                                   GValue       *dest)
{
  const CoglColor *color = g_value_get_boxed (src);

  if (color)
    {
      gchar *string = cogl_color_to_string (color);

      g_value_take_string (dest, string);
    }
  else
    g_value_set_string (dest, NULL);
}

static void
cogl_value_transform_string_color (const GValue *src,
                                   GValue       *dest)
{
  const char *str = g_value_get_string (src);

  if (str)
    {
      CoglColor color = { 0, };

      cogl_color_from_string (&color, str);

      cogl_value_set_color (dest, &color);
    }
  else
    cogl_value_set_color (dest, NULL);
}
