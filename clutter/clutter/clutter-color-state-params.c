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

#define UNIFORM_NAME_GAMMA_EXP "gamma_exp"
#define UNIFORM_NAME_INV_GAMMA_EXP "inv_gamma_exp"
#define UNIFORM_NAME_COLOR_SPACE_MAPPING "color_transformation_matrix"
#define UNIFORM_NAME_TO_LMS "to_lms"
#define UNIFORM_NAME_FROM_LMS "from_lms"
#define UNIFORM_NAME_SRC_MAX_LUM "src_max_lum"
#define UNIFORM_NAME_DST_MAX_LUM "dst_max_lum"
#define UNIFORM_NAME_SRC_REF_LUM "src_ref_lum"
#define UNIFORM_NAME_TONEMAPPING_REF_LUM "tone_mapping_ref_lum"
#define UNIFORM_NAME_LINEAR_TONEMAPPING "linear_mapping"
#define UNIFORM_NAME_LUMINANCE_MAPPING "luminance_factor"
#define D50_X 0.9642f
#define D50_Y 1.0f
#define D50_Z 0.8251f
#define D65_X 0.95047f
#define D65_Y 1.0f
#define D65_Z 1.08883f

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
    case CLUTTER_COLORSPACE_PAL:
      return "PAL";
    case CLUTTER_COLORSPACE_P3:
      return "P3";
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
clutter_eotf_do_apply_srgb (float input)
{
  if (input <= 0.04045f)
    return input / 12.92f;
  else
    return powf ((input + 0.055f) / 1.055f, 12.0f / 5.0f);
}

static float
clutter_eotf_apply_srgb (float input)
{
  if (input < 0.0f)
    return -clutter_eotf_do_apply_srgb (-input);

  return clutter_eotf_do_apply_srgb (input);
}

static float
clutter_eotf_do_apply_srgb_inv (float input)
{
  if (input <= 0.0031308f)
    return input * 12.92f;
  else
    return powf (input, (5.0f / 12.0f)) * 1.055f - 0.055f;
}

static float
clutter_eotf_apply_srgb_inv (float input)
{
  if (input < 0.0f)
    return -clutter_eotf_do_apply_srgb_inv (-input);

  return clutter_eotf_do_apply_srgb_inv (input);
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
  input = CLAMP (input, 0.0f, 1.0f);
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
  input = CLAMP (input, 0.0f, 1.0f);
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

static ClutterPrimaries p3_primaries = {
  .r_x = 0.68f, .r_y = 0.32f,
  .g_x = 0.265f, .g_y = 0.69f,
  .b_x = 0.15f, .b_y = 0.06f,
  .w_x = 0.3127f, .w_y = 0.329f,
};

static ClutterPrimaries pal_primaries = {
  .r_x = 0.64f, .r_y = 0.33f,
  .g_x = 0.29f, .g_y = 0.60f,
  .b_x = 0.15f, .b_y = 0.06f,
  .w_x = 0.3127f, .w_y = 0.329f,
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
    case CLUTTER_COLORSPACE_PAL:
      return &pal_primaries;
    case CLUTTER_COLORSPACE_P3:
      return &p3_primaries;
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
luminances_equal (const ClutterLuminance *lum,
                  const ClutterLuminance *other_lum)
{
  return luminance_value_approx_equal (lum->min, other_lum->min, 0.1f) &&
         luminance_value_approx_equal (lum->max, other_lum->max, 0.1f) &&
         luminance_value_approx_equal (lum->ref, other_lum->ref, 0.1f) &&
         lum->ref_is_1_0 == other_lum->ref_is_1_0;
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

static gboolean
needs_tone_mapping (const ClutterLuminance *lum,
                    const ClutterLuminance *target_lum)
{
  return lum->max > target_lum->max;
}

static gboolean
needs_lum_mapping (const ClutterLuminance *lum,
                   const ClutterLuminance *target_lum)
{
  if (needs_tone_mapping (lum, target_lum))
    return FALSE;

  if (target_lum->ref_is_1_0)
    {
      if (lum->ref_is_1_0)
        return FALSE;

      return !G_APPROX_VALUE (lum->max, lum->ref, 0.1f);
    }

  if (lum->ref_is_1_0)
    return !G_APPROX_VALUE (target_lum->ref, target_lum->max, 0.1f);

  return !G_APPROX_VALUE (target_lum->ref * lum->max,
                          lum->ref * target_lum->max,
                          0.1f);
}

static void
clutter_color_state_params_init_color_transform_key (ClutterColorState               *color_state,
                                                     ClutterColorState               *target_color_state,
                                                     ClutterColorStateTransformFlags  flags,
                                                     ClutterColorTransformKey        *key)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterColorStateParams *target_color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (target_color_state);
  const ClutterLuminance *lum, *target_lum;

  lum = clutter_color_state_params_get_luminance (color_state_params);
  target_lum =
    clutter_color_state_params_get_luminance (target_color_state_params);

  key->source_eotf_bits = get_eotf_key (color_state_params->eotf);
  key->target_eotf_bits = get_eotf_key (target_color_state_params->eotf);
  key->luminance_bit = needs_lum_mapping (lum, target_lum) ? 1 : 0;
  key->color_trans_bit = colorimetry_equal (color_state_params,
                                            target_color_state_params) ? 0 : 1;
  key->tone_mapping_bit = needs_tone_mapping (lum, target_lum) ? 1 : 0;
  key->lut_3d = 0;
  key->opaque_bit = !!(flags & CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE);
}

static const char srgb_eotf_source[] =
  "// srgb_eotf:\n"
  "// @color: Normalized ([0,1]) electrical signal value.\n"
  "// Returns: Normalized tristimulus values ([0,1])\n"
  "vec3 srgb_eotf (vec3 color)\n"
  "{\n"
  "  vec3 vsign = sign (color);\n"
  "  color = abs (color);\n"
  "  bvec3 is_low = lessThanEqual (color, vec3 (0.04045));\n"
  "  vec3 lo_part = color / 12.92;\n"
  "  vec3 hi_part = pow ((color + 0.055) / 1.055, vec3 (12.0 / 5.0));\n"
  "  return vsign * mix (hi_part, lo_part, is_low);\n"
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
  "  vec3 vsign = sign (color);\n"
  "  color = abs (color);\n"
  "  bvec3 is_lo = lessThanEqual (color, vec3 (0.0031308));\n"
  "\n"
  "  vec3 lo_part = color * 12.92;\n"
  "  vec3 hi_part = pow (color, vec3 (5.0 / 12.0)) * 1.055 - 0.055;\n"
  "  return vsign * mix (hi_part, lo_part, is_lo);\n"
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
  "  color = clamp (color, vec3 (0.0), vec3 (1.0));\n"
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
  "  color = clamp (color, vec3 (0.0), vec3 (1.0));\n"
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

static const ClutterColorOpSnippet srgb_eotf = {
  .source = srgb_eotf_source,
  .name = "srgb_eotf",
};

static const ClutterColorOpSnippet srgb_inv_eotf = {
  .source = srgb_inv_eotf_source,
  .name = "srgb_inv_eotf",
};

static const ClutterColorOpSnippet pq_eotf = {
  .source = pq_eotf_source,
  .name = "pq_eotf",
};

static const ClutterColorOpSnippet pq_inv_eotf = {
  .source = pq_inv_eotf_source,
  .name = "pq_inv_eotf",
};

static const ClutterColorOpSnippet bt709_eotf = {
  .source = bt709_eotf_source,
  .name = "bt709_eotf",
};

static const ClutterColorOpSnippet bt709_inv_eotf = {
  .source = bt709_inv_eotf_source,
  .name = "bt709_inv_eotf",
};

static const ClutterColorOpSnippet gamma_eotf = {
  .source = gamma_eotf_source,
  .name = "gamma_eotf",
};

static const ClutterColorOpSnippet gamma_inv_eotf = {
  .source = gamma_inv_eotf_source,
  .name = "gamma_inv_eotf",
};

static const ClutterColorOpSnippet *
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

static const ClutterColorOpSnippet *
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
get_eotf_snippets (ClutterColorStateParams      *color_state_params,
                   ClutterColorStateParams      *target_color_state_params,
                   const ClutterColorOpSnippet **eotf_snippet,
                   const ClutterColorOpSnippet **inv_eotf_snippet)
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

static const ClutterColorOpSnippet luminance_mapping = {
  .source = luminance_mapping_source,
  .name = "luminance_mapping",
};

static void
get_luminance_mapping_snippet (const ClutterLuminance       *lum,
                               const ClutterLuminance       *target_lum,
                               const ClutterColorOpSnippet **luminance_mapping_snippet)
{
  if (!needs_lum_mapping (lum, target_lum))
    return;

  *luminance_mapping_snippet = &luminance_mapping;
}

static const char color_space_mapping_source[] =
  "uniform mat4 " UNIFORM_NAME_COLOR_SPACE_MAPPING ";\n"
  "// color_space_mapping:\n"
  "// @color: Normalized ([0,1]) in origin colorspace\n"
  "// Returns: Normalized ([0,1]) in target colorspace\n"
  "vec3 color_space_mapping (vec3 color)\n"
  "{\n"
  " return (" UNIFORM_NAME_COLOR_SPACE_MAPPING " * vec4 (color, 1.0)).rgb;\n"
  "}\n"
  "\n"
  "vec4 color_space_mapping (vec4 color)\n"
  "{\n"
  "  return vec4 (color_space_mapping (color.rgb), color.a);\n"
  "}\n";

static const ClutterColorOpSnippet color_space_mapping = {
  .source = color_space_mapping_source,
  .name = "color_space_mapping",
};

static const char tone_mapping_source[] =
  "uniform mat4 " UNIFORM_NAME_TO_LMS ";\n"
  "uniform mat4 " UNIFORM_NAME_FROM_LMS ";\n"
  "uniform float " UNIFORM_NAME_SRC_MAX_LUM ";\n"
  "uniform float " UNIFORM_NAME_DST_MAX_LUM ";\n"
  "uniform float " UNIFORM_NAME_SRC_REF_LUM ";\n"
  "uniform float " UNIFORM_NAME_TONEMAPPING_REF_LUM ";\n"
  "uniform float " UNIFORM_NAME_LINEAR_TONEMAPPING ";\n"
  "\n"
  "const mat3 to_ictcp = mat3(\n"
  "  0.5,  1.613769531250,  4.378173828125,\n"
  "  0.5, -3.323486328125, -4.245605468750,\n"
  "  0.0,  1.709716796875, -0.132568359375\n"
  ");\n"
  "\n"
  "const mat3 from_ictcp = mat3(\n"
  "  1.0,               1.0,             1.0,\n"
  "  0.00860903703793, -0.008609037037,  0.56031335710680,\n"
  "  0.11102962500303, -0.111029625003, -0.32062717498732\n"
  ");\n"
  "\n"
  "float pq_eotf_float (float color) {\n"
  "  const float c1 = 0.8359375;\n"
  "  const float c2 = 18.8515625;\n"
  "  const float c3 = 18.6875;\n"
  "  const float oo_m1 = 1.0 / 0.1593017578125;\n"
  "  const float oo_m2 = 1.0 / 78.84375;\n"
  "  color = clamp (color, 0.0, 1.0);\n"
  "  float num = max (pow (color, oo_m2) - c1, 0.0);\n"
  "  float den = c2 - c3 * pow (color, oo_m2);\n"
  "  return pow (num / den, oo_m1);\n"
  "}\n"
  "\n"
  "float pq_inv_eotf_float (float color) {\n"
  "  const float m1 = 0.1593017578125;\n"
  "  const float m2 = 78.84375;\n"
  "  const float c1 = 0.8359375;\n"
  "  const float c2 = 18.8515625;\n"
  "  const float c3 = 18.6875;\n"
  "  color = clamp (color, 0.0, 1.0);\n"
  "  float color_pow_m1 = pow (color, m1);\n"
  "  float num = c1 + c2 * color_pow_m1;\n"
  "  float denum = 1.0 + c3 * color_pow_m1;\n"
  "  return pow (num / denum, m2);\n"
  "}\n"
  "\n"
  "// ICtCp tone_mapping:\n"
  "// @color: Normalized ([0,1]) in target colorspace\n"
  "// Returns: Normalized ([0,1]) tone mapped value\n"
  "vec3 tone_mapping (vec3 color)\n"
  "{\n"
  "  color = (" UNIFORM_NAME_TO_LMS " * vec4 (color, 1.0)).rgb;\n"
  "  color = pq_inv_eotf (color);\n"
  "  color = to_ictcp * color;\n"
  "  float luminance = pq_eotf_float (color.r) * " UNIFORM_NAME_SRC_MAX_LUM ";\n"
  "\n"
  "  if (luminance < " UNIFORM_NAME_SRC_REF_LUM ")\n"
  "    {\n"
  "      luminance *= " UNIFORM_NAME_LINEAR_TONEMAPPING ";\n"
  "    }\n"
  "  else\n"
  "    {\n"
  "      float x = (luminance - " UNIFORM_NAME_SRC_REF_LUM ") / "
                   "(" UNIFORM_NAME_SRC_MAX_LUM " - " UNIFORM_NAME_SRC_REF_LUM ");\n"
  "      luminance = " UNIFORM_NAME_TONEMAPPING_REF_LUM " + (" UNIFORM_NAME_DST_MAX_LUM " - "
                     "" UNIFORM_NAME_TONEMAPPING_REF_LUM ") * (5.0 * x) / (4.0 * x + 1.0);\n"
  "    }\n"
  "\n"
  "  color.r = pq_inv_eotf_float (luminance / " UNIFORM_NAME_DST_MAX_LUM ");\n"
  "  color = from_ictcp * color;\n"
  "  color = pq_eotf (color);\n"
  "  color = (" UNIFORM_NAME_FROM_LMS " * vec4 (color, 1.0)).rgb;\n"
  "\n"
  "  return color;\n"
  "}\n"
  "\n"
  "vec4 tone_mapping (vec4 color)\n"
  "{\n"
  "  return vec4 (tone_mapping (color.rgb), color.a);\n"
  "}\n";

static const ClutterColorOpSnippet tone_mapping = {
  .source = tone_mapping_source,
  .name = "tone_mapping",
};

static void
get_color_space_mapping_snippet (ClutterColorStateParams      *color_state_params,
                                 ClutterColorStateParams      *target_color_state_params,
                                 const ClutterColorOpSnippet **color_space_mapping_snippet)
{
  if (colorimetry_equal (color_state_params, target_color_state_params))
    return;

  *color_space_mapping_snippet = &color_space_mapping;
}

static void
get_tone_mapping_snippet (const ClutterLuminance       *lum,
                          const ClutterLuminance       *target_lum,
                          const ClutterColorOpSnippet **tone_mapping_snippet)
{
  if (!needs_tone_mapping (lum, target_lum))
    return;

  *tone_mapping_snippet = &tone_mapping;
}

static void
clutter_color_state_params_append_transform_snippet (ClutterColorState *color_state,
                                                     ClutterColorState *target_color_state,
                                                     GString           *snippet_globals,
                                                     GString           *snippet_source,
                                                     const char        *snippet_color_var)
{
  const ClutterColorOpSnippet *eotf_snippet = NULL;
  const ClutterColorOpSnippet *inv_eotf_snippet = NULL;
  const ClutterColorOpSnippet *luminance_mapping_snippet = NULL;
  const ClutterColorOpSnippet *color_space_mapping_snippet = NULL;
  const ClutterColorOpSnippet *tone_mapping_snippet = NULL;
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterColorStateParams *target_color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (target_color_state);
  const ClutterLuminance *lum, *target_lum;

  lum = clutter_color_state_params_get_luminance (color_state_params);
  target_lum =
    clutter_color_state_params_get_luminance (target_color_state_params);

  get_eotf_snippets (color_state_params,
                     target_color_state_params,
                     &eotf_snippet,
                     &inv_eotf_snippet);
  get_luminance_mapping_snippet (lum, target_lum, &luminance_mapping_snippet);
  get_color_space_mapping_snippet (color_state_params,
                                   target_color_state_params,
                                   &color_space_mapping_snippet);
  get_tone_mapping_snippet (lum, target_lum, &tone_mapping_snippet);

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
  clutter_color_op_snippet_append_global (eotf_snippet, snippet_globals);
  clutter_color_op_snippet_append_global (inv_eotf_snippet, snippet_globals);
  clutter_color_op_snippet_append_global (luminance_mapping_snippet, snippet_globals);
  clutter_color_op_snippet_append_global (color_space_mapping_snippet, snippet_globals);
  if (tone_mapping_snippet)
    {
      if (eotf_snippet != &pq_eotf)
        clutter_color_op_snippet_append_global (&pq_eotf, snippet_globals);

      if (inv_eotf_snippet != &pq_inv_eotf)
        clutter_color_op_snippet_append_global (&pq_inv_eotf, snippet_globals);

      clutter_color_op_snippet_append_global (tone_mapping_snippet, snippet_globals);
    }

  g_string_append_printf (snippet_globals,
                          "vec3 transform_color_state (vec3 %s)\n"
                          "{\n",
                          snippet_color_var);

  clutter_color_op_snippet_append_source (eotf_snippet,
                                          snippet_globals,
                                          snippet_color_var);

  clutter_color_op_snippet_append_source (luminance_mapping_snippet,
                                          snippet_globals,
                                          snippet_color_var);

  clutter_color_op_snippet_append_source (color_space_mapping_snippet,
                                          snippet_globals,
                                          snippet_color_var);

  clutter_color_op_snippet_append_source (tone_mapping_snippet,
                                          snippet_globals,
                                          snippet_color_var);

  clutter_color_op_snippet_append_source (inv_eotf_snippet,
                                          snippet_globals,
                                          snippet_color_var);

  g_string_append_printf (snippet_globals,
                          "  return %s;\n"
                          "}\n"
                          "\n",
                          snippet_color_var);
}

static float
get_lum_mapping (const ClutterLuminance *lum,
                       const ClutterLuminance *target_lum)
{
  if (target_lum->ref_is_1_0)
    {
      if (lum->ref_is_1_0)
        return 1.0f;

      return lum->max / lum->ref;
    }

  if (lum->ref_is_1_0)
      return target_lum->ref / target_lum->max;

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
 * Get the matrix to_XYZ that makes:
 *
 *   color_XYZ = to_XYZ * color_RGB
 *
 * Steps:
 *
 *   (1) white_point_XYZ = to_XYZ * white_point_RGB
 *
 * Breaking down to_XYZ: to_XYZ = primaries_mat * coefficients_mat
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
 *  (5) to_XYZ = primaries_mat * coefficients_mat
 *
 * Notes:
 *
 *   white_point_XYZ: xy white point coordinates transformed to XYZ space
 *                    using the maximum luminance: Y = 1
 *
 *   primaries_mat: matrix made from xy chromaticities transformed to xyz
 *                  considering x + y + z = 1
 *
 *   from_XYZ = to_XYZ^-1
 *
 * Reference:
 *   https://www.ryanjuckett.com/rgb-color-space-conversion/
 */
static void
get_to_XYZ (ClutterColorStateParams *color_state_params,
            graphene_matrix_t       *to_XYZ)
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
    {
      g_warning ("Failed computing color space mapping matrix to XYZ");
      graphene_matrix_init_identity (to_XYZ);
      return;
    }

  xyY_to_XYZ (primaries->w_x, primaries->w_y, 1.0f,  &white_point_XYZ);

  graphene_matrix_transform_vec3 (&inv_primaries_mat, &white_point_XYZ, &coefficients);

  graphene_matrix_init_scale (
    &coefficients_mat,
    graphene_vec3_get_x (&coefficients),
    graphene_vec3_get_y (&coefficients),
    graphene_vec3_get_z (&coefficients));

  graphene_matrix_multiply (&coefficients_mat, &primaries_mat, to_XYZ);
}

static void
get_from_XYZ (ClutterColorStateParams *color_state_params,
              graphene_matrix_t       *from_XYZ)
{
  graphene_matrix_t to_XYZ;

  get_to_XYZ (color_state_params, &to_XYZ);

  if (!graphene_matrix_inverse (&to_XYZ, from_XYZ))
    {
      g_warning ("Failed computing color space mapping matrix from XYZ");
      graphene_matrix_init_identity (from_XYZ);
    }
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
compute_chromatic_adaptation (graphene_vec3_t   *src_white_point_XYZ,
                              graphene_vec3_t   *dst_white_point_XYZ,
                              graphene_matrix_t *chromatic_adaptation)
{
  graphene_matrix_t coefficients_mat;
  graphene_matrix_t bradford_mat, inv_bradford_mat;
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

  graphene_matrix_transform_vec3 (&bradford_mat, src_white_point_XYZ,
                                  &src_white_point_LMS);
  graphene_matrix_transform_vec3 (&bradford_mat, dst_white_point_XYZ,
                                  &dst_white_point_LMS);

  graphene_vec3_divide (&dst_white_point_LMS, &src_white_point_LMS,
                        &coefficients);

  graphene_matrix_init_scale (
    &coefficients_mat,
    graphene_vec3_get_x (&coefficients),
    graphene_vec3_get_y (&coefficients),
    graphene_vec3_get_z (&coefficients));

  graphene_matrix_multiply (&bradford_mat, &coefficients_mat,
                            chromatic_adaptation);
  graphene_matrix_multiply (chromatic_adaptation, &inv_bradford_mat,
                            chromatic_adaptation);
}

static void
get_to_D50 (ClutterColorStateParams *color_state_params,
            graphene_matrix_t       *to_D50)
{
  graphene_vec3_t D50_XYZ;
  graphene_vec3_t white_point_XYZ;
  const ClutterPrimaries *primaries = get_primaries (color_state_params);

  xyY_to_XYZ (primaries->w_x, primaries->w_y, 1.0f, &white_point_XYZ);
  graphene_vec3_init (&D50_XYZ, D50_X, D50_Y, D50_Z);

  compute_chromatic_adaptation (&white_point_XYZ, &D50_XYZ, to_D50);
}

static void
get_from_D50 (ClutterColorStateParams *color_state_params,
              graphene_matrix_t       *from_D50)
{
  graphene_vec3_t D50_XYZ;
  graphene_vec3_t white_point_XYZ;
  const ClutterPrimaries *primaries = get_primaries (color_state_params);

  graphene_vec3_init (&D50_XYZ, D50_X, D50_Y, D50_Z);
  xyY_to_XYZ (primaries->w_x, primaries->w_y, 1.0f, &white_point_XYZ);

  compute_chromatic_adaptation (&D50_XYZ, &white_point_XYZ, from_D50);
}

static void
get_to_D65 (ClutterColorStateParams *color_state_params,
            graphene_matrix_t       *to_D65)
{
  graphene_vec3_t D65_XYZ;
  graphene_vec3_t white_point_XYZ;
  const ClutterPrimaries *primaries = get_primaries (color_state_params);

  xyY_to_XYZ (primaries->w_x, primaries->w_y, 1.0f, &white_point_XYZ);
  graphene_vec3_init (&D65_XYZ, D65_X, D65_Y, D65_Z);

  compute_chromatic_adaptation (&white_point_XYZ, &D65_XYZ, to_D65);
}

static void
get_from_D65 (ClutterColorStateParams *color_state_params,
              graphene_matrix_t       *from_D65)
{
  graphene_vec3_t D65_XYZ;
  graphene_vec3_t white_point_XYZ;
  const ClutterPrimaries *primaries = get_primaries (color_state_params);

  graphene_vec3_init (&D65_XYZ, D65_X, D65_Y, D65_Z);
  xyY_to_XYZ (primaries->w_x, primaries->w_y, 1.0f, &white_point_XYZ);

  compute_chromatic_adaptation (&D65_XYZ, &white_point_XYZ, from_D65);
}

static void
clutter_color_state_params_get_to_XYZ (ClutterColorStateParams *color_state_params,
                                       graphene_matrix_t       *out_to_XYZ)
{
  graphene_matrix_t *matrix = out_to_XYZ;
  graphene_matrix_t to_XYZ, to_D50;

  graphene_matrix_init_identity (matrix);

  get_to_XYZ (color_state_params, &to_XYZ);
  get_to_D50 (color_state_params, &to_D50);

  graphene_matrix_multiply (matrix, &to_XYZ, matrix);
  graphene_matrix_multiply (matrix, &to_D50, matrix);
}

static void
clutter_color_state_params_get_from_XYZ (ClutterColorStateParams *color_state_params,
                                         graphene_matrix_t       *out_from_XYZ)
{
  graphene_matrix_t *matrix = out_from_XYZ;
  graphene_matrix_t from_D50, from_XYZ;

  graphene_matrix_init_identity (matrix);

  get_from_D50 (color_state_params, &from_D50);
  get_from_XYZ (color_state_params, &from_XYZ);

  graphene_matrix_multiply (matrix, &from_D50, matrix);
  graphene_matrix_multiply (matrix, &from_XYZ, matrix);
}

static void
clutter_color_state_params_get_color_space_mapping (ClutterColorStateParams *color_state_params,
                                                    ClutterColorStateParams *target_color_state_params,
                                                    graphene_matrix_t       *out_color_space_mapping)
{
  graphene_matrix_t *matrix = out_color_space_mapping;
  graphene_matrix_t to_XYZ, from_XYZ;

  graphene_matrix_init_identity (matrix);

  clutter_color_state_params_get_to_XYZ (color_state_params, &to_XYZ);
  clutter_color_state_params_get_from_XYZ (target_color_state_params, &from_XYZ);

  graphene_matrix_multiply (matrix, &to_XYZ, matrix);
  graphene_matrix_multiply (matrix, &from_XYZ, matrix);
}

static void
get_to_LMS (graphene_matrix_t *out_to_LMS)
{
  /* This is the HPE LMS transform matrix with a crosstalk matrix applied.
   * Reference: https://professional.dolby.com/siteassets/pdfs/ictcp_dolbywhitepaper_v071.pdf */
  graphene_matrix_init_from_float (
    out_to_LMS,
    (float [16]) {
    0.35930f, -0.1921f, 0.0071f, 0.0f,
    0.69760f,  1.1005f, 0.0748f, 0.0f,
    -0.0359f,  0.0754f, 0.8433f, 0.0f,
    0.0f,      0.0f,    0.0f,    1.0f
  });
}

static void
get_from_LMS (graphene_matrix_t *out_from_LMS)
{
  /* This is the HPE LMS transform matrix with a crosstalk matrix applied.
   * Reference: https://professional.dolby.com/siteassets/pdfs/ictcp_dolbywhitepaper_v071.pdf */
  graphene_matrix_init_from_float (
    out_from_LMS,
    (float [16]) {
    2.0700350f,  0.364750f, -0.049781f, 0.0f,
    -1.326231f,  0.680546f, -0.049198f, 0.0f,
    0.2067020f, -0.045320f,  1.188097f, 0.0f,
    0.0f,        0.0f,       0.0f,      1.0f
  });
}

static gboolean
clutter_color_state_params_get_to_LMS (ClutterColorStateParams *color_state_params,
                                       graphene_matrix_t       *out_to_LMS)
{
  graphene_matrix_t *matrix = out_to_LMS;
  graphene_matrix_t to_XYZ, to_D65, to_LMS;

  graphene_matrix_init_identity (matrix);

  get_to_XYZ (color_state_params, &to_XYZ);
  get_to_D65 (color_state_params, &to_D65);
  get_to_LMS (&to_LMS);

  graphene_matrix_multiply (matrix, &to_XYZ, matrix);
  graphene_matrix_multiply (matrix, &to_D65, matrix);
  graphene_matrix_multiply (matrix, &to_LMS, matrix);

  return TRUE;
}

static void
clutter_color_state_params_get_from_LMS (ClutterColorStateParams *color_state_params,
                                         graphene_matrix_t       *out_from_LMS)
{
  graphene_matrix_t *matrix = out_from_LMS;
  graphene_matrix_t from_LMS, from_D65, from_XYZ;

  graphene_matrix_init_identity (matrix);

  get_from_LMS (&from_LMS);
  get_from_D65 (color_state_params, &from_D65);
  get_from_XYZ (color_state_params, &from_XYZ);

  graphene_matrix_multiply (matrix, &from_LMS, matrix);
  graphene_matrix_multiply (matrix, &from_D65, matrix);
  graphene_matrix_multiply (matrix, &from_XYZ, matrix);
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
  const ClutterLuminance *lum, *target_lum;

  lum = clutter_color_state_params_get_luminance (color_state_params);
  target_lum =
    clutter_color_state_params_get_luminance (target_color_state_params);

  if (!needs_lum_mapping (lum, target_lum))
    return;

  lum_mapping = get_lum_mapping (lum, target_lum);

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
  graphene_matrix_t color_space_mapping_matrix;
  float matrix[16];
  int uniform_location_color_space_mapping;

  if (colorimetry_equal (color_state_params, target_color_state_params))
    return;

  clutter_color_state_params_get_color_space_mapping (color_state_params,
                                                      target_color_state_params,
                                                      &color_space_mapping_matrix);
  graphene_matrix_to_float (&color_space_mapping_matrix, matrix);

  uniform_location_color_space_mapping =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_COLOR_SPACE_MAPPING);

  cogl_pipeline_set_uniform_matrix (pipeline,
                                    uniform_location_color_space_mapping,
                                    4,
                                    1,
                                    FALSE,
                                    matrix);
}


static void
get_ictcp_mapping_matrices (graphene_matrix_t *out_to_ictcp,
                            graphene_matrix_t *out_from_ictcp)
{
  graphene_matrix_init_from_float (
    out_to_ictcp,
    (float [16]) {
    0.5f,  1.6137695f,  4.3781738f, 0.0f,
    0.5f, -3.3234863f, -4.2456054f, 0.0f,
    0.0f,  1.7097167f, -0.1325683f, 0.0f,
    0.0f,  0.0f,        0.0f,       1.0f,
  });

  graphene_matrix_init_from_float (
    out_from_ictcp,
    (float [16]) {
    1.0f,        1.0f,        1.0f,       0.0f,
    0.0086090f, -0.0086090f,  0.5603133f, 0.0f,
    0.1110296f, -0.1110296f, -0.3206271f, 0.0f,
    0.0f,        0.0f,        0.0f,       1.0f,
  });
}

static float
get_tonemapping_ref_lum (const ClutterLuminance *lum)
{
  float headroom;

  /* The tone mapper needs for dst lum at least a headroom of 1.5 */
  headroom = lum->max / lum->ref;
  return headroom >= 1.5f ? lum->ref : lum->max / 1.5f;
}

static void
update_tone_mapping_uniforms (ClutterColorStateParams *color_state_params,
                              ClutterColorStateParams *target_color_state_params,
                              CoglPipeline            *pipeline)
{
  float matrix[16];
  int uniform_location_to_lms;
  int uniform_location_from_lms;
  int uniform_location_src_max_lum;
  int uniform_location_dst_max_lum;
  int uniform_location_src_ref_lum;
  int uniform_location_tonemapping_ref_lum;
  int uniform_location_linear_tonemapping;
  float tonemapping_ref_lum;
  const ClutterLuminance *lum;
  const ClutterLuminance *target_lum;
  graphene_matrix_t to_LMS, from_LMS;

  lum = clutter_color_state_params_get_luminance (color_state_params);
  target_lum = clutter_color_state_params_get_luminance (target_color_state_params);

  if (!needs_tone_mapping (lum, target_lum))
    return;

  clutter_color_state_params_get_to_LMS (target_color_state_params, &to_LMS);
  graphene_matrix_to_float (&to_LMS, matrix);

  uniform_location_to_lms =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_TO_LMS);

  cogl_pipeline_set_uniform_matrix (pipeline,
                                    uniform_location_to_lms,
                                    4,
                                    1,
                                    FALSE,
                                    matrix);

  clutter_color_state_params_get_from_LMS (target_color_state_params, &from_LMS);
  graphene_matrix_to_float (&from_LMS, matrix);

  uniform_location_from_lms =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_FROM_LMS);

  cogl_pipeline_set_uniform_matrix (pipeline,
                                    uniform_location_from_lms,
                                    4,
                                    1,
                                    FALSE,
                                    matrix);

  uniform_location_src_max_lum =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_SRC_MAX_LUM);
  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_src_max_lum,
                                lum->max);

  uniform_location_dst_max_lum =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_DST_MAX_LUM);
  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_dst_max_lum,
                                target_lum->max);

  uniform_location_src_ref_lum =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_SRC_REF_LUM);
  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_src_ref_lum,
                                lum->ref);

  tonemapping_ref_lum = get_tonemapping_ref_lum (target_lum);

  uniform_location_tonemapping_ref_lum =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_TONEMAPPING_REF_LUM);
  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_tonemapping_ref_lum,
                                tonemapping_ref_lum);

  uniform_location_linear_tonemapping =
    cogl_pipeline_get_uniform_location (pipeline,
                                        UNIFORM_NAME_LINEAR_TONEMAPPING);
  cogl_pipeline_set_uniform_1f (pipeline,
                                uniform_location_linear_tonemapping,
                                tonemapping_ref_lum / lum->ref);
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

  update_tone_mapping_uniforms (color_state_params,
                                target_color_state_params,
                                pipeline);
}

static void
clutter_luminance_apply_tone_mapping (const ClutterLuminance *lum,
                                      const ClutterLuminance *target_lum,
                                      float                  *data,
                                      int                     n_samples)
{
  float result[4];
  float tonemapping_ref_lum, luminance;
  graphene_matrix_t to_LMS, from_LMS;
  graphene_matrix_t to_D65, from_D65;
  graphene_matrix_t to_ictcp, from_ictcp;
  graphene_vec4_t g_result;
  graphene_vec3_t D65_XYZ, D50_XYZ;
  int i;

  /* Data is in XYZ (D50) */
  graphene_vec3_init (&D65_XYZ, D65_X, D65_Y, D65_Z);
  graphene_vec3_init (&D50_XYZ, D50_X, D50_Y, D50_Z);

  compute_chromatic_adaptation (&D50_XYZ, &D65_XYZ, &to_D65);
  get_to_LMS (&to_LMS);
  graphene_matrix_multiply (&to_D65, &to_LMS, &to_LMS);

  compute_chromatic_adaptation (&D65_XYZ, &D50_XYZ, &from_D65);
  get_from_LMS (&from_LMS);
  graphene_matrix_multiply (&from_LMS, &from_D65, &from_LMS);

  get_ictcp_mapping_matrices (&to_ictcp, &from_ictcp);

  tonemapping_ref_lum = get_tonemapping_ref_lum (target_lum);

  for (i = 0; i < n_samples; i++)
    {
      result[0] = data[0];
      result[1] = data[1];
      result[2] = data[2];
      result[3] = 1.0f;

      /* To LMS (D65) */
      graphene_vec4_init_from_float (&g_result, result);
      graphene_matrix_transform_vec4 (&to_LMS, &g_result, &g_result);
      graphene_vec4_to_float (&g_result, result);

      /* Encode in PQ */
      result[0] = clutter_eotf_apply_pq_inv (result[0]);
      result[1] = clutter_eotf_apply_pq_inv (result[1]);
      result[2] = clutter_eotf_apply_pq_inv (result[2]);

      /* To ICtCp */
      graphene_vec4_init_from_float (&g_result, result);
      graphene_matrix_transform_vec4 (&to_ictcp, &g_result, &g_result);
      graphene_vec4_to_float (&g_result, result);

      /* Work with I channel */
      luminance = clutter_eotf_apply_pq (result[0]) * lum->max;
      if (luminance < lum->ref)
        {
          luminance *= tonemapping_ref_lum / lum->ref;
        }
      else
        {
          float num = luminance - lum->ref;
          float den = lum->max - lum->ref;
          luminance = tonemapping_ref_lum +
                      (target_lum->max - tonemapping_ref_lum) *
                      powf (num / den, 0.5f);
        }
      result[0] = clutter_eotf_apply_pq_inv (luminance / target_lum->max);

      /* To LMS in PQ */
      graphene_vec4_init_from_float (&g_result, result);
      graphene_matrix_transform_vec4 (&from_ictcp, &g_result, &g_result);
      graphene_vec4_to_float (&g_result, result);

      /* Unencode PQ */
      result[0] = clutter_eotf_apply_pq (result[0]);
      result[1] = clutter_eotf_apply_pq (result[1]);
      result[2] = clutter_eotf_apply_pq (result[2]);

      /* To XYZ (D50) */
      graphene_vec4_init_from_float (&g_result, result);
      graphene_matrix_transform_vec4 (&from_LMS, &g_result, &g_result);
      graphene_vec4_to_float (&g_result, result);

      data[0] = result[0];
      data[1] = result[1];
      data[2] = result[2];

      data += 3;
    }
}

static void
clutter_luminance_apply_luminance_mapping (const ClutterLuminance *lum,
                                           const ClutterLuminance *target_lum,
                                           float                  *data,
                                           int                     n_samples)
{
  float lum_mapping;
  int i;

  if (!needs_lum_mapping (lum, target_lum))
    return;

  lum_mapping = get_lum_mapping (lum, target_lum);

  for (i = 0; i < n_samples; i++)
    {
      data[0] *= lum_mapping;
      data[1] *= lum_mapping;
      data[2] *= lum_mapping;
      data += 3;
    }
}

static void
clutter_color_state_params_do_transform_to_XYZ (ClutterColorState *color_state,
                                                float             *data,
                                                int                n_samples)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterEOTF eotf = color_state_params->eotf;
  int i;
  float result[4];
  graphene_matrix_t color_trans_mat;
  graphene_vec4_t g_result;

  clutter_color_state_params_get_to_XYZ (color_state_params,
                                         &color_trans_mat);

  for (i = 0; i < n_samples; i++)
    {
      /* EOTF */
      result[0] = clutter_eotf_apply (eotf, data[0]);
      result[1] = clutter_eotf_apply (eotf, data[1]);
      result[2] = clutter_eotf_apply (eotf, data[2]);
      result[3] = 1.0f;

      /* Color space mapping */
      graphene_vec4_init_from_float (&g_result, result);
      graphene_matrix_transform_vec4 (&color_trans_mat, &g_result, &g_result);
      graphene_vec4_to_float (&g_result, result);

      data[0] = result[0];
      data[1] = result[1];
      data[2] = result[2];

      data += 3;
    }
}

static void
clutter_color_state_params_do_transform_from_XYZ (ClutterColorState *color_state,
                                                  float             *data,
                                                  int                n_samples)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterEOTF eotf = color_state_params->eotf;
  int i;
  float result[4];
  graphene_matrix_t color_trans_mat;
  graphene_vec4_t g_result;

  clutter_color_state_params_get_from_XYZ (color_state_params,
                                           &color_trans_mat);

  for (i = 0; i < n_samples; i++)
    {
      result[0] = data[0];
      result[1] = data[1];
      result[2] = data[2];
      result[3] = 1.0f;

      /* Color space mapping */
      graphene_vec4_init_from_float (&g_result, result);
      graphene_matrix_transform_vec4 (&color_trans_mat, &g_result, &g_result);
      graphene_vec4_to_float (&g_result, result);

      /* Inverse EOTF */
      result[0] = clutter_eotf_apply_inv (eotf, result[0]);
      result[1] = clutter_eotf_apply_inv (eotf, result[1]);
      result[2] = clutter_eotf_apply_inv (eotf, result[2]);

      data[0] = CLAMP (result[0], 0.0f, 1.0f);
      data[1] = CLAMP (result[1], 0.0f, 1.0f);
      data[2] = CLAMP (result[2], 0.0f, 1.0f);

      data += 3;
    }
}

/**
 * clutter_color_state_params_do_tone_mapping:
 * @color_state: a #ClutterColorState
 * @other_color_state: the other a #ClutterColorState
 * @data: (array): The data
 * @n_samples: The number of data samples
 *
 * Applies the tone mapping to the given #ClutterColorState
 */
void
clutter_color_state_params_do_tone_mapping (ClutterColorState *color_state,
                                            ClutterColorState *other_color_state,
                                            float             *data,
                                            int                n_samples)
{
  const ClutterLuminance *src_lum;
  const ClutterLuminance *dst_lum;
  ClutterColorStateParams *color_state_params;

  if (CLUTTER_IS_COLOR_STATE_PARAMS (color_state))
    {
      color_state_params = CLUTTER_COLOR_STATE_PARAMS (color_state);
      src_lum = clutter_color_state_params_get_luminance (color_state_params);
    }
  else
    {
      src_lum = &sdr_default_luminance;
    }

  if (CLUTTER_IS_COLOR_STATE_PARAMS (other_color_state))
    {
      color_state_params = CLUTTER_COLOR_STATE_PARAMS (other_color_state);
      dst_lum = clutter_color_state_params_get_luminance (color_state_params);
    }
  else
    {
      dst_lum = &sdr_default_luminance;
    }

  if (needs_tone_mapping (src_lum, dst_lum))
    {
      clutter_luminance_apply_tone_mapping (src_lum,
                                            dst_lum,
                                            data,
                                            n_samples);
    }
  else if (needs_lum_mapping (src_lum, dst_lum))
    {
      clutter_luminance_apply_luminance_mapping (src_lum,
                                                 dst_lum,
                                                 data,
                                                 n_samples);
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
  const ClutterLuminance *lum, *target_lum;

  if (!colorimetry_equal (color_state_params, other_color_state_params) ||
      !eotf_equal (color_state_params, other_color_state_params))
    return FALSE;

  lum = clutter_color_state_params_get_luminance (color_state_params);
  target_lum =
    clutter_color_state_params_get_luminance (other_color_state_params);

  return luminances_equal (lum, target_lum);
}

static gboolean
clutter_color_state_params_needs_mapping (ClutterColorState *color_state,
                                          ClutterColorState *target_color_state)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterColorStateParams *target_color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (target_color_state);
  const ClutterLuminance *lum, *target_lum;

  if (!colorimetry_equal (color_state_params, target_color_state_params) ||
      !eotf_equal (color_state_params, target_color_state_params))
    return TRUE;

  lum = clutter_color_state_params_get_luminance (color_state_params);
  target_lum =
    clutter_color_state_params_get_luminance (target_color_state_params);

  return needs_tone_mapping (lum, target_lum) ||
         needs_lum_mapping (lum, target_lum);
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
  const ClutterLuminance *luminance;

  luminance = clutter_color_state_params_get_luminance (color_state_params);
  if (luminance->max > luminance->ref && luminance->ref_is_1_0)
    return CLUTTER_ENCODING_REQUIRED_FORMAT_FP16;

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
 * Currently all content is blended with sRGB transfer characteristics.
 */
static ClutterColorState *
clutter_color_state_params_get_blending (ClutterColorState *color_state,
                                         gboolean           force_linear)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  ClutterContext *context;
  ClutterColorimetry blending_colorimetry;
  ClutterEOTF blending_eotf;
  ClutterLuminance blending_luminance;

  blending_eotf.type = CLUTTER_EOTF_TYPE_NAMED;

  if (force_linear)
    {
      blending_colorimetry = color_state_params->colorimetry;
      blending_eotf.tf_name = CLUTTER_TRANSFER_FUNCTION_LINEAR;
    }
  else
    {
      blending_colorimetry.type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      blending_colorimetry.colorspace = CLUTTER_COLORSPACE_SRGB;
      blending_eotf.tf_name = CLUTTER_TRANSFER_FUNCTION_SRGB;
    }

  if (color_state_params->eotf.type == CLUTTER_EOTF_TYPE_NAMED &&
      color_state_params->eotf.tf_name == blending_eotf.tf_name)
    return g_object_ref (color_state);

  blending_luminance =
    *clutter_color_state_params_get_luminance (color_state_params);
  blending_luminance.ref_is_1_0 =
    blending_luminance.max > blending_luminance.ref;

  g_object_get (G_OBJECT (color_state), "context", &context, NULL);

  return clutter_color_state_params_new_from_primitives (context,
                                                         blending_colorimetry,
                                                         blending_eotf,
                                                         blending_luminance);
}

static void
clutter_color_state_params_class_init (ClutterColorStateParamsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorStateClass *color_state_class = CLUTTER_COLOR_STATE_CLASS (klass);

  object_class->finalize = clutter_color_state_params_finalize;

  color_state_class->init_color_transform_key = clutter_color_state_params_init_color_transform_key;
  color_state_class->append_transform_snippet = clutter_color_state_params_append_transform_snippet;
  color_state_class->update_uniforms = clutter_color_state_params_update_uniforms;
  color_state_class->do_transform_to_XYZ = clutter_color_state_params_do_transform_to_XYZ;
  color_state_class->do_transform_from_XYZ = clutter_color_state_params_do_transform_from_XYZ;
  color_state_class->equals = clutter_color_state_params_equals;
  color_state_class->needs_mapping = clutter_color_state_params_needs_mapping;
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
                                              NULL, -1.0f, -1.0f, -1.0f, -1.0f,
                                              FALSE);
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
                                     float                    ref_lum,
                                     gboolean                 ref_is_1_0)
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

  color_state_params->luminance.ref_is_1_0 = ref_is_1_0;
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
                                              luminance.ref,
                                              luminance.ref_is_1_0);
}

static gboolean
cicp_primaries_to_clutter (ClutterCicpPrimaries   primaries,
                           ClutterColorimetry    *colorimetry,
                           GError               **error)
{
  switch (primaries)
    {
    case CLUTTER_CICP_PRIMARIES_SRGB:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_SRGB;
      return TRUE;
    case CLUTTER_CICP_PRIMARIES_PAL:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_PAL;
      return TRUE;
    case CLUTTER_CICP_PRIMARIES_NTSC:
    case CLUTTER_CICP_PRIMARIES_NTSC_2:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_NTSC;
      return TRUE;
    case CLUTTER_CICP_PRIMARIES_BT2020:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_BT2020;
      return TRUE;
    case CLUTTER_CICP_PRIMARIES_P3:
      colorimetry->type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      colorimetry->colorspace = CLUTTER_COLORSPACE_P3;
      return TRUE;
    default:
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unhandled cicp color primaries: %u",
                   primaries);
      return FALSE;
    }
}

static gboolean
cicp_transfer_to_clutter (ClutterCicpTransfer   transfer,
                          ClutterEOTF          *eotf,
                          GError              **error)
{
  switch (transfer)
    {
    case CLUTTER_CICP_TRANSFER_BT709:
    case CLUTTER_CICP_TRANSFER_BT601:
    case CLUTTER_CICP_TRANSFER_BT2020:
    case CLUTTER_CICP_TRANSFER_BT2020_2:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_BT709;
      return TRUE;
    case CLUTTER_CICP_TRANSFER_GAMMA22:
      eotf->type = CLUTTER_EOTF_TYPE_GAMMA;
      eotf->gamma_exp = 2.2f;
      return TRUE;
    case CLUTTER_CICP_TRANSFER_GAMMA28:
      eotf->type = CLUTTER_EOTF_TYPE_GAMMA;
      eotf->gamma_exp = 2.8f;
      return TRUE;
    case CLUTTER_CICP_TRANSFER_LINEAR:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_LINEAR;
      return TRUE;
    case CLUTTER_CICP_TRANSFER_SRGB:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_SRGB;
      return TRUE;
    case CLUTTER_CICP_TRANSFER_PQ:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_PQ;
      return TRUE;
    case CLUTTER_CICP_TRANSFER_HLG:
    default:
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unhandled cicp transfer characteristics: %u",
                   transfer);
      return FALSE;
    }
}

/**
 * clutter_color_state_params_new_from_cicp:
 * @context: a  clutter context
 * @cicp: the cicp tuple
 * @error: return location for an error
 *
 * Create a new ClutterColorState object from a cicp tuple.
 *
 * See ITU-T H.273 for the specifications of the numbers in
 * the ClutterCicp struct.
 *
 * Return value: A new ClutterColorState object.
 **/
ClutterColorState *
clutter_color_state_params_new_from_cicp (ClutterContext     *context,
                                          const ClutterCicp  *cicp,
                                          GError            **error)
{
  ClutterColorimetry colorimetry;
  ClutterEOTF eotf;
  ClutterLuminance lum;

  if (!cicp_primaries_to_clutter (cicp->primaries, &colorimetry, error))
    return NULL;

  if (!cicp_transfer_to_clutter (cicp->transfer, &eotf, error))
    return NULL;

  if (cicp->matrix_coefficients != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unhandled cicp matrix coefficients: %u",
                   cicp->matrix_coefficients);
      return NULL;
    }

  if (cicp->video_full_range_flag != 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unhandled cicp full-range flag: %u",
                   cicp->video_full_range_flag);
      return NULL;
    }

  lum.type = CLUTTER_LUMINANCE_TYPE_DERIVED;

  return clutter_color_state_params_new_from_primitives (context,
                                                         colorimetry,
                                                         eotf,
                                                         lum);
}
