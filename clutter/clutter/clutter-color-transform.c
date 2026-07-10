/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Red Hat
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
 */

/*
 * ClutterColorTransform Connection Space
 *
 * All color transforms in this module connect through a standardized
 * intermediate color space to ensure consistency and avoid clipping:
 *
 * CONNECTION SPACE: Linear BT.2020 (Rec. 2020) with D65 white point
 *
 * This choice provides:
 * - Wide gamut coverage (wider than most source/target color spaces)
 * - Values stay within [0,1] for typical content when sampling 3D LUTs
 * - Consistent connection point for both parametric and ICC transforms
 * - D65 white point matches BT.2020 specification
 *
 * Transform pipeline structure:
 *   Source color state → Linear BT.2020 D65 → Target color state
 *
 * For parametric (params) color states:
 *   1. Apply EOTF to get linear light
 *   2. Matrix transform: source primaries → XYZ D50
 *   3. Chromatic adaptation: D50 → D65
 *   4. Matrix transform: XYZ D65 → BT.2020 primaries
 *
 * For ICC profile color states:
 *   1. 3D LUT: ICC profile → linear BT.2020 D65
 *      (sampled uniformly in [0,1]³ linear BT.2020 space)
 *
 * Note: XYZ is used as an intermediate step for matrix calculations,
 * but is NOT the connection space itself.
 */

#include "config.h"

#include "clutter/clutter-color-transform-private.h"
#include "clutter/clutter-color-state-params.h"
#include "clutter/clutter-color-state-icc.h"
#include "clutter/clutter-color-state.h"
#include "clutter/clutter-color-op.h"
#include "clutter/clutter-context.h"

#include <lcms2.h>

#define LUT_3D_SIZE 33

G_DEFINE_AUTOPTR_CLEANUP_FUNC (cmsHTRANSFORM, cmsDeleteTransform)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (cmsHPROFILE, cmsCloseProfile)

typedef cmsToneCurve *CmsToneCurveTriple[3];

static inline void
clear_cms_tone_curve_triple (CmsToneCurveTriple *p)
{
  cmsFreeToneCurveTriple (*p);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (CmsToneCurveTriple, clear_cms_tone_curve_triple)

struct _ClutterColorTransform
{
  GObject parent;

  ClutterColorPipeline *pipeline;
};

G_DEFINE_FINAL_TYPE (ClutterColorTransform,
                     clutter_color_transform,
                     G_TYPE_OBJECT)

static void
clutter_color_transform_finalize (GObject *object)
{
  ClutterColorTransform *transform = CLUTTER_COLOR_TRANSFORM (object);

  g_clear_object (&transform->pipeline);

  G_OBJECT_CLASS (clutter_color_transform_parent_class)->finalize (object);
}

static void
clutter_color_transform_class_init (ClutterColorTransformClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = clutter_color_transform_finalize;
}

static void
clutter_color_transform_init (ClutterColorTransform *transform)
{
}

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
       0.89510f, -0.7502f,  0.0389f, 0.0f,
       0.26640f,  1.7135f, -0.0685f, 0.0f,
      -0.16140f,  0.0367f,  1.0296f, 0.0f,
       0.00000f,  0.0000f,  0.0000f, 1.0f,
    });

  graphene_matrix_init_from_float (
    &inv_bradford_mat,
    (float [16]) {
       0.9869929f, 0.4323053f, -0.0085287f, 0.0f,
      -0.1470543f, 0.5183603f,  0.0400428f, 0.0f,
       0.1599627f, 0.0492912f,  0.9684867f, 0.0f,
       0.0000000f, 0.0000000f,  0.0000000f, 1.0f,
    });

  graphene_matrix_transform_vec3 (&bradford_mat,
                                  src_white_point_XYZ,
                                  &src_white_point_LMS);
  graphene_matrix_transform_vec3 (&bradford_mat,
                                  dst_white_point_XYZ,
                                  &dst_white_point_LMS);

  graphene_vec3_divide (&dst_white_point_LMS,
                        &src_white_point_LMS,
                        &coefficients);

  graphene_matrix_init_scale (&coefficients_mat,
                              graphene_vec3_get_x (&coefficients),
                              graphene_vec3_get_y (&coefficients),
                              graphene_vec3_get_z (&coefficients));

  graphene_matrix_multiply (&bradford_mat,
                            &coefficients_mat,
                            chromatic_adaptation);
  graphene_matrix_multiply (chromatic_adaptation,
                            &inv_bradford_mat,
                            chromatic_adaptation);
}

static void
get_to_D65 (const ClutterColorimetry *colorimetry,
            graphene_matrix_t        *to_D65)
{
  const ClutterPrimaries *primaries;
  graphene_vec3_t D65_XYZ;
  graphene_vec3_t white_point_XYZ;

  primaries = clutter_colorimetry_get_primaries (colorimetry);
  clutter_xyY_to_XYZ (primaries->w_x, primaries->w_y, 1.0f, &white_point_XYZ);
  graphene_vec3_init (&D65_XYZ, CLUTTER_D65_X, CLUTTER_D65_Y, CLUTTER_D65_Z);

  compute_chromatic_adaptation (&white_point_XYZ, &D65_XYZ, to_D65);
}

static void
get_from_D65 (const ClutterColorimetry *colorimetry,
              graphene_matrix_t        *from_D65)
{
  const ClutterPrimaries *primaries;
  graphene_vec3_t D65_XYZ;
  graphene_vec3_t white_point_XYZ;

  primaries = clutter_colorimetry_get_primaries (colorimetry);
  graphene_vec3_init (&D65_XYZ, CLUTTER_D65_X, CLUTTER_D65_Y, CLUTTER_D65_Z);
  clutter_xyY_to_XYZ (primaries->w_x, primaries->w_y, 1.0f, &white_point_XYZ);

  compute_chromatic_adaptation (&D65_XYZ, &white_point_XYZ, from_D65);
}

static void
get_params_to_linear_bt2020 (ClutterColorStateParams *params,
                             graphene_matrix_t       *out_matrix)
{
  const ClutterColorimetry *colorimetry;
  ClutterColorimetry bt2020_colorimetry = {
    .type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE,
    .colorspace = CLUTTER_COLORSPACE_BT2020,
  };
  graphene_matrix_t to_XYZ, to_D65, from_XYZ_bt2020;
  graphene_matrix_t temp;

  colorimetry = clutter_color_state_params_get_colorimetry (params);

  clutter_colorimetry_to_XYZ (colorimetry, &to_XYZ);
  get_to_D65 (colorimetry, &to_D65);
  clutter_colorimetry_from_XYZ (&bt2020_colorimetry, &from_XYZ_bt2020);

  /* source → XYZ → XYZ D65 → BT.2020 */
  graphene_matrix_multiply (&to_XYZ, &to_D65, &temp);
  graphene_matrix_multiply (&temp, &from_XYZ_bt2020, out_matrix);
}

static void
get_params_from_linear_bt2020 (ClutterColorStateParams *params,
                               graphene_matrix_t       *out_matrix)
{
  const ClutterColorimetry *colorimetry;
  ClutterColorimetry bt2020_colorimetry = {
    .type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE,
    .colorspace = CLUTTER_COLORSPACE_BT2020,
  };
  graphene_matrix_t to_XYZ_bt2020, from_D65, from_XYZ;
  graphene_matrix_t temp;

  colorimetry = clutter_color_state_params_get_colorimetry (params);

  clutter_colorimetry_to_XYZ (&bt2020_colorimetry, &to_XYZ_bt2020);
  get_from_D65 (colorimetry, &from_D65);
  clutter_colorimetry_from_XYZ (colorimetry, &from_XYZ);

  /* BT.2020 → XYZ D65 → XYZ → target */
  graphene_matrix_multiply (&to_XYZ_bt2020, &from_D65, &temp);
  graphene_matrix_multiply (&temp, &from_XYZ, out_matrix);
}

static ClutterColorOp *
create_eotf_op (const ClutterEOTF *eotf)
{
  if (eotf->type == CLUTTER_EOTF_TYPE_GAMMA)
    {
      if (G_APPROX_VALUE (eotf->gamma_exp, 1.0f, FLT_EPSILON))
        return NULL;

      return clutter_color_op_gamma_power_new (eotf->gamma_exp);
    }

  switch (eotf->tf_name)
    {
    case CLUTTER_TRANSFER_FUNCTION_SRGB_PIECEWISE:
      return clutter_color_op_srgb_piecewise_eotf_new (FALSE);
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return clutter_color_op_pq_eotf_new ();
    case CLUTTER_TRANSFER_FUNCTION_BT1886:
      return clutter_color_op_gamma_power_new (2.4f);
    case CLUTTER_TRANSFER_FUNCTION_GAMMA22:
      return clutter_color_op_gamma_power_new (2.2f);
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return NULL;
    }

  g_assert_not_reached ();
}

static ClutterColorOp *
create_inv_eotf_op (const ClutterEOTF *eotf)
{
  if (eotf->type == CLUTTER_EOTF_TYPE_GAMMA)
    {
      if (G_APPROX_VALUE (eotf->gamma_exp, 1.0f, FLT_EPSILON))
        return NULL;

      return clutter_color_op_gamma_power_new (1.0f / eotf->gamma_exp);
    }

  switch (eotf->tf_name)
    {
    case CLUTTER_TRANSFER_FUNCTION_SRGB_PIECEWISE:
      return clutter_color_op_srgb_piecewise_inv_eotf_new (FALSE);
    case CLUTTER_TRANSFER_FUNCTION_PQ:
      return clutter_color_op_pq_inv_eotf_new ();
    case CLUTTER_TRANSFER_FUNCTION_BT1886:
      return clutter_color_op_gamma_power_new (1.0f / 2.4f);
    case CLUTTER_TRANSFER_FUNCTION_GAMMA22:
      return clutter_color_op_gamma_power_new (1.0f / 2.2f);
    case CLUTTER_TRANSFER_FUNCTION_LINEAR:
      return NULL;
    }

  g_assert_not_reached ();
}

static void
add_params_to_linear_bt2020_ops (ClutterColorPipeline    *pipeline,
                                 ClutterColorStateParams *params)
{
  const ClutterEOTF *eotf;
  graphene_matrix_t to_linear_bt2020_stack;
  graphene_matrix_t *to_linear_bt2020;

  eotf = clutter_color_state_params_get_eotf (params);

  /* 1. Apply EOTF to get linear light in source colorspace */
  {
    ClutterColorOp *eotf_op = create_eotf_op (eotf);

    if (eotf_op)
      clutter_color_pipeline_take_op (pipeline, eotf_op);
  }

  /* 2. Transform to linear BT.2020 D65 (via XYZ) */
  get_params_to_linear_bt2020 (params, &to_linear_bt2020_stack);

  to_linear_bt2020 = graphene_matrix_alloc ();
  graphene_matrix_init_from_matrix (to_linear_bt2020,
                                    &to_linear_bt2020_stack);
  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_matrix_4x4_new (to_linear_bt2020));
}

static void
add_params_from_linear_bt2020_ops (ClutterColorPipeline    *pipeline,
                                   ClutterColorStateParams *params)
{
  const ClutterEOTF *eotf;
  graphene_matrix_t from_linear_bt2020_stack;
  graphene_matrix_t *from_linear_bt2020;

  eotf = clutter_color_state_params_get_eotf (params);

  /* 1. Transform from linear BT.2020 D65 to target colorspace (via XYZ) */
  get_params_from_linear_bt2020 (params, &from_linear_bt2020_stack);
  from_linear_bt2020 = graphene_matrix_alloc ();
  graphene_matrix_init_from_matrix (from_linear_bt2020,
                                    &from_linear_bt2020_stack);
  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_matrix_4x4_new (from_linear_bt2020));

  /* 2. Apply inverse EOTF */
  {
    ClutterColorOp *inv_eotf_op = create_inv_eotf_op (eotf);

    if (inv_eotf_op)
      clutter_color_pipeline_take_op (pipeline, inv_eotf_op);
  }
}

static void
sample_3d_lut_input (float *data,
                     int    lut_size)
{
  int index;
  int i, j, k;
  float x, y, z;
  float step;

  step = 1.0f / (lut_size - 1);

  index = 0;
  for (k = 0, z = 0.0f; k < lut_size; k++, z += step)
    for (j = 0, y = 0.0f; j < lut_size; j++, y += step)
      for (i = 0, x = 0.0f; i < lut_size; i++, x += step)
        {
          data[index++] = x;
          data[index++] = y;
          data[index++] = z;
        }
}

static void
do_lcms_transform (cmsHTRANSFORM *transform,
                   float         *data,
                   int            n_samples)
{
  /*
   * Apply LCMS color transform without clamping.
   *
   * The 3D LUT must store out-of-gamut values (negative or >1.0) to maintain
   * accuracy during interpolation. When the connection space (linear BT.2020)
   * is wider than the target gamut, some grid points will map to out-of-gamut
   * colors in the target space. If we clamp these values, tetrahedral
   * interpolation will produce incorrect results for colors near gamut boundaries.
   *
   * Clamping should only occur at the final output stage if needed.
   */
  cmsDoTransform (transform, data, data, n_samples);
}

static cmsHPROFILE *
create_linear_bt2020_profile (void)
{
  const ClutterPrimaries *bt2020_primaries;
  cmsCIExyY white_point;
  cmsCIExyYTRIPLE primaries;
  cmsToneCurve *curve;
  g_autoptr (cmsHPROFILE) profile = NULL;

  bt2020_primaries = clutter_colorspace_to_primaries (CLUTTER_COLORSPACE_BT2020);

  /* BT.2020 uses D65 white point */
  white_point.x = bt2020_primaries->w_x;
  white_point.y = bt2020_primaries->w_y;
  white_point.Y = 1.0;

  primaries.Red.x = bt2020_primaries->r_x;
  primaries.Red.y = bt2020_primaries->r_y;
  primaries.Red.Y = 1.0;
  primaries.Green.x = bt2020_primaries->g_x;
  primaries.Green.y = bt2020_primaries->g_y;
  primaries.Green.Y = 1.0;
  primaries.Blue.x = bt2020_primaries->b_x;
  primaries.Blue.y = bt2020_primaries->b_y;
  primaries.Blue.Y = 1.0;

  /* Linear transfer function (gamma 1.0) */
  curve = cmsBuildGamma (NULL, 1.0);

  profile = cmsCreateRGBProfile (&white_point, &primaries,
                                 (cmsToneCurve *[3]) { curve, curve, curve });

  cmsFreeToneCurve (curve);

  return g_steal_pointer (&profile);
}

#define TRC_SAMPLE_COUNT 256

static float *
sample_trc_curve (const cmsToneCurve *curve,
                  size_t              n_samples)
{
  g_autofree float *samples = NULL;

  samples = g_new (float, n_samples);

  for (size_t i = 0; i < n_samples; i++)
    {
      float v = (float) i / (float) (n_samples - 1);
      samples[i] = cmsEvalToneCurveFloat (curve, v);
    }

  return g_steal_pointer (&samples);
}

static float *
sample_inv_trc_curve (const cmsToneCurve *curve,
                      size_t              n_samples)
{
  cmsToneCurve *inv;
  g_autofree float *samples = NULL;

  inv = cmsReverseToneCurve (curve);
  g_assert_nonnull (inv);

  samples = sample_trc_curve (inv, n_samples);
  cmsFreeToneCurve (inv);

  return g_steal_pointer (&samples);
}

static gboolean
get_icc_to_linear_bt2020_matrix (cmsHPROFILE        *profile,
                                 graphene_matrix_t  *out_matrix)
{
  const cmsCIEXYZ *red, *green, *blue;
  ClutterColorimetry bt2020_colorimetry = {
    .type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE,
    .colorspace = CLUTTER_COLORSPACE_BT2020,
  };
  graphene_matrix_t colorant_to_xyz_d50;
  graphene_matrix_t adapt_d50_to_d65;
  graphene_matrix_t from_xyz_bt2020;
  graphene_matrix_t temp;
  graphene_vec3_t d50_xyz, d65_xyz;

  red = cmsReadTag (profile, cmsSigRedColorantTag);
  green = cmsReadTag (profile, cmsSigGreenColorantTag);
  blue = cmsReadTag (profile, cmsSigBlueColorantTag);

  if (!red || !green || !blue)
    return FALSE;

  graphene_matrix_init_from_float (
    &colorant_to_xyz_d50,
    (float [16]) {
      (float) red->X,   (float) red->Y,   (float) red->Z,   0.0f,
      (float) green->X, (float) green->Y, (float) green->Z, 0.0f,
      (float) blue->X,  (float) blue->Y,  (float) blue->Z,  0.0f,
      0.0f,             0.0f,             0.0f,             1.0f,
    });

  graphene_vec3_init (&d50_xyz, CLUTTER_D50_X, CLUTTER_D50_Y, CLUTTER_D50_Z);
  graphene_vec3_init (&d65_xyz, CLUTTER_D65_X, CLUTTER_D65_Y, CLUTTER_D65_Z);
  compute_chromatic_adaptation (&d50_xyz, &d65_xyz, &adapt_d50_to_d65);

  clutter_colorimetry_from_XYZ (&bt2020_colorimetry, &from_xyz_bt2020);

  graphene_matrix_multiply (&colorant_to_xyz_d50, &adapt_d50_to_d65, &temp);
  graphene_matrix_multiply (&temp, &from_xyz_bt2020, out_matrix);

  return TRUE;
}

static gboolean
get_icc_from_linear_bt2020_matrix (cmsHPROFILE        *profile,
                                   graphene_matrix_t  *out_matrix)
{
  graphene_matrix_t forward;

  if (!get_icc_to_linear_bt2020_matrix (profile, &forward))
    return FALSE;

  if (!graphene_matrix_inverse (&forward, out_matrix))
    {
      g_warning ("Failed inverting ICC to BT.2020 matrix");
      return FALSE;
    }

  return TRUE;
}

static void
add_icc_trc_ops (ClutterColorPipeline *pipeline,
                 cmsHPROFILE          *profile,
                 gboolean              inverse)
{
  const cmsToneCurve *red_trc, *green_trc, *blue_trc;
  float *r_samples, *g_samples, *b_samples;

  red_trc = cmsReadTag (profile, cmsSigRedTRCTag);
  green_trc = cmsReadTag (profile, cmsSigGreenTRCTag);
  blue_trc = cmsReadTag (profile, cmsSigBlueTRCTag);

  if (cmsIsToneCurveLinear (red_trc) &&
      cmsIsToneCurveLinear (green_trc) &&
      cmsIsToneCurveLinear (blue_trc))
    return;

  if (inverse)
    {
      r_samples = sample_inv_trc_curve (red_trc, TRC_SAMPLE_COUNT);
      g_samples = sample_inv_trc_curve (green_trc, TRC_SAMPLE_COUNT);
      b_samples = sample_inv_trc_curve (blue_trc, TRC_SAMPLE_COUNT);
    }
  else
    {
      r_samples = sample_trc_curve (red_trc, TRC_SAMPLE_COUNT);
      g_samples = sample_trc_curve (green_trc, TRC_SAMPLE_COUNT);
      b_samples = sample_trc_curve (blue_trc, TRC_SAMPLE_COUNT);
    }

  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_curve_1d_new (TRC_SAMPLE_COUNT,
                                                                 r_samples,
                                                                 g_samples,
                                                                 b_samples,
                                                                 NULL));
}

static void
add_icc_matrix_shaper_to_linear_bt2020_ops (ClutterColorPipeline *pipeline,
                                            ClutterColorStateIcc *icc)
{
  cmsHPROFILE *profile;
  graphene_matrix_t matrix_stack;
  graphene_matrix_t *matrix;

  profile = clutter_color_state_icc_get_profile (icc);

  if (!clutter_color_state_icc_is_linearized (icc))
    add_icc_trc_ops (pipeline, profile, /* inverse = */ FALSE);

  if (!get_icc_to_linear_bt2020_matrix (profile, &matrix_stack))
    return;

  matrix = graphene_matrix_alloc ();
  graphene_matrix_init_from_matrix (matrix, &matrix_stack);
  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_matrix_4x4_new (matrix));
}

static void
add_icc_matrix_shaper_from_linear_bt2020_ops (ClutterColorPipeline *pipeline,
                                              ClutterColorStateIcc *icc)
{
  cmsHPROFILE *profile;
  graphene_matrix_t matrix_stack;
  graphene_matrix_t *matrix;

  profile = clutter_color_state_icc_get_profile (icc);

  if (!get_icc_from_linear_bt2020_matrix (profile, &matrix_stack))
    return;

  matrix = graphene_matrix_alloc ();
  graphene_matrix_init_from_matrix (matrix, &matrix_stack);
  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_matrix_4x4_new (matrix));

  if (!clutter_color_state_icc_is_linearized (icc))
    add_icc_trc_ops (pipeline, profile, /* inverse = */ TRUE);
}

static float
dot_product (float a[3],
             float b[3])
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/*
 * Estimate per-channel EOTF by projecting single-channel RGB ramps through
 * the profile's RGB→XYZ transform onto the max-luminance direction.
 *
 * Reference:
 * https://lists.freedesktop.org/archives/wayland-devel/2019-March/040171.html
 */
static void
estimate_eotf_curves (cmsHPROFILE   *icc_profile,
                      cmsToneCurve  *curves[3])
{
  int valid_intents[] = {
    INTENT_PERCEPTUAL,
    INTENT_RELATIVE_COLORIMETRIC,
    INTENT_SATURATION,
    INTENT_ABSOLUTE_COLORIMETRIC
  };
  size_t n_points = 1024;
  float step;
  float rgb[3] = { 0.0f, 0.0f, 0.0f };
  g_autofree float *values = NULL;
  g_autoptr (cmsHPROFILE) XYZ_profile = NULL;
  g_autoptr (cmsHTRANSFORM) transform = NULL;

  step = 1.0f / (n_points - 1);

  XYZ_profile = cmsCreateXYZProfile ();

  for (size_t i = 0; i < G_N_ELEMENTS (valid_intents); i++)
    {
      transform = cmsCreateTransform (icc_profile,
                                      TYPE_RGB_FLT,
                                      XYZ_profile,
                                      TYPE_XYZ_FLT,
                                      valid_intents[i],
                                      0);
      if (transform)
        break;
    }

  if (!transform)
    return;

  values = g_malloc (n_points * sizeof (float));

  for (size_t ch = 0; ch < 3; ch++)
    {
      float max_XYZ[3];
      float squared_max_XYZ_norm;

      rgb[ch] = 1.0f;
      cmsDoTransform (transform, rgb, max_XYZ, 1);
      squared_max_XYZ_norm = dot_product (max_XYZ, max_XYZ);

      for (size_t i = 0; i < n_points; i++)
        {
          float xyz[3];

          rgb[ch] = (float) i * step;
          cmsDoTransform (transform, rgb, xyz, 1);
          values[i] = dot_product (xyz, max_XYZ) / squared_max_XYZ_norm;
        }

      rgb[ch] = 0.0f;

      curves[ch] = cmsBuildTabulatedToneCurveFloat (NULL, n_points, values);

      if (!cmsIsToneCurveMonotonic (curves[ch]))
        {
          g_warning ("Estimated curve is not monotonic, something is "
                     "probably wrong");
        }
    }
}

static cmsHPROFILE *
create_inv_eotf_device_link (cmsHPROFILE *icc_profile)
{
  g_auto (CmsToneCurveTriple) eotfs = { 0 };
  g_auto (CmsToneCurveTriple) inv_eotfs = { 0 };
  g_autoptr (cmsHPROFILE) inv_eotf_profile = NULL;

  if (cmsIsMatrixShaper (icc_profile))
    {
      eotfs[0] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigRedTRCTag));
      eotfs[1] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigGreenTRCTag));
      eotfs[2] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigBlueTRCTag));
    }
  else
    {
      estimate_eotf_curves (icc_profile, eotfs);
    }

  if (!eotfs[0] || !eotfs[1] || !eotfs[2])
    return NULL;

  inv_eotfs[0] = cmsReverseToneCurve (eotfs[0]);
  inv_eotfs[1] = cmsReverseToneCurve (eotfs[1]);
  inv_eotfs[2] = cmsReverseToneCurve (eotfs[2]);

  if (!inv_eotfs[0] || !inv_eotfs[1] || !inv_eotfs[2])
    return NULL;

  inv_eotf_profile = cmsCreateLinearizationDeviceLink (cmsSigRgbData,
                                                       inv_eotfs);

  return g_steal_pointer (&inv_eotf_profile);
}

static cmsHPROFILE *
create_eotf_device_link (cmsHPROFILE *icc_profile)
{
  g_auto (CmsToneCurveTriple) eotfs = { 0 };
  g_autoptr (cmsHPROFILE) eotf_profile = NULL;

  if (cmsIsMatrixShaper (icc_profile))
    {
      eotfs[0] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigRedTRCTag));
      eotfs[1] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigGreenTRCTag));
      eotfs[2] = cmsDupToneCurve (cmsReadTag (icc_profile, cmsSigBlueTRCTag));
    }
  else
    {
      estimate_eotf_curves (icc_profile, eotfs);
    }

  if (!eotfs[0] || !eotfs[1] || !eotfs[2])
    return NULL;

  eotf_profile = cmsCreateLinearizationDeviceLink (cmsSigRgbData, eotfs);

  return g_steal_pointer (&eotf_profile);
}

static void
add_icc_to_linear_bt2020_op (ClutterColorPipeline *pipeline,
                             ClutterColorStateIcc *icc)
{
  cmsHPROFILE *profile;
  gboolean is_linearized;
  int lut_size = LUT_3D_SIZE;
  int n_samples;
  g_autofree float *lut_data = NULL;
  g_autoptr (cmsHPROFILE) linear_bt2020_profile = NULL;
  g_autoptr (cmsHPROFILE) inv_eotf_profile = NULL;
  g_autoptr (cmsHTRANSFORM) transform = NULL;

  profile = clutter_color_state_icc_get_profile (icc);
  is_linearized = clutter_color_state_icc_is_linearized (icc);
  n_samples = lut_size * lut_size * lut_size;

  if (cmsIsMatrixShaper (profile))
    {
      add_icc_matrix_shaper_to_linear_bt2020_ops (pipeline, icc);
      return;
    }

  if (is_linearized)
    {
      inv_eotf_profile = create_inv_eotf_device_link (profile);
      if (!inv_eotf_profile)
        {
          g_warning ("Failed to create inverse EOTF for linearized ICC");
          is_linearized = FALSE;
        }
    }

  lut_data = g_malloc (n_samples * 3 * sizeof (float));
  sample_3d_lut_input (lut_data, lut_size);

  linear_bt2020_profile = create_linear_bt2020_profile ();

  if (is_linearized)
    {
      cmsHPROFILE profiles[3] = {
        inv_eotf_profile, profile, linear_bt2020_profile
      };
      transform = cmsCreateMultiprofileTransform (profiles, 3,
                                                  TYPE_RGB_FLT,
                                                  TYPE_RGB_FLT,
                                                  INTENT_RELATIVE_COLORIMETRIC,
                                                  0);
    }
  else
    {
      transform = cmsCreateTransform (profile,
                                      TYPE_RGB_FLT,
                                      linear_bt2020_profile, TYPE_RGB_FLT,
                                      INTENT_RELATIVE_COLORIMETRIC,
                                      0);
    }
  g_assert_nonnull (transform);

  do_lcms_transform (transform, lut_data, n_samples);

  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_3d_lut_new (lut_size, lut_data));
  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_clamp_unit_new ());
}

static void
add_icc_from_linear_bt2020_op (ClutterColorPipeline *pipeline,
                               ClutterColorStateIcc *icc)
{
  cmsHPROFILE *profile;
  gboolean is_linearized;
  int lut_size = LUT_3D_SIZE;
  int n_samples;
  g_autofree float *lut_data = NULL;
  g_autoptr (cmsHPROFILE) linear_bt2020_profile = NULL;
  g_autoptr (cmsHPROFILE) eotf_profile = NULL;
  g_autoptr (cmsHTRANSFORM) transform = NULL;

  profile = clutter_color_state_icc_get_profile (icc);
  is_linearized = clutter_color_state_icc_is_linearized (icc);
  n_samples = lut_size * lut_size * lut_size;

  if (cmsIsMatrixShaper (profile))
    {
      add_icc_matrix_shaper_from_linear_bt2020_ops (pipeline, icc);
      return;
    }

  if (is_linearized)
    {
      eotf_profile = create_eotf_device_link (profile);
      if (!eotf_profile)
        {
          g_warning ("Failed to create EOTF for linearized ICC");
          is_linearized = FALSE;
        }
    }

  lut_data = g_malloc (n_samples * 3 * sizeof (float));
  sample_3d_lut_input (lut_data, lut_size);

  linear_bt2020_profile = create_linear_bt2020_profile ();

  if (is_linearized)
    {
      cmsHPROFILE profiles[3] = {
        linear_bt2020_profile, profile, eotf_profile
      };
      transform = cmsCreateMultiprofileTransform (profiles, 3,
                                                  TYPE_RGB_FLT,
                                                  TYPE_RGB_FLT,
                                                  INTENT_RELATIVE_COLORIMETRIC,
                                                  0);
    }
  else
    {
      transform = cmsCreateTransform (linear_bt2020_profile, TYPE_RGB_FLT,
                                      profile,
                                      TYPE_RGB_FLT,
                                      INTENT_RELATIVE_COLORIMETRIC,
                                      0);
    }
  g_assert_nonnull (transform);

  do_lcms_transform (transform, lut_data, n_samples);

  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_3d_lut_new (lut_size, lut_data));
  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_clamp_unit_new ());
}

static void
add_icc_to_icc_op (ClutterColorPipeline *pipeline,
                   ClutterColorStateIcc *src_icc,
                   ClutterColorStateIcc *target_icc)
{
  cmsHPROFILE *src_profile;
  cmsHPROFILE *target_profile;
  gboolean src_linearized, target_linearized;
  int lut_size = LUT_3D_SIZE;
  int n_samples;
  g_autofree float *lut_data = NULL;
  g_autoptr (cmsHPROFILE) src_inv_eotf = NULL;
  g_autoptr (cmsHPROFILE) target_eotf = NULL;
  g_autoptr (cmsHTRANSFORM) transform = NULL;
  cmsHPROFILE profiles[4];
  int n_profiles = 0;

  src_profile = clutter_color_state_icc_get_profile (src_icc);
  target_profile = clutter_color_state_icc_get_profile (target_icc);
  src_linearized = clutter_color_state_icc_is_linearized (src_icc);
  target_linearized = clutter_color_state_icc_is_linearized (target_icc);
  n_samples = lut_size * lut_size * lut_size;

  if (cmsIsMatrixShaper (src_profile) && cmsIsMatrixShaper (target_profile))
    {
      add_icc_matrix_shaper_to_linear_bt2020_ops (pipeline, src_icc);
      add_icc_matrix_shaper_from_linear_bt2020_ops (pipeline, target_icc);
      return;
    }

  if (src_linearized)
    {
      src_inv_eotf = create_inv_eotf_device_link (src_profile);
      if (!src_inv_eotf)
        {
          g_warning ("Failed to create inverse EOTF for linearized src ICC");
          src_linearized = FALSE;
        }
    }

  if (target_linearized)
    {
      target_eotf = create_eotf_device_link (target_profile);
      if (!target_eotf)
        {
          g_warning ("Failed to create EOTF for linearized target ICC");
          target_linearized = FALSE;
        }
    }

  if (src_linearized)
    profiles[n_profiles++] = src_inv_eotf;
  profiles[n_profiles++] = src_profile;
  profiles[n_profiles++] = target_profile;
  if (target_linearized)
    profiles[n_profiles++] = target_eotf;

  lut_data = g_malloc (n_samples * 3 * sizeof (float));
  sample_3d_lut_input (lut_data, lut_size);

  transform = cmsCreateMultiprofileTransform (profiles, n_profiles,
                                              TYPE_RGB_FLT,
                                              TYPE_RGB_FLT,
                                              INTENT_RELATIVE_COLORIMETRIC,
                                              0);
  g_assert_nonnull (transform);

  do_lcms_transform (transform, lut_data, n_samples);

  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_3d_lut_new (lut_size, lut_data));
  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_clamp_unit_new ());
}


static void
add_to_linear_bt2020_ops (ClutterColorPipeline *pipeline,
                          ClutterColorState    *color_state)
{
  if (CLUTTER_IS_COLOR_STATE_PARAMS (color_state))
    {
      add_params_to_linear_bt2020_ops (pipeline,
                                       CLUTTER_COLOR_STATE_PARAMS (color_state));
    }
  else if (CLUTTER_IS_COLOR_STATE_ICC (color_state))
    {
      add_icc_to_linear_bt2020_op (pipeline,
                                   CLUTTER_COLOR_STATE_ICC (color_state));
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
add_from_linear_bt2020_ops (ClutterColorPipeline *pipeline,
                            ClutterColorState    *color_state)
{
  if (CLUTTER_IS_COLOR_STATE_PARAMS (color_state))
    {
      add_params_from_linear_bt2020_ops (pipeline,
                                         CLUTTER_COLOR_STATE_PARAMS (color_state));
    }
  else if (CLUTTER_IS_COLOR_STATE_ICC (color_state))
    {
      add_icc_from_linear_bt2020_op (pipeline,
                                     CLUTTER_COLOR_STATE_ICC (color_state));
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
add_luminance_mapping_op (ClutterColorPipeline *pipeline,
                          ClutterColorState    *source_color_state,
                          ClutterColorState    *target_color_state)
{
  const ClutterLuminance *source_lum;
  const ClutterLuminance *target_lum;
  float lum_factor;

  source_lum = clutter_color_state_get_luminance (source_color_state);
  target_lum = clutter_color_state_get_luminance (target_color_state);

  if (source_lum->max == target_lum->max && source_lum->ref == target_lum->ref)
    return;

  lum_factor = (target_lum->ref / source_lum->ref) *
               (source_lum->max / target_lum->max);
  clutter_color_pipeline_take_op (pipeline,
                                  clutter_color_op_multiply_new (lum_factor));
}

static void
build_transform_pipeline (ClutterColorPipeline            *pipeline,
                          ClutterColorState               *source_color_state,
                          ClutterColorState               *target_color_state,
                          ClutterColorStateTransformFlags  flags)
{
  if (!(flags & CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE))
    clutter_color_pipeline_take_op (pipeline, clutter_color_op_unpremultiply_new ());

  if (CLUTTER_IS_COLOR_STATE_ICC (source_color_state) &&
      CLUTTER_IS_COLOR_STATE_ICC (target_color_state))
    {
      add_icc_to_icc_op (pipeline,
                         CLUTTER_COLOR_STATE_ICC (source_color_state),
                         CLUTTER_COLOR_STATE_ICC (target_color_state));
    }
  else
    {
      /* source → linear BT.2020 D65 → target */
      add_to_linear_bt2020_ops (pipeline, source_color_state);
      add_luminance_mapping_op (pipeline, source_color_state, target_color_state);
      add_from_linear_bt2020_ops (pipeline, target_color_state);
    }

  if (!(flags & CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE))
    clutter_color_pipeline_take_op (pipeline, clutter_color_op_premultiply_new ());
}

ClutterColorTransform *
clutter_color_transform_new (ClutterColorState               *source_color_state,
                             ClutterColorState               *target_color_state,
                             ClutterColorStateTransformFlags  flags)
{
  g_autoptr (ClutterColorTransform) transform = NULL;
  g_autoptr (ClutterColorPipeline) pipeline = NULL;

  transform = g_object_new (CLUTTER_TYPE_COLOR_TRANSFORM, NULL);
  pipeline = g_object_new (CLUTTER_TYPE_COLOR_PIPELINE, NULL);

  if (!clutter_color_state_equals (source_color_state, target_color_state))
    build_transform_pipeline (pipeline, source_color_state, target_color_state, flags);

  clutter_color_pipeline_simplify (pipeline);
  clutter_color_pipeline_combine (pipeline);
  transform->pipeline = g_steal_pointer (&pipeline);

  return g_steal_pointer (&transform);
}

ClutterColorPipeline *
clutter_color_transform_get_pipeline (ClutterColorTransform *transform)
{
  return transform->pipeline;
}

typedef struct _ColorTransformCacheKey
{
  ClutterColorState *source;
  ClutterColorState *target;
  uint32_t flags;
} ColorTransformCacheKey;

typedef struct _ColorTransformCache
{
  GHashTable *transforms; /* ColorTransformCacheKey * -> ClutterColorTransform * */
  GHashTable *tracked_states; /* ClutterColorState * (set) */
  ClutterContext *context;
} ColorTransformCache;

static void on_color_state_destroyed (ClutterColorState *color_state,
                                      ClutterContext    *context);

static unsigned int
color_transform_cache_key_hash (gconstpointer key)
{
  const ColorTransformCacheKey *k = key;
  unsigned int hash;

  hash = clutter_color_state_hash (k->source);
  hash = hash * 40499 + clutter_color_state_hash (k->target);
  hash = hash * 31 + k->flags;

  return hash;
}

static gboolean
color_transform_cache_key_equal (gconstpointer a,
                                 gconstpointer b)
{
  const ColorTransformCacheKey *ka = a;
  const ColorTransformCacheKey *kb = b;

  return clutter_color_state_equals (ka->source, kb->source) &&
         clutter_color_state_equals (ka->target, kb->target) &&
         ka->flags == kb->flags;
}

static void
color_transform_cache_free (gpointer data)
{
  ColorTransformCache *cache = data;
  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, cache->tracked_states);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      g_signal_handlers_disconnect_by_func (key,
                                            on_color_state_destroyed,
                                            cache->context);
    }

  g_hash_table_unref (cache->transforms);
  g_hash_table_unref (cache->tracked_states);
  g_free (cache);
}

static ColorTransformCache *
get_color_transform_cache (ClutterContext *context)
{
  ColorTransformCache *cache;

  cache = g_object_get_data (G_OBJECT (context),
                             "color-transform-cache");
  if (!cache)
    {
      cache = g_new0 (ColorTransformCache, 1);
      cache->context = context;
      cache->transforms =
        g_hash_table_new_full (color_transform_cache_key_hash,
                               color_transform_cache_key_equal,
                               g_free,
                               g_object_unref);
      cache->tracked_states =
        g_hash_table_new (g_direct_hash, g_direct_equal);
      g_object_set_data_full (G_OBJECT (context),
                              "color-transform-cache",
                              cache,
                              color_transform_cache_free);
    }

  return cache;
}

static void
on_color_state_destroyed (ClutterColorState *color_state,
                          ClutterContext    *context)
{
  ColorTransformCache *cache;
  GHashTableIter iter;
  gpointer key;

  cache = g_object_get_data (G_OBJECT (context),
                             "color-transform-cache");
  if (!cache)
    return;

  g_hash_table_iter_init (&iter, cache->transforms);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    {
      const ColorTransformCacheKey *k = key;

      if (k->source == color_state || k->target == color_state)
        g_hash_table_iter_remove (&iter);
    }

  g_hash_table_remove (cache->tracked_states, color_state);
}

static void
ensure_color_state_tracking (ColorTransformCache *cache,
                             ClutterColorState   *color_state)
{
  if (g_hash_table_contains (cache->tracked_states, color_state))
    return;

  g_signal_connect (color_state, "destroyed",
                    G_CALLBACK (on_color_state_destroyed),
                    cache->context);

  g_hash_table_add (cache->tracked_states, color_state);
}

ClutterColorTransform *
clutter_color_transform_from_color_states (ClutterContext                  *context,
                                           ClutterColorState               *source_color_state,
                                           ClutterColorState               *target_color_state,
                                           ClutterColorStateTransformFlags  flags)
{
  ColorTransformCache *cache;
  ColorTransformCacheKey lookup_key = {
    .source = source_color_state,
    .target = target_color_state,
    .flags = flags,
  };
  ClutterColorTransform *transform;

  cache = get_color_transform_cache (context);

  transform = g_hash_table_lookup (cache->transforms, &lookup_key);
  if (transform)
    return transform;

  {
    g_autoptr (ClutterColorTransform) owned_transform = NULL;

    owned_transform = transform = clutter_color_transform_new (source_color_state,
                                                               target_color_state,
                                                               flags);

    ensure_color_state_tracking (cache, source_color_state);
    ensure_color_state_tracking (cache, target_color_state);

    g_hash_table_insert (cache->transforms,
                         g_memdup2 (&lookup_key, sizeof (ColorTransformCacheKey)),
                         g_steal_pointer (&owned_transform));
  }

  return transform;
}
