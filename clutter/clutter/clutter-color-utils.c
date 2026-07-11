/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2022  Intel Corporation.
 * Copyright (C) 2023-2025 Red Hat
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

#include "clutter/clutter-color-utils.h"

#include <float.h>
#include <glib.h>

const char *
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

char *
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

  g_assert_not_reached ();
}

const char *
clutter_eotf_to_string (ClutterEOTF eotf)
{
  switch (eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB_PIECEWISE:
          return "sRGB piece-wise";
        case CLUTTER_TRANSFER_FUNCTION_GAMMA22:
          return "gamma 2.2";
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          return "PQ";
        case CLUTTER_TRANSFER_FUNCTION_BT1886:
          return "BT.1886";
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
          return "linear";
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return "gamma";
    }

  g_assert_not_reached ();
}

static const ClutterLuminance sdr_default_luminance = {
  .type = CLUTTER_LUMINANCE_TYPE_DERIVED,
  .min = 0.2f,
  .max = 80.0f,
  .ref = 80.0f,
  .mastering_max = 80.0f,
};

static const ClutterLuminance bt1886_default_luminance = {
  .type = CLUTTER_LUMINANCE_TYPE_DERIVED,
  .min = 0.01f,
  .max = 100.0f,
  .ref = 100.0f,
  .mastering_max = 100.0f,
};

static const ClutterLuminance pq_default_luminance = {
  .type = CLUTTER_LUMINANCE_TYPE_DERIVED,
  .min = 0.005f,
  .max = 10000.0f,
  .ref = 203.0f,
  .mastering_max = 10000.0f,
};

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

static const ClutterPrimaries p3_primaries = {
  .r_x = 0.68f, .r_y = 0.32f,
  .g_x = 0.265f, .g_y = 0.69f,
  .b_x = 0.15f, .b_y = 0.06f,
  .w_x = 0.3127f, .w_y = 0.329f,
};

static const ClutterPrimaries pal_primaries = {
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

const ClutterLuminance *
clutter_eotf_get_default_luminance (ClutterEOTF eotf)
{
  switch (eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB_PIECEWISE:
        case CLUTTER_TRANSFER_FUNCTION_LINEAR:
        case CLUTTER_TRANSFER_FUNCTION_GAMMA22:
          return &sdr_default_luminance;
        case CLUTTER_TRANSFER_FUNCTION_BT1886:
          return &bt1886_default_luminance;
        case CLUTTER_TRANSFER_FUNCTION_PQ:
          return &pq_default_luminance;
        }
      break;
    case CLUTTER_EOTF_TYPE_GAMMA:
      return &sdr_default_luminance;
    }

  g_assert_not_reached ();
}

const ClutterLuminance *
clutter_luminance_get_default_sdr (void)
{
  return &sdr_default_luminance;
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

gboolean
clutter_primaries_equal (const ClutterPrimaries *primaries,
                         const ClutterPrimaries *other_primaries)
{
  return chromaticity_equal (primaries->r_x, primaries->r_y,
                             other_primaries->r_x, other_primaries->r_y) &&
         chromaticity_equal (primaries->g_x, primaries->g_y,
                             other_primaries->g_x, other_primaries->g_y) &&
         chromaticity_equal (primaries->b_x, primaries->b_y,
                             other_primaries->b_x, other_primaries->b_y) &&
         chromaticity_equal (primaries->w_x, primaries->w_y,
                             other_primaries->w_x, other_primaries->w_y);
}

gboolean
clutter_colorimetry_equal (const ClutterColorimetry *colorimetry,
                           const ClutterColorimetry *other_colorimetry)
{
  if (colorimetry->type == CLUTTER_COLORIMETRY_TYPE_COLORSPACE &&
      other_colorimetry->type == CLUTTER_COLORIMETRY_TYPE_COLORSPACE)
    return colorimetry->colorspace == other_colorimetry->colorspace;

  return clutter_primaries_equal (
    clutter_colorimetry_get_primaries (colorimetry),
    clutter_colorimetry_get_primaries (other_colorimetry));
}

gboolean
clutter_eotf_equal (const ClutterEOTF *eotf,
                    const ClutterEOTF *other_eotf)
{
  if (eotf->type == CLUTTER_EOTF_TYPE_NAMED &&
      other_eotf->type == CLUTTER_EOTF_TYPE_NAMED)
    return eotf->tf_name == other_eotf->tf_name;

  if (eotf->type == CLUTTER_EOTF_TYPE_GAMMA &&
      other_eotf->type == CLUTTER_EOTF_TYPE_GAMMA)
    return G_APPROX_VALUE (eotf->gamma_exp, other_eotf->gamma_exp, 0.0001f);

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

gboolean
clutter_luminance_equal (const ClutterLuminance *lum,
                         const ClutterLuminance *other_lum)
{
  return luminance_value_approx_equal (lum->min, other_lum->min, 0.1f) &&
         luminance_value_approx_equal (lum->max, other_lum->max, 0.1f) &&
         luminance_value_approx_equal (lum->mastering_max,
                                       other_lum->mastering_max, 0.1f) &&
         luminance_value_approx_equal (lum->ref, other_lum->ref, 0.1f);
}

const ClutterPrimaries *
clutter_colorimetry_get_primaries (const ClutterColorimetry *colorimetry)
{
  switch (colorimetry->type)
    {
    case CLUTTER_COLORIMETRY_TYPE_COLORSPACE:
      return clutter_colorspace_to_primaries (colorimetry->colorspace);
    case CLUTTER_COLORIMETRY_TYPE_PRIMARIES:
      return colorimetry->primaries;
    }

  g_warning ("Unhandled colorimetry when getting primaries");

  return &srgb_primaries;
}

void
clutter_xyY_to_XYZ (float            x,
                    float            y,
                    float            Y,
                    graphene_vec3_t *XYZ)
{
  if (y == 0.0f)
    {
      y = FLT_EPSILON;
      g_warning ("y coordinate is 0, something is probably wrong");
    }

  graphene_vec3_init (XYZ,
                      (x * Y) / y,
                      Y,
                      ((1 - x - y) * Y) / y);
}

void
clutter_colorimetry_to_XYZ (const ClutterColorimetry *colorimetry,
                            graphene_matrix_t        *to_XYZ)
{
  const ClutterPrimaries *primaries;
  graphene_matrix_t coefficients_mat;
  graphene_matrix_t inv_primaries_mat;
  graphene_matrix_t primaries_mat;
  graphene_vec3_t white_point_XYZ;
  graphene_vec3_t coefficients;

  primaries = clutter_colorimetry_get_primaries (colorimetry);

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

  clutter_xyY_to_XYZ (primaries->w_x, primaries->w_y, 1.0f,
                      &white_point_XYZ);

  graphene_matrix_transform_vec3 (&inv_primaries_mat,
                                  &white_point_XYZ,
                                  &coefficients);

  graphene_matrix_init_scale (&coefficients_mat,
                              graphene_vec3_get_x (&coefficients),
                              graphene_vec3_get_y (&coefficients),
                              graphene_vec3_get_z (&coefficients));

  graphene_matrix_multiply (&coefficients_mat, &primaries_mat, to_XYZ);
}

void
clutter_colorimetry_from_XYZ (const ClutterColorimetry *colorimetry,
                              graphene_matrix_t        *from_XYZ)
{
  graphene_matrix_t to_XYZ;

  clutter_colorimetry_to_XYZ (colorimetry, &to_XYZ);

  if (!graphene_matrix_inverse (&to_XYZ, from_XYZ))
    {
      g_warning ("Failed computing color space mapping matrix from XYZ");
      graphene_matrix_init_identity (from_XYZ);
    }
}
