/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2022  Intel Corporation.
 * Copyright (C) 2023-2024 Red Hat
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
 *
 * Author:
 *   Naveen Kumar <naveen1.kumar@intel.com>
 *   Jonas Ådahl <jadahl@redhat.com>
 */

#include "config.h"

#include "clutter/clutter-color-state-params.h"

#include "clutter/clutter-color-state-private.h"
#include "clutter/clutter-main.h"

#define UNIFORM_NAME_GAMMA_EXP "gamma_exp"
#define UNIFORM_NAME_INV_GAMMA_EXP "inv_gamma_exp"
#define UNIFORM_NAME_COLOR_SPACE_MAPPING "color_transformation_matrix"
#define UNIFORM_NAME_LUMINANCE_MAPPING "luminance_factor"

typedef struct _ClutterColorStateParams
{
  ClutterColorState parent;

  ClutterColorimetry colorimetry;
  ClutterEOTF eotf;
  ClutterLuminance luminance;
} ClutterColorStateParams;

G_DEFINE_TYPE (ClutterColorStateParams,
               clutter_color_state_params,
               CLUTTER_TYPE_COLOR_STATE)

static const char *
clutter_colorspace_to_string (ClutterColorspace colorspace)
{
  switch (colorspace)
    {
    case CLUTTER_COLORSPACE_SRGB:
      return "sRGB";
    case CLUTTER_COLORSPACE_BT2020:
      return "BT.2020";
    case CLUTTER_COLORSPACE_NTSC:
      return "NTSC";
    }

  g_assert_not_reached ();
}

static char *
clutter_colorimetry_to_string (ClutterColorimetry colorimetry)
{
  const ClutterPrimaries *primaries;

  switch (colorimetry.type)
    {
    case CLUTTER_COLORIMETRY_TYPE_COLORSPACE:
      return g_strdup (clutter_colorspace_to_string (colorimetry.colorspace));
    case CLUTTER_COLORIMETRY_TYPE_PRIMARIES:
      primaries = colorimetry.primaries;
      return g_strdup_printf ("[R: %f, %f G: %f, %f B: %f, %f W: %f, %f]",
                              primaries->r_x, primaries->r_y,
                              primaries->g_x, primaries->g_y,
                              primaries->b_x, primaries->b_y,
                              primaries->w_x, primaries->w_y);
    }
}

static const char *
clutter_eotf_to_string (ClutterEOTF eotf)
{
  switch (eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
          return "sRGB";
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          return "PQ";
        case CLUTTER_TRANSFER_FUNCTION_BT709:
          return "BT.709";
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return "linear";
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return "gamma";
    }

  g_assert_not_reached ();
}

const ClutterColorimetry *
clutter_color_state_params_get_colorimetry (ClutterColorStateParams *color_state_params)
{
  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE_PARAMS (color_state_params),
                        NULL);

  return &color_state_params->colorimetry;
}

const ClutterEOTF *
clutter_color_state_params_get_eotf (ClutterColorStateParams *color_state_params)
{
  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE_PARAMS (color_state_params),
                        NULL);

  return &color_state_params->eotf;
}

const ClutterLuminance *
clutter_color_state_params_get_luminance (ClutterColorStateParams *color_state_params)
{
  g_return_val_if_fail (CLUTTER_IS_COLOR_STATE_PARAMS (color_state_params),
                        NULL);

  switch (color_state_params->luminance.type)
    {
    case CLUTTER_LUMINANCE_TYPE_DERIVED:
      return clutter_eotf_get_default_luminance (color_state_params->eotf);
    case CLUTTER_LUMINANCE_TYPE_EXPLICIT:
      return &color_state_params->luminance;
    }
}

static const ClutterLuminance sdr_default_luminance = {
  .type = CLUTTER_LUMINANCE_TYPE_DERIVED,
  .min = 0.2f,
  .max = 80.0f,
  .ref = 80.0f,
};

static const ClutterLuminance bt709_default_luminance = {
  .type = CLUTTER_LUMINANCE_TYPE_DERIVED,
  .min = 0.01f,
  .max = 100.0f,
  .ref = 100.0f,
};

static const ClutterLuminance pq_default_luminance = {
  .type = CLUTTER_LUMINANCE_TYPE_DERIVED,
  .min = 0.005f,
  .max = 10000.0f,
  .ref = 203.0f,
};

const ClutterLuminance *
clutter_eotf_get_default_luminance (ClutterEOTF eotf)
{
  switch (eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return &sdr_default_luminance;
        case CLUTTER_TRANSFER_FUNCTION_BT709:
          return &bt709_default_luminance;
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          return &pq_default_luminance;
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return &sdr_default_luminance;
    }

  g_assert_not_reached ();
}

static float
clutter_eotf_apply_srgb (float input)
{
  if (input <= 0.04045f)
    return input / 12.92f;
  else
    return powf ((input + 0.055f) / 1.055f, 12.0f / 5.0f);
}

static float
clutter_eotf_apply_srgb_inv (float input)
{
  if (input <= 0.0031308f)
    return input * 12.92f;
  else
    return powf (input, (5.0f / 12.0f)) * 1.055f - 0.055f;
}

static float
clutter_eotf_apply_pq (float input)
{
  float c1, c2, c3, oo_m1, oo_m2, num, den;

  c1 = 0.8359375f;
  c2 = 18.8515625f;
  c3 = 18.6875f;
  oo_m1 = 1.0f / 0.1593017f;
  oo_m2 = 1.0f / 78.84375f;
  num = MAX (powf (input, oo_m2) - c1, 0.0f);
  den = c2 - c3 * powf (input, oo_m2);
  return powf (num / den, oo_m1);
}

static float
clutter_eotf_apply_pq_inv (float input)
{
  float c1, c2, c3, m1, m2, in_pow_m1, num, den;

  c1 = 0.8359375f;
  c2 = 18.8515625f;
  c3 = 18.6875f;
  m1 = 0.1593017f;
  m2 = 78.84375f;
  in_pow_m1 = powf (input, m1);
  num = c1 + c2 * in_pow_m1;
  den = 1.0f + c3 * in_pow_m1;
  return powf (num / den, m2);
}

static float
clutter_eotf_apply_bt709 (float input)
{
  if (input < 0.08124f)
    return input / 4.5f;
  else
    return powf ((input + 0.099f) / 1.099f, 1.0f / 0.45f);
}

static float
clutter_eotf_apply_bt709_inv (float input)
{
  if (input < 0.018f)
    return input * 4.5f;
  else
    return 1.099f * powf (input, 0.45f) - 0.099f;
}

static float
clutter_eotf_apply_gamma (float input,
                          float gamma_exp)
{
  /* This avoids returning nan */
  return G_APPROX_VALUE (input, 0.0f, FLT_EPSILON) ?
         0.0f :
         powf (input, gamma_exp);
}

static float
clutter_eotf_apply (ClutterEOTF eotf,
                    float       input)
{
  switch (eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
          return clutter_eotf_apply_srgb (input);
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          return clutter_eotf_apply_pq (input);
        case CLUTTER_TRANSFER_FUNCTION_BT709:
          return clutter_eotf_apply_bt709 (input);
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return input;
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return clutter_eotf_apply_gamma (input, eotf.gamma_exp);
    }

  g_warning ("Didn't apply tranfer function %s",
             clutter_eotf_to_string (eotf));
  return input;
}

static float
clutter_eotf_apply_inv (ClutterEOTF eotf,
                        float       input)
{
  switch (eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
          return clutter_eotf_apply_srgb_inv (input);
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          return clutter_eotf_apply_pq_inv (input);
        case CLUTTER_TRANSFER_FUNCTION_BT709:
          return clutter_eotf_apply_bt709_inv (input);
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return input;
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return clutter_eotf_apply_gamma (input, 1.0f / eotf.gamma_exp);
    }

  g_warning ("Didn't apply inv tranfer function %s",
             clutter_eotf_to_string (eotf));
  return input;
}

/* Primaries and white point retrieved from:
 * https://www.color.org */
static const ClutterPrimaries srgb_primaries = {
  .r_x = 0.64f, .r_y = 0.33f,
  .g_x = 0.30f, .g_y = 0.60f,
  .b_x = 0.15f, .b_y = 0.06f,
  .w_x = 0.3127f, .w_y = 0.3290f,
};

static const ClutterPrimaries ntsc_primaries = {
  .r_x = 0.63f, .r_y = 0.34f,
  .g_x = 0.31f, .g_y = 0.595f,
  .b_x = 0.155f, .b_y = 0.07f,
  .w_x = 0.3127f, .w_y = 0.3290f,
};

static const ClutterPrimaries bt2020_primaries = {
  .r_x = 0.708f, .r_y = 0.292f,
  .g_x = 0.170f, .g_y = 0.797f,
  .b_x = 0.131f, .b_y = 0.046f,
  .w_x = 0.3127f, .w_y = 0.3290f,
};

const ClutterPrimaries *
clutter_colorspace_to_primaries (ClutterColorspace colorspace)
{
  switch (colorspace)
    {
    case CLUTTER_COLORSPACE_SRGB:
      return &srgb_primaries;
    case CLUTTER_COLORSPACE_NTSC:
      return &ntsc_primaries;
    case CLUTTER_COLORSPACE_BT2020:
      return &bt2020_primaries;
    }

  g_warning ("Unhandled colorspace %s",
             clutter_colorspace_to_string (colorspace));

  return &srgb_primaries;
}

void
clutter_primaries_ensure_normalized_range (ClutterPrimaries *primaries)
{
  if (!primaries)
    return;

  primaries->r_x = CLAMP (primaries->r_x, 0.0f, 1.0f);
  primaries->r_y = CLAMP (primaries->r_y, 0.0f, 1.0f);
  primaries->g_x = CLAMP (primaries->g_x, 0.0f, 1.0f);
  primaries->g_y = CLAMP (primaries->g_y, 0.0f, 1.0f);
  primaries->b_x = CLAMP (primaries->b_x, 0.0f, 1.0f);
  primaries->b_y = CLAMP (primaries->b_y, 0.0f, 1.0f);
  primaries->w_x = CLAMP (primaries->w_x, 0.0f, 1.0f);
  primaries->w_y = CLAMP (primaries->w_y, 0.0f, 1.0f);
}

static void
clutter_color_state_params_finalize (GObject *object)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (object);

  if (color_state_params->colorimetry.type == CLUTTER_COLORIMETRY_TYPE_PRIMARIES)
    g_clear_pointer (&color_state_params->colorimetry.primaries, g_free);

  G_OBJECT_CLASS (clutter_color_state_params_parent_class)->finalize (object);
}

static const ClutterPrimaries *
get_primaries (ClutterColorStateParams *color_state_params)
{
  ClutterColorimetry colorimetry = color_state_params->colorimetry;

  switch (colorimetry.type)
    {
    case CLUTTER_COLORIMETRY_TYPE_COLORSPACE:
      return clutter_colorspace_to_primaries (colorimetry.colorspace);
    case CLUTTER_COLORIMETRY_TYPE_PRIMARIES:
      return colorimetry.primaries;
    }

  g_warning ("Unhandled colorimetry when getting primaries");

  return &srgb_primaries;
}

static gboolean
chromaticity_equal (float x1,
                    float y1,
                    float x2,
                    float y2)

{
  /* FIXME: the next color managment version will use more precision */
  return G_APPROX_VALUE (x1, x2, 0.0001f) &&
         G_APPROX_VALUE (y1, y2, 0.0001f);
}

static gboolean
colorimetry_equal (ClutterColorStateParams *color_state_params,
                   ClutterColorStateParams *other_color_state_params)
{
  const ClutterPrimaries *primaries;
  const ClutterPrimaries *other_primaries;

  if (color_state_params->colorimetry.type == CLUTTER_COLORIMETRY_TYPE_COLORSPACE &&
      other_color_state_params->colorimetry.type == CLUTTER_COLORIMETRY_TYPE_COLORSPACE)
    {
      return color_state_params->colorimetry.colorspace ==
             other_color_state_params->colorimetry.colorspace;
    }

  primaries = get_primaries (color_state_params);
  other_primaries = get_primaries (other_color_state_params);

  return chromaticity_equal (primaries->r_x, primaries->r_y,
                             other_primaries->r_x, other_primaries->r_y) &&
         chromaticity_equal (primaries->g_x, primaries->g_y,
                             other_primaries->g_x, other_primaries->g_y) &&
         chromaticity_equal (primaries->b_x, primaries->b_y,
                             other_primaries->b_x, other_primaries->b_y) &&
         chromaticity_equal (primaries->w_x, primaries->w_y,
                             other_primaries->w_x, other_primaries->w_y);
}

static gboolean
eotf_equal (ClutterColorStateParams *color_state_params,
            ClutterColorStateParams *other_color_state_params)
{
  if (color_state_params->eotf.type == CLUTTER_EOTF_TYPE_NAMED &&
      other_color_state_params->eotf.type == CLUTTER_EOTF_TYPE_NAMED)
    {
      return color_state_params->eotf.tf_name ==
             other_color_state_params->eotf.tf_name;
    }

  if (color_state_params->eotf.type == CLUTTER_EOTF_TYPE_GAMMA &&
      other_color_state_params->eotf.type == CLUTTER_EOTF_TYPE_GAMMA)
    {
      return G_APPROX_VALUE (color_state_params->eotf.gamma_exp,
                             other_color_state_params->eotf.gamma_exp,
                             0.0001f);
    }

  return FALSE;
}

static gboolean
luminance_value_approx_equal (float lum,
                              float other_lum,
                              float epsilon)
{
  if (lum == 0.0f || other_lum == 0.0f)
    return lum == other_lum;

  return G_APPROX_VALUE (lum / other_lum, 1.0f, epsilon);
}

static gboolean
luminances_equal (ClutterColorStateParams *color_state_params,
                  ClutterColorStateParams *other_color_state_params)
{
  const ClutterLuminance *lum;
  const ClutterLuminance *other_lum;

  lum = clutter_color_state_params_get_luminance (color_state_params);
  other_lum = clutter_color_state_params_get_luminance (other_color_state_params);

  return luminance_value_approx_equal (lum->min, other_lum->min, 0.1f) &&
         luminance_value_approx_equal (lum->max, other_lum->max, 0.1f) &&
         luminance_value_approx_equal (lum->ref, other_lum->ref, 0.1f);
}

static guint
get_eotf_key (ClutterEOTF eotf)
{
  switch (eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      return eotf.tf_name << 1;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return 1;
    }
}

static void
clutter_color_state_params_init_color_transform_key (ClutterColorState        *color_state,
                                                     ClutterColorState        *target_color_state,
                                                     ClutterColorTransformKey *key)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterColorStateParams *target_color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (target_color_state);

  key->source_eotf_bits = get_eotf_key (color_state_params->eotf);
  key->target_eotf_bits = get_eotf_key (target_color_state_params->eotf);
  key->luminance_bit = luminances_equal (color_state_params,
                                         target_color_state_params) ? 0 : 1;
  key->color_trans_bit = colorimetry_equal (color_state_params,
                                            target_color_state_params) ? 0 : 1;
}

static const char srgb_eotf_source[] =
  "// srgb_eotf:\n"
  "// @color: Normalized ([0,1]) electrical signal value.\n"
  "// Returns: Normalized tristimulus values ([0,1])\n"
  "vec3 srgb_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_low = lessThanEqual (color, vec3 (0.04045));\n"
  "  vec3 lo_part = color / 12.92;\n"
  "  vec3 hi_part = pow ((color + 0.055) / 1.055, vec3 (12.0 / 5.0));\n"
  "  return mix (hi_part, lo_part, is_low);\n"
  "}\n"
  "\n"
  "vec4 srgb_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (srgb_eotf (color.rgb), color.a);\n"
  "}\n";

static const char srgb_inv_eotf_source[] =
  "// srgb_inv_eotf:\n"
  "// @color: Normalized ([0,1]) tristimulus values\n"
  "// Returns: Normalized ([0,1]) electrical signal value\n"
  "vec3 srgb_inv_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_lo = lessThanEqual (color, vec3 (0.0031308));\n"
  "\n"
  "  vec3 lo_part = color * 12.92;\n"
  "  vec3 hi_part = pow (color, vec3 (5.0 / 12.0)) * 1.055 - 0.055;\n"
  "  return mix (hi_part, lo_part, is_lo);\n"
  "}\n"
  "\n"
  "vec4 srgb_inv_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (srgb_inv_eotf (color.rgb), color.a);\n"
  "}\n";

static const char pq_eotf_source[] =
  "// pq_eotf:\n"
  "// @color: Normalized ([0,1]) electrical signal value\n"
  "// Returns: tristimulus values ([0,1])\n"
  "vec3 pq_eotf (vec3 color)\n"
  "{\n"
  "  const float c1 = 0.8359375;\n"
  "  const float c2 = 18.8515625;\n"
  "  const float c3 = 18.6875;\n"
  "\n"
  "  const float oo_m1 = 1.0 / 0.1593017578125;\n"
  "  const float oo_m2 = 1.0 / 78.84375;\n"
  "\n"
  "  vec3 num = max (pow (color, vec3 (oo_m2)) - c1, vec3 (0.0));\n"
  "  vec3 den = c2 - c3 * pow (color, vec3 (oo_m2));\n"
  "\n"
  "  return pow (num / den, vec3 (oo_m1));\n"
  "}\n"
  "\n"
  "vec4 pq_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (pq_eotf (color.rgb), color.a);\n"
  "}\n";

static const char pq_inv_eotf_source[] =
  "// pq_inv_eotf:\n"
  "// @color: Normalized tristimulus values ([0,1])"
  "// Returns: Normalized ([0,1]) electrical signal value\n"
  "vec3 pq_inv_eotf (vec3 color)\n"
  "{\n"
  "  float m1 = 0.1593017578125;\n"
  "  float m2 = 78.84375;\n"
  "  float c1 = 0.8359375;\n"
  "  float c2 = 18.8515625;\n"
  "  float c3 = 18.6875;\n"
  "  vec3 color_pow_m1 = pow (color, vec3 (m1));\n"
  "  vec3 num = vec3 (c1) + c2 * color_pow_m1;\n"
  "  vec3 denum = vec3 (1.0) + c3 * color_pow_m1;\n"
  "  return pow (num / denum, vec3 (m2));\n"
  "}\n"
  "\n"
  "vec4 pq_inv_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (pq_inv_eotf (color.rgb), color.a);\n"
  "}\n";

static const char bt709_eotf_source[] =
  "// bt709_eotf:\n"
  "// @color: Normalized ([0,1]) electrical signal value\n"
  "// Returns: tristimulus values ([0,1])\n"
  "vec3 bt709_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_low = lessThan (color, vec3 (0.08124));\n"
  "  vec3 lo_part = color / 4.5;\n"
  "  vec3 hi_part = pow ((color + 0.099) / 1.099, vec3 (1.0 / 0.45));\n"
  "  return mix (hi_part, lo_part, is_low);\n"
  "}\n"
  "\n"
  "vec4 bt709_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (bt709_eotf (color.rgb), color.a);\n"
  "}\n";

static const char bt709_inv_eotf_source[] =
  "// bt709_inv_eotf:\n"
  "// @color: Normalized tristimulus values ([0,1])"
  "// Returns: Normalized ([0,1]) electrical signal value\n"
  "vec3 bt709_inv_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_low = lessThan (color, vec3 (0.018));\n"
  "  vec3 lo_part = 4.5 * color;\n"
  "  vec3 hi_part = 1.099 * pow (color, vec3 (0.45)) - 0.099;\n"
  "  return mix (hi_part, lo_part, is_low);\n"
  "}\n"
  "\n"
  "vec4 bt709_inv_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (bt709_inv_eotf (color.rgb), color.a);\n"
  "}\n";

static const char gamma_eotf_source[] =
  "uniform float " UNIFORM_NAME_GAMMA_EXP ";\n"
  "// gamma_eotf:\n"
  "// @color: Normalized ([0,1]) electrical signal value\n"
  "// Returns: tristimulus values ([0,1])\n"
  "vec3 gamma_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_negative = lessThan (color, vec3 (0.0));"
  "  vec3 positive = pow (abs (color), vec3 (" UNIFORM_NAME_GAMMA_EXP "));\n"
  "  vec3 negative = -positive;\n"
  "  return mix (positive, negative, is_negative);\n"
  "}\n"
  "\n"
  "vec4 gamma_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (gamma_eotf (color.rgb), color.a);\n"
  "}\n";

static const char gamma_inv_eotf_source[] =
  "uniform float " UNIFORM_NAME_INV_GAMMA_EXP ";\n"
  "// gamma_inv_eotf:\n"
  "// @color: Normalized tristimulus values ([0,1])"
  "// Returns: Normalized ([0,1]) electrical signal value\n"
  "vec3 gamma_inv_eotf (vec3 color)\n"
  "{\n"
  "  bvec3 is_negative = lessThan (color, vec3 (0.0));"
  "  vec3 positive = pow (abs (color), vec3 (" UNIFORM_NAME_INV_GAMMA_EXP "));\n"
  "  vec3 negative = -positive;\n"
  "  return mix (positive, negative, is_negative);\n"
  "}\n"
  "\n"
  "vec4 gamma_inv_eotf (vec4 color)\n"
  "{\n"
  "  return vec4 (gamma_inv_eotf (color.rgb), color.a);\n"
  "}\n";

typedef struct _ColorOpSnippet
{
  const char *source;
  const char *name;
} ColorOpSnippet;

static const ColorOpSnippet srgb_eotf = {
  .source = srgb_eotf_source,
  .name = "srgb_eotf",
};

static const ColorOpSnippet srgb_inv_eotf = {
  .source = srgb_inv_eotf_source,
  .name = "srgb_inv_eotf",
};

static const ColorOpSnippet pq_eotf = {
  .source = pq_eotf_source,
  .name = "pq_eotf",
};

static const ColorOpSnippet pq_inv_eotf = {
  .source = pq_inv_eotf_source,
  .name = "pq_inv_eotf",
};

static const ColorOpSnippet bt709_eotf = {
  .source = bt709_eotf_source,
  .name = "bt709_eotf",
};

static const ColorOpSnippet bt709_inv_eotf = {
  .source = bt709_inv_eotf_source,
  .name = "bt709_inv_eotf",
};

static const ColorOpSnippet gamma_eotf = {
  .source = gamma_eotf_source,
  .name = "gamma_eotf",
};

static const ColorOpSnippet gamma_inv_eotf = {
  .source = gamma_inv_eotf_source,
  .name = "gamma_inv_eotf",
};

static const ColorOpSnippet *
get_eotf_snippet (ClutterColorStateParams *color_state_params)
{
  switch (color_state_params->eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (color_state_params->eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
          return &srgb_eotf;
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          return &pq_eotf;
        case CLUTTER_TRANSFER_FUNCTION_BT709:
          return &bt709_eotf;
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return NULL;
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return &gamma_eotf;
    }

  g_warning ("Unhandled tranfer function %s",
             clutter_eotf_to_string (color_state_params->eotf));
  return NULL;
}

static const ColorOpSnippet *
get_inv_eotf_snippet (ClutterColorStateParams *color_state_params)
{
  switch (color_state_params->eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (color_state_params->eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
          return &srgb_inv_eotf;
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          return &pq_inv_eotf;
        case CLUTTER_TRANSFER_FUNCTION_BT709:
          return &bt709_inv_eotf;
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return NULL;
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return &gamma_inv_eotf;
    }

  g_warning ("Unhandled tranfer function %s",
             clutter_eotf_to_string (color_state_params->eotf));
  return NULL;
}

static void
get_eotf_snippets (ClutterColorStateParams  *color_state_params,
                   ClutterColorStateParams  *target_color_state_params,
                   const ColorOpSnippet    **eotf_snippet,
                   const ColorOpSnippet    **inv_eotf_snippet)
{
  *eotf_snippet = get_eotf_snippet (color_state_params);
  *inv_eotf_snippet = get_inv_eotf_snippet (target_color_state_params);
}

static const char luminance_mapping_source[] =
  "uniform float " UNIFORM_NAME_LUMINANCE_MAPPING ";\n"
  "// luminance_mapping:\n"
  "// @color: Normalized ([0,1]) in origin luminance\n"
  "// Returns: Normalized ([0,1]) in target luminance\n"
  "vec3 luminance_mapping (vec3 color)\n"
  "{\n"
  " return " UNIFORM_NAME_LUMINANCE_MAPPING " * color;\n"
  "}\n"
  "\n"
  "vec4 luminance_mapping (vec4 color)\n"
  "{\n"
  "  return vec4 (luminance_mapping (color.rgb), color.a);\n"
  "}\n";

static const ColorOpSnippet luminance_mapping = {
  .source = luminance_mapping_source,
  .name = "luminance_mapping",
};

static void
get_luminance_mapping_snippet (ClutterColorStateParams  *color_state_params,
                               ClutterColorStateParams  *target_color_state_params,
                               const ColorOpSnippet    **luminance_mapping_snippet)
{
  if (luminances_equal (color_state_params, target_color_state_params))
    return;

  *luminance_mapping_snippet = &luminance_mapping;
}

static const char color_space_mapping_source[] =
  "uniform mat3 " UNIFORM_NAME_COLOR_SPACE_MAPPING ";\n"
  "// color_space_mapping:\n"
  "// @color: Normalized ([0,1]) in origin colorspace\n"
  "// Returns: Normalized ([0,1]) in target colorspace\n"
  "vec3 color_space_mapping (vec3 color)\n"
  "{\n"
  " return " UNIFORM_NAME_COLOR_SPACE_MAPPING " * color;\n"
  "}\n"
  "\n"
  "vec4 color_space_mapping (vec4 color)\n"
  "{\n"
  "  return vec4 (color_space_mapping (color.rgb), color.a);\n"
  "}\n";

static const ColorOpSnippet color_space_mapping = {
  .source = color_space_mapping_source,
  .name = "color_space_mapping",
};

static void
get_color_space_mapping_snippet (ClutterColorStateParams  *color_state_params,
                                 ClutterColorStateParams  *target_color_state_params,
                                 const ColorOpSnippet    **color_space_mapping_snippet)
{
  if (colorimetry_equal (color_state_params, target_color_state_params))
    return;

  *color_space_mapping_snippet = &color_space_mapping;
}

static void
append_color_op_snippet (const ColorOpSnippet *color_snippet,
                         GString              *snippet_globals,
                         GString              *snippet_source,
                         const char           *snippet_color_var)
{
  if (!color_snippet)
    return;

  g_string_append_printf (snippet_globals, "%s\n", color_snippet->source);
  g_string_append_printf (snippet_source,
                          "  %s = %s (%s);\n",
                          snippet_color_var,
                          color_snippet->name,
                          snippet_color_var);
}

static CoglSnippet *
clutter_color_state_params_create_transform_snippet (ClutterColorState *color_state,
                                                     ClutterColorState *target_color_state)
{
  CoglSnippet *snippet;
  const char *snippet_color_var;
  g_autoptr (GString) snippet_globals = NULL;
  g_autoptr (GString) snippet_source = NULL;
  const ColorOpSnippet *eotf_snippet = NULL;
  const ColorOpSnippet *inv_eotf_snippet = NULL;
  const ColorOpSnippet *color_space_mapping_snippet = NULL;
  const ColorOpSnippet *luminance_mapping_snippet = NULL;
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterColorStateParams *target_color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (target_color_state);

  snippet_globals = g_string_new (NULL);
  snippet_source = g_string_new (NULL);
  snippet_color_var = "color_state_color";

  get_eotf_snippets (color_state_params,
                     target_color_state_params,
                     &eotf_snippet,
                     &inv_eotf_snippet);
  get_luminance_mapping_snippet (color_state_params,
                                 target_color_state_params,
                                 &luminance_mapping_snippet);
  get_color_space_mapping_snippet (color_state_params,
                                   target_color_state_params,
                                   &color_space_mapping_snippet);

  /*
   * The following statements generate a shader snippet that transforms colors
   * from one color state (transfer function, color space, color encoding) into
   * another. When the target color state is optically encoded, we always draw
   * into an intermediate 64 bit half float typed pixel.
   *
   * The value stored in this pixel is roughly the luminance expected by the
   * target color state's transfer function.
   *
   * For sRGB that means luminance relative the reference display as defined by
   * the sRGB specification, i.e. a value typically between 0.0 and 1.0. For PQ
   * this means absolute luminance in cd/m² (nits).
   *
   * The snippet contains a pipeline that roughly looks like this:
   *
   *     color = eotf (color)
   *     color = luminance_mapping (color)
   *     color = color_space_mapping (color)
   *     color = inv_eotf (color)
   *
   */

  g_string_append_printf (snippet_source,
                          "  vec3 %s = cogl_color_out.rgb;\n",
                          snippet_color_var);

  append_color_op_snippet (eotf_snippet,
                           snippet_globals,
                           snippet_source,
                           snippet_color_var);

  append_color_op_snippet (luminance_mapping_snippet,
                           snippet_globals,
                           snippet_source,
                           snippet_color_var);

  append_color_op_snippet (color_space_mapping_snippet,
                           snippet_globals,
                           snippet_source,
                           snippet_color_var);

  append_color_op_snippet (inv_eotf_snippet,
                           snippet_globals,
                           snippet_source,
                           snippet_color_var);

  g_string_append_printf (snippet_source,
                          "  cogl_color_out = vec4 (%s, cogl_color_out.a);\n",
                          snippet_color_var);

  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                              snippet_globals->str,
                              snippet_source->str);
  cogl_snippet_set_capability (snippet,
                               CLUTTER_PIPELINE_CAPABILITY,
                               CLUTTER_PIPELINE_CAPABILITY_COLOR_STATE);
  return snippet;
}

static float
get_luminance_mapping (ClutterColorStateParams *color_state_params,
                       ClutterColorStateParams *target_color_state_params)
{
  const ClutterLuminance *lum;
  const ClutterLuminance *target_lum;

  lum = clutter_color_state_params_get_luminance (color_state_params);
  target_lum = clutter_color_state_params_get_luminance (target_color_state_params);

  /* this is a very basic, non-contrast preserving way of matching the reference
   * luminance level */
  return (target_lum->ref / lum->ref) * (lum->max / target_lum->max);
}

static void
xyY_to_XYZ (float            x,
            float            y,
            float            Y,
            graphene_vec3_t *XYZ)
{
  if (y == 0.0f)
    {
      /* Avoid a division by 0 */
      y = FLT_EPSILON;
      g_warning ("y coordinate is 0, something is probably wrong");
    }

  graphene_vec3_init (XYZ,
                      (x * Y) / y,
                      Y,
                      ((1 - x - y) * Y) / y);
}

/*
 * Get the matrix rgb_to_xyz that makes:
 *
 *   color_XYZ = rgb_to_xyz * color_RGB
 *
 * Steps:
 *
 *   (1) white_point_XYZ = rgb_to_xyz * white_point_RGB
 *
 * Breaking down rgb_to_xyz: rgb_to_xyz = primaries_mat * coefficients_mat
 *
 *   (2) white_point_XYZ = primaries_mat * coefficients_mat * white_point_RGB
 *
 * white_point_RGB is (1, 1, 1) and coefficients_mat is a diagonal matrix:
 * coefficients_vec = coefficients_mat * white_point_RGB
 *
 *   (3) white_point_XYZ = primaries_mat * coefficients_vec
 *
 *   (4) primaries_mat^-1 * white_point_XYZ = coefficients_vec
 *
 * When coefficients_vec is calculated, coefficients_mat can be composed to
 * finally solve:
 *
 *  (5) rgb_to_xyz = primaries_mat * coefficients_mat
 *
 * Notes:
 *
 *   white_point_XYZ: xy white point coordinates transformed to XYZ space
 *                    using the maximum luminance: Y = 1
 *
 *   primaries_mat: matrix made from xy chromaticities transformed to xyz
 *                  considering x + y + z = 1
 *
 *   xyz_to_rgb = rgb_to_xyz^-1
 *
 * Reference:
 *   https://www.ryanjuckett.com/rgb-color-space-conversion/
 */
static gboolean
get_color_space_trans_matrices (ClutterColorStateParams *color_state_params,
                                graphene_matrix_t       *rgb_to_xyz,
                                graphene_matrix_t       *xyz_to_rgb)
{
  const ClutterPrimaries *primaries = get_primaries (color_state_params);
  graphene_matrix_t coefficients_mat;
  graphene_matrix_t inv_primaries_mat;
  graphene_matrix_t primaries_mat;
  graphene_vec3_t white_point_XYZ;
  graphene_vec3_t coefficients;

  graphene_matrix_init_from_float (
    &primaries_mat,
    (float [16]) {
    primaries->r_x, primaries->r_y, 1 - primaries->r_x - primaries->r_y, 0.0f,
    primaries->g_x, primaries->g_y, 1 - primaries->g_x - primaries->g_y, 0.0f,
    primaries->b_x, primaries->b_y, 1 - primaries->b_x - primaries->b_y, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });

  if (!graphene_matrix_inverse (&primaries_mat, &inv_primaries_mat))
    return FALSE;

  xyY_to_XYZ (primaries->w_x, primaries->w_y, 1.0f,  &white_point_XYZ);

  graphene_matrix_transform_vec3 (&inv_primaries_mat, &white_point_XYZ, &coefficients);

  graphene_matrix_init_from_float (
    &coefficients_mat,
    (float [16]) {
    graphene_vec3_get_x (&coefficients), 0.0f, 0.0f, 0.0f,
    0.0f, graphene_vec3_get_y (&coefficients), 0.0f, 0.0f,
    0.0f, 0.0f, graphene_vec3_get_z (&coefficients), 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });

  graphene_matrix_multiply (&coefficients_mat, &primaries_mat, rgb_to_xyz);

  if (!graphene_matrix_inverse (rgb_to_xyz, xyz_to_rgb))
    return FALSE;

  return TRUE;
}

static gboolean
primaries_white_point_equal (ClutterColorStateParams *color_state_params,
                             ClutterColorStateParams *other_color_state_params)
{
  const ClutterPrimaries *primaries;
  const ClutterPrimaries *other_primaries;

  primaries = get_primaries (color_state_params);
  other_primaries = get_primaries (other_color_state_params);

  return chromaticity_equal (primaries->w_x, primaries->w_y,
                             other_primaries->w_x, other_primaries->w_y);
}

/*
 * Get the matrix that converts XYZ chromaticity relative to src_white_point
 * to XYZ chromaticity relative to dst_white_point:
 *
 *   dst_XYZ = chromatic_adaptation * src_XYZ
 *
 * Steps:
 *
 *   chromatic_adaptation = bradford_mat^-1 * coefficients_mat * bradford_mat
 *
 *   coefficients_mat = diag (coefficients)
 *
 *   coefficients = dst_white_LMS / src_white_LMS
 *
 *   dst_white_LMS = bradford_mat * dst_white_XYZ
 *   src_white_LMS = bradford_mat * src_white_XYZ
 *
 * Notes:
 *
 *   *_white_XYZ: xy white point coordinates transformed to XYZ space
 *                using the maximum luminance: Y = 1
 *
 * Bradford matrix and reference:
 *   http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
 */
static void
get_chromatic_adaptation (ClutterColorStateParams *color_state_params,
                          ClutterColorStateParams *target_color_state_params,
                          graphene_matrix_t       *chromatic_adaptation)
{
  const ClutterPrimaries *source_primaries = get_primaries (color_state_params);
  const ClutterPrimaries *target_primaries = get_primaries (target_color_state_params);
  graphene_matrix_t coefficients_mat;
  graphene_matrix_t bradford_mat, inv_bradford_mat;
  graphene_vec3_t src_white_point_XYZ, dst_white_point_XYZ;
  graphene_vec3_t src_white_point_LMS, dst_white_point_LMS;
  graphene_vec3_t coefficients;

  graphene_matrix_init_from_float (
    &bradford_mat,
    (float [16]) {
    0.8951f, -0.7502f, 0.0389f, 0.0f,
    0.2664f, 1.7135f, -0.0685f, 0.0f,
    -0.1614f, 0.0367f, 1.0296f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });

  graphene_matrix_init_from_float (
    &inv_bradford_mat,
    (float [16]) {
    0.9869929f, 0.4323053f, -0.0085287f, 0.0f,
    -0.1470543f, 0.5183603f, 0.0400428f, 0.0f,
    0.1599627f, 0.0492912f, 0.9684867f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });

  xyY_to_XYZ (source_primaries->w_x, source_primaries->w_y, 1.0f,
              &src_white_point_XYZ);
  xyY_to_XYZ (target_primaries->w_x, target_primaries->w_y, 1.0f,
              &dst_white_point_XYZ);

  graphene_matrix_transform_vec3 (&bradford_mat, &src_white_point_XYZ,
                                  &src_white_point_LMS);
  graphene_matrix_transform_vec3 (&bradford_mat, &dst_white_point_XYZ,
                                  &dst_white_point_LMS);

  graphene_vec3_divide (&dst_white_point_LMS, &src_white_point_LMS,
                        &coefficients);

  graphene_matrix_init_from_float (
    &coefficients_mat,
    (float [16]) {
    graphene_vec3_get_x (&coefficients), 0.0f, 0.0f, 0.0f,
    0.0f, graphene_vec3_get_y (&coefficients), 0.0f, 0.0f,
    0.0f, 0.0f, graphene_vec3_get_z (&coefficients), 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  });

  graphene_matrix_multiply (&bradford_mat, &coefficients_mat,
                            chromatic_adaptation);
  graphene_matrix_multiply (chromatic_adaptation, &inv_bradford_mat,
                            chromatic_adaptation);
}

static void
get_color_space_mapping_matrix (ClutterColorStateParams *color_state_params,
                                ClutterColorStateParams *target_color_state_params,
                                float                    out_color_space_mapping[9])
{
  graphene_matrix_t matrix;
  graphene_matrix_t src_rgb_to_xyz, src_xyz_to_rgb;
  graphene_matrix_t target_rgb_to_xyz, target_xyz_to_rgb;
  graphene_matrix_t chromatic_adaptation;

  if (!get_color_space_trans_matrices (color_state_params,
                                       &src_rgb_to_xyz,
                                       &src_xyz_to_rgb) ||
      !get_color_space_trans_matrices (target_color_state_params,
                                       &target_rgb_to_xyz,
                                       &target_xyz_to_rgb))
    {
      graphene_matrix_init_identity (&matrix);
    }
  else
    {
      if (!primaries_white_point_equal (color_state_params,
                                        target_color_state_params))
        {
          get_chromatic_adaptation (color_state_params,
                                    target_color_state_params,
                                    &chromatic_adaptation);
          graphene_matrix_multiply (&src_rgb_to_xyz, &chromatic_adaptation,
                                    &matrix);
          graphene_matrix_multiply (&matrix, &target_xyz_to_rgb,
                                    &matrix);
        }
      else
        {
          graphene_matrix_multiply (&src_rgb_to_xyz, &target_xyz_to_rgb, &matrix);
        }
    }

  out_color_space_mapping[0] = graphene_matrix_get_value (&matrix, 0, 0);
  out_color_space_mapping[1] = graphene_matrix_get_value (&matrix, 0, 1);
  out_color_space_mapping[2] = graphene_matrix_get_value (&matrix, 0, 2);
  out_color_space_mapping[3] = graphene_matrix_get_value (&matrix, 1, 0);
  out_color_space_mapping[4] = graphene_matrix_get_value (&matrix, 1, 1);
  out_color_space_mapping[5] = graphene_matrix_get_value (&matrix, 1, 2);
  out_color_space_mapping[6] = graphene_matrix_get_value (&matrix, 2, 0);
  out_color_space_mapping[7] = graphene_matrix_get_value (&matrix, 2, 1);
  out_color_space_mapping[8] = graphene_matrix_get_value (&matrix, 2, 2);
}

static void
update_eotf_uniforms (ClutterColorStateParams *color_state_params,
                      CoglPipeline            *pipeline)
{
  const ClutterEOTF *eotf;
  int uniform_location_gamma_exp;

  eotf = clutter_color_state_params_get_eotf (color_state_params);
  if (eotf->type == CLUTTER_EOTF_TYPE_GAMMA)
    {
      uniform_location_gamma_exp =
        cogl_pipeline_get_uniform_location (pipeline,
                                            UNIFORM_NAME_GAMMA_EXP);

      cogl_pipeline_set_uniform_1f (pipeline,
                                    uniform_location_gamma_exp,
                                    eotf->gamma_exp);
    }
}

static void
update_inv_eotf_uniforms (ClutterColorStateParams *color_state_params,
                          CoglPipeline            *pipeline)
{
  const ClutterEOTF *target_eotf;
  int uniform_location_inv_gamma_exp;

  target_eotf = clutter_color_state_params_get_eotf (color_state_params);
  if (target_eotf->type == CLUTTER_EOTF_TYPE_GAMMA)
    {
      uniform_location_inv_gamma_exp =
        cogl_pipeline_get_uniform_location (pipeline,
                                            UNIFORM_NAME_INV_GAMMA_EXP);

      cogl_pipeline_set_uniform_1f (pipeline,
                                    uniform_location_inv_gamma_exp,
                                    1.0f / target_eotf->gamma_exp);
    }
}

static void
update_eotfs_uniforms (ClutterColorStateParams *color_state_params,
                       ClutterColorStateParams *target_color_state_params,
                       CoglPipeline            *pipeline)
{
  update_eotf_uniforms (color_state_params, pipeline);
  update_inv_eotf_uniforms (target_color_state_params, pipeline);
}

static void
update_luminance_mapping_uniforms (ClutterColorStateParams *color_state_params,
                                   ClutterColorStateParams *target_color_state_params,
                                   CoglPipeline            *pipeline)
{
  float lum_mapping;
  int uniform_location_luminance_mapping;

  if (luminances_equal (color_state_params, target_color_state_params))
    return;

  lum_mapping = get_luminance_mapping (color_state_params,
                                       target_color_state_params);

  uniform_location_luminance_mapping =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_LUMINANCE_MAPPING);

  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_luminance_mapping,
                                lum_mapping);
}

static void
update_color_space_mapping_uniforms (ClutterColorStateParams *color_state_params,
                                     ClutterColorStateParams *target_color_state_params,
                                     CoglPipeline            *pipeline)
{
  float color_space_mapping_matrix[9] = { 0 };
  int uniform_location_color_space_mapping;

  if (colorimetry_equal (color_state_params, target_color_state_params))
    return;

  get_color_space_mapping_matrix (color_state_params,
                                  target_color_state_params,
                                  color_space_mapping_matrix);

  uniform_location_color_space_mapping =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_COLOR_SPACE_MAPPING);

  cogl_pipeline_set_uniform_matrix (pipeline,
                                    uniform_location_color_space_mapping,
                                    3,
                                    1,
                                    FALSE,
                                    color_space_mapping_matrix);
}

static void
clutter_color_state_params_update_uniforms (ClutterColorState *color_state,
                                            ClutterColorState *target_color_state,
                                            CoglPipeline      *pipeline)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterColorStateParams *target_color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (target_color_state);

  update_eotfs_uniforms (color_state_params,
                         target_color_state_params,
                         pipeline);

  update_luminance_mapping_uniforms (color_state_params,
                                     target_color_state_params,
                                     pipeline);

  update_color_space_mapping_uniforms (color_state_params,
                                       target_color_state_params,
                                       pipeline);
}

static void
clutter_color_state_params_do_transform (ClutterColorState *color_state,
                                         ClutterColorState *target_color_state,
                                         const float       *input,
                                         float             *output,
                                         int                n_samples)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterColorStateParams *target_color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (target_color_state);
  ClutterEOTF eotf = color_state_params->eotf;
  ClutterEOTF target_eotf = target_color_state_params->eotf;
  int i;
  float result[3];
  float color_trans_mat[9];
  float lum_mapping;
  graphene_matrix_t g_color_trans_mat;
  graphene_vec3_t g_result;

  get_color_space_mapping_matrix (color_state_params,
                                  target_color_state_params,
                                  color_trans_mat);
  graphene_matrix_init_from_float (
    &g_color_trans_mat,
    (float [16]) {
     color_trans_mat[0], color_trans_mat[1], color_trans_mat[2], 0.0f,
     color_trans_mat[3], color_trans_mat[4], color_trans_mat[5], 0.0f,
     color_trans_mat[6], color_trans_mat[7], color_trans_mat[8], 0.0f,
     0.0f, 0.0f, 0.0f, 1.0f,
    });

  lum_mapping = get_luminance_mapping (color_state_params,
                                       target_color_state_params);

  for (i = 0; i < n_samples; i++)
    {
      /* EOTF */
      result[0] = clutter_eotf_apply (eotf, input[0]);
      result[1] = clutter_eotf_apply (eotf, input[1]);
      result[2] = clutter_eotf_apply (eotf, input[2]);

      /* Luminance mapping */
      result[0] = result[0] * lum_mapping;
      result[1] = result[1] * lum_mapping;
      result[2] = result[2] * lum_mapping;

      /* Color space mapping */
      graphene_vec3_init_from_float (&g_result, result);
      graphene_matrix_transform_vec3 (&g_color_trans_mat, &g_result, &g_result);
      graphene_vec3_to_float (&g_result, result);

      /* Inverse EOTF */
      result[0] = clutter_eotf_apply_inv (target_eotf, result[0]);
      result[1] = clutter_eotf_apply_inv (target_eotf, result[1]);
      result[2] = clutter_eotf_apply_inv (target_eotf, result[2]);

      output[0] = CLAMP (result[0], 0.0f, 1.0f);
      output[1] = CLAMP (result[1], 0.0f, 1.0f);
      output[2] = CLAMP (result[2], 0.0f, 1.0f);

      input += 3;
      output += 3;
    }
}

static gboolean
clutter_color_state_params_equals (ClutterColorState *color_state,
                                   ClutterColorState *other_color_state)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterColorStateParams *other_color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (other_color_state);

  return colorimetry_equal (color_state_params, other_color_state_params) &&
         eotf_equal (color_state_params, other_color_state_params) &&
         luminances_equal (color_state_params, other_color_state_params);
}

static char *
clutter_color_state_params_to_string (ClutterColorState *color_state)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  g_autofree char *primaries_name = NULL;
  const char *transfer_function_name;
  const ClutterLuminance *lum;
  unsigned int id;

  id = clutter_color_state_get_id (color_state);
  primaries_name = clutter_colorimetry_to_string (color_state_params->colorimetry);
  transfer_function_name = clutter_eotf_to_string (color_state_params->eotf);
  lum = clutter_color_state_params_get_luminance (color_state_params);

  return g_strdup_printf ("ClutterColorState %d "
                          "(primaries: %s, transfer function: %s, "
                          "min lum: %f, max lum: %f, ref lum: %f)",
                          id,
                          primaries_name,
                          transfer_function_name,
                          lum->min,
                          lum->max,
                          lum->ref);


}

static ClutterEncodingRequiredFormat
clutter_color_state_params_required_format (ClutterColorState *color_state)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);

  switch (color_state_params->eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (color_state_params->eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
        case CLUTTER_TRANSFER_FUNCTION_BT709:
          return CLUTTER_ENCODING_REQUIRED_FORMAT_UINT8;
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          return CLUTTER_ENCODING_REQUIRED_FORMAT_UINT10;
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return CLUTTER_ENCODING_REQUIRED_FORMAT_FP16;
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return CLUTTER_ENCODING_REQUIRED_FORMAT_UINT8;
    }

  g_assert_not_reached ();
}

/*
 * Currently sRGB content is blended with sRGB and not with linear transfer
 * characteristics.
 */
static ClutterColorState *
clutter_color_state_params_get_blending (ClutterColorState *color_state,
                                         gboolean           force)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterContext *context;
  ClutterEOTF blending_eotf;

  blending_eotf.type = CLUTTER_EOTF_TYPE_NAMED;

  switch (color_state_params->eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (color_state_params->eotf.tf_name)
        {
        /* effectively this means we will blend sRGB content in sRGB, not linear */
        case CLUTTER_TRANSFER_FUNCTION_SRGB:
          blending_eotf.tf_name = CLUTTER_TRANSFER_FUNCTION_SRGB;
          break;
        case CLUTTER_TRANSFER_FUNCTION_PQ:
        case CLUTTER_TRANSFER_FUNCTION_BT709:
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          blending_eotf.tf_name = CLUTTER_TRANSFER_FUNCTION_LINEAR;
          break;
        default:
          g_assert_not_reached ();
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      blending_eotf.tf_name = CLUTTER_TRANSFER_FUNCTION_LINEAR;
      break;
    }

  if (force)
    blending_eotf.tf_name = CLUTTER_TRANSFER_FUNCTION_LINEAR;

  if (color_state_params->eotf.type == CLUTTER_EOTF_TYPE_NAMED &&
      color_state_params->eotf.tf_name == blending_eotf.tf_name)
    return g_object_ref (color_state);

  g_object_get (G_OBJECT (color_state), "context", &context, NULL);

  return clutter_color_state_params_new_from_primitives (context,
                                                         color_state_params->colorimetry,
                                                         blending_eotf,
                                                         color_state_params->luminance);
}

static void
clutter_color_state_params_class_init (ClutterColorStateParamsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorStateClass *color_state_class = CLUTTER_COLOR_STATE_CLASS (klass);

  object_class->finalize = clutter_color_state_params_finalize;

  color_state_class->init_color_transform_key = clutter_color_state_params_init_color_transform_key;
  color_state_class->create_transform_snippet = clutter_color_state_params_create_transform_snippet;
  color_state_class->update_uniforms = clutter_color_state_params_update_uniforms;
  color_state_class->do_transform = clutter_color_state_params_do_transform;
  color_state_class->equals = clutter_color_state_params_equals;
  color_state_class->to_string = clutter_color_state_params_to_string;
  color_state_class->required_format = clutter_color_state_params_required_format;
  color_state_class->get_blending = clutter_color_state_params_get_blending;
}

static void
clutter_color_state_params_init (ClutterColorStateParams *color_state_params)
{
}

/**
 * clutter_color_state_params_new:
 *
 * Create a new ClutterColorStateParams object.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_params_new (ClutterContext          *context,
                                ClutterColorspace        colorspace,
                                ClutterTransferFunction  transfer_function)
{
  return clutter_color_state_params_new_full (context,
                                              colorspace, transfer_function,
                                              NULL, -1.0f, -1.0f, -1.0f, -1.0f);
}

/**
 * clutter_color_state_params_new_full:
 *
 * Create a new ClutterColorStateParams object with all possible parameters.
 * Some arguments might not be valid to set with other arguments.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_params_new_full (ClutterContext          *context,
                                     ClutterColorspace        colorspace,
                                     ClutterTransferFunction  transfer_function,
                                     ClutterPrimaries        *primaries,
                                     float                    gamma_exp,
                                     float                    min_lum,
                                     float                    max_lum,
                                     float                    ref_lum)
{
  ClutterColorStateParams *color_state_params;

  color_state_params = g_object_new (CLUTTER_TYPE_COLOR_STATE_PARAMS,
                                     "context", context,
                                     NULL);

  if (primaries)
    {
      color_state_params->colorimetry.type = CLUTTER_COLORIMETRY_TYPE_PRIMARIES;
      color_state_params->colorimetry.primaries =
        g_memdup2 (primaries, sizeof (*primaries));
    }
  else
    {
      color_state_params->colorimetry.type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      color_state_params->colorimetry.colorspace = colorspace;
    }

  if (gamma_exp >= 1.0f)
    {
      color_state_params->eotf.type = CLUTTER_EOTF_TYPE_GAMMA;
      color_state_params->eotf.gamma_exp = gamma_exp;
    }
  else
    {
      color_state_params->eotf.type = CLUTTER_EOTF_TYPE_NAMED;
      color_state_params->eotf.tf_name = transfer_function;
    }

  if (min_lum >= 0.0f && max_lum > 0.0f && ref_lum >= 0.0f)
    {
      color_state_params->luminance.type = CLUTTER_LUMINANCE_TYPE_EXPLICIT;
      color_state_params->luminance.min = min_lum;
      if (transfer_function == CLUTTER_TRANSFER_FUNCTION_PQ)
        color_state_params->luminance.max = min_lum + 10000.0f;
      else
        color_state_params->luminance.max = max_lum;
      color_state_params->luminance.ref = ref_lum;
    }
  else
    {
      color_state_params->luminance.type = CLUTTER_LUMINANCE_TYPE_DERIVED;
    }

  return CLUTTER_COLOR_STATE (color_state_params);
}

/**
 * clutter_color_state_params_new_from_primitives:
 *
 * Create a new ClutterColorState object using the color primitives.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_params_new_from_primitives (ClutterContext     *context,
                                                ClutterColorimetry  colorimetry,
                                                ClutterEOTF         eotf,
                                                ClutterLuminance    luminance)
{
  ClutterColorspace colorspace;
  ClutterPrimaries *primaries;
  ClutterTransferFunction tf_name;
  float gamma_exp;

  switch (colorimetry.type)
    {
    case CLUTTER_COLORIMETRY_TYPE_COLORSPACE:
      colorspace = colorimetry.colorspace;
      primaries = NULL;
      break;
    case CLUTTER_COLORIMETRY_TYPE_PRIMARIES:
      colorspace = CLUTTER_COLORSPACE_SRGB;
      primaries = colorimetry.primaries;
      break;
    }

  switch (eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      tf_name = eotf.tf_name;
      gamma_exp = -1.0f;
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      tf_name = CLUTTER_TRANSFER_FUNCTION_SRGB;
      gamma_exp = eotf.gamma_exp;
      break;
    }

  return clutter_color_state_params_new_full (context,
                                              colorspace,
                                              tf_name,
                                              primaries,
                                              gamma_exp,
                                              luminance.min,
                                              luminance.max,
                                              luminance.ref);
}
