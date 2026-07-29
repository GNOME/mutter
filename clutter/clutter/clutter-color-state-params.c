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

#include "clutter/clutter-color-state.h"
#include "clutter/clutter-color-utils.h"
#include "clutter/clutter-context.h"

#define UNIFORM_NAME_GAMMA_EXP "gamma_exp"
#define UNIFORM_NAME_INV_GAMMA_EXP "inv_gamma_exp"
#define UNIFORM_NAME_COLOR_SPACE_MAPPING "color_transformation_matrix"
#define UNIFORM_NAME_TO_LMS "to_lms"
#define UNIFORM_NAME_FROM_LMS "from_lms"
#define UNIFORM_NAME_SRC_MAX_LUM "src_max_lum"
#define UNIFORM_NAME_DST_MAX_LUM "dst_max_lum"
#define UNIFORM_NAME_SRC_MASTERING_MAX_LUM "src_mastering_max_lum"
#define UNIFORM_NAME_DST_MASTERING_MAX_LUM "dst_mastering_max_lum"
#define UNIFORM_NAME_SRC_REF_LUM "src_ref_lum"
#define UNIFORM_NAME_TONEMAPPING_REF_LUM "tone_mapping_ref_lum"
#define UNIFORM_NAME_LINEAR_TONEMAPPING "linear_mapping"
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

static void
clutter_color_state_params_finalize (GObject *object)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (object);

  clutter_colorimetry_clear (&color_state_params->colorimetry);

  G_OBJECT_CLASS (clutter_color_state_params_parent_class)->finalize (object);
}


static const ClutterLuminance *
clutter_color_state_params_get_luminance_vfunc (ClutterColorState *color_state)
{
  return clutter_color_state_params_get_luminance (
    CLUTTER_COLOR_STATE_PARAMS (color_state));
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

  if (!clutter_colorimetry_equal (&color_state_params->colorimetry,
                                  &other_color_state_params->colorimetry) ||
      !clutter_eotf_equal (&color_state_params->eotf,
                           &other_color_state_params->eotf))
    return FALSE;

  lum = clutter_color_state_params_get_luminance (color_state_params);
  target_lum =
    clutter_color_state_params_get_luminance (other_color_state_params);

  return clutter_luminance_equal (lum, target_lum);
}

static char *
clutter_color_state_params_to_string (ClutterColorState *color_state)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  g_autofree char *primaries_name = NULL;
  const char *transfer_function_name;
  const ClutterLuminance *lum;
  uint64_t id;

  id = clutter_color_state_get_id (color_state);
  primaries_name = clutter_colorimetry_to_string (color_state_params->colorimetry);
  transfer_function_name = clutter_eotf_to_string (color_state_params->eotf);
  lum = clutter_color_state_params_get_luminance (color_state_params);

  return g_strdup_printf ("ClutterColorState %" G_GUINT64_FORMAT " "
                          "(primaries: %s, transfer function: %s, "
                          "min lum: %f, max lum: %f, ref lum: %f, "
                          "mastering max lum: %f)",
                          id,
                          primaries_name,
                          transfer_function_name,
                          lum->min,
                          lum->max,
                          lum->ref,
                          lum->mastering_max);


}

static ClutterEncodingRequiredFormat
clutter_color_state_params_required_format (ClutterColorState *color_state)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  const ClutterLuminance *luminance;

  luminance = clutter_color_state_params_get_luminance (color_state_params);
  if (luminance->mastering_max > luminance->max)
    return CLUTTER_ENCODING_REQUIRED_FORMAT_FP16;

  switch (color_state_params->eotf.type)
    {
    case CLUTTER_EOTF_TYPE_NAMED:
      switch (color_state_params->eotf.tf_name)
        {
        case CLUTTER_TRANSFER_FUNCTION_SRGB_PIECEWISE:
        case CLUTTER_TRANSFER_FUNCTION_BT1886:
        case CLUTTER_TRANSFER_FUNCTION_GAMMA22:
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
 * Currently all content is blended with gamma transfer characteristics.
 */
static ClutterColorState *
clutter_color_state_params_get_blending (ClutterColorState *color_state,
                                         gboolean           force_linear)
{
  ClutterColorStateParams *color_state_params =
    CLUTTER_COLOR_STATE_PARAMS (color_state);
  g_autoptr (ClutterContext) context = NULL;
  ClutterColorimetry blending_colorimetry;
  ClutterEOTF blending_eotf;
  ClutterLuminance luminance, blending_luminance;

  blending_colorimetry = color_state_params->colorimetry;
  blending_eotf = color_state_params->eotf;

  if (force_linear)
    {
      blending_eotf.type = CLUTTER_EOTF_TYPE_NAMED;
      blending_eotf.tf_name = CLUTTER_TRANSFER_FUNCTION_LINEAR;
    }
  else if (blending_eotf.type == CLUTTER_EOTF_TYPE_NAMED &&
           blending_eotf.tf_name == CLUTTER_TRANSFER_FUNCTION_PQ)
    {
      blending_colorimetry.type = CLUTTER_COLORIMETRY_TYPE_COLORSPACE;
      blending_colorimetry.colorspace = CLUTTER_COLORSPACE_SRGB;
      blending_eotf.type = CLUTTER_EOTF_TYPE_NAMED;
      blending_eotf.tf_name = CLUTTER_TRANSFER_FUNCTION_GAMMA22;
    }

  if (clutter_eotf_equal (&blending_eotf, &color_state_params->eotf) &&
      clutter_colorimetry_equal (&blending_colorimetry,
                                 &color_state_params->colorimetry))
    return g_object_ref (color_state);

  luminance = *clutter_color_state_params_get_luminance (color_state_params);
  if (force_linear)
    {
      blending_luminance = luminance;
    }
  else
    {
      blending_luminance = *clutter_eotf_get_default_luminance (blending_eotf);
      blending_luminance.type = CLUTTER_LUMINANCE_TYPE_EXPLICIT;
      blending_luminance.mastering_max = blending_luminance.ref *
                                         luminance.mastering_max / luminance.ref;
    }

  g_object_get (G_OBJECT (color_state), "context", &context, NULL);

  return clutter_color_state_params_new_from_primitives (context,
                                                         blending_colorimetry,
                                                         blending_eotf,
                                                         blending_luminance);
}

static guint
clutter_color_state_params_hash (ClutterColorState *color_state)
{
  ClutterColorStateParams *params = CLUTTER_COLOR_STATE_PARAMS (color_state);
  guint hash;

  hash = params->colorimetry.type;
  if (params->colorimetry.type == CLUTTER_COLORIMETRY_TYPE_COLORSPACE)
    hash = hash * 31 + params->colorimetry.colorspace;

  hash = hash * 31 + params->eotf.type;
  if (params->eotf.type == CLUTTER_EOTF_TYPE_NAMED)
    hash = hash * 31 + params->eotf.tf_name;

  return hash;
}

static void
clutter_color_state_params_class_init (ClutterColorStateParamsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterColorStateClass *color_state_class = CLUTTER_COLOR_STATE_CLASS (klass);

  object_class->finalize = clutter_color_state_params_finalize;

  color_state_class->equals = clutter_color_state_params_equals;
  color_state_class->to_string = clutter_color_state_params_to_string;
  color_state_class->required_format = clutter_color_state_params_required_format;
  color_state_class->get_blending = clutter_color_state_params_get_blending;
  color_state_class->hash = clutter_color_state_params_hash;
  color_state_class->get_luminance = clutter_color_state_params_get_luminance_vfunc;
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
                                              -1.0f);
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
                                     float                    mastering_max_lum)
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

      if (mastering_max_lum > 0.0f)
        color_state_params->luminance.mastering_max = mastering_max_lum;
      else
        color_state_params->luminance.mastering_max =
          color_state_params->luminance.max;
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
  ClutterPrimaries *primaries = NULL;
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
      if (!clutter_primaries_equal (colorimetry.primaries,
                                    clutter_colorspace_to_primaries (CLUTTER_COLORSPACE_SRGB)))
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
      tf_name = CLUTTER_TRANSFER_FUNCTION_SRGB_PIECEWISE;
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
                                              luminance.mastering_max);
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
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_BT1886;
      return TRUE;
    case CLUTTER_CICP_TRANSFER_GAMMA22:
      eotf->type = CLUTTER_EOTF_TYPE_NAMED;
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_GAMMA22;
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
      eotf->tf_name = CLUTTER_TRANSFER_FUNCTION_SRGB_PIECEWISE;
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

  lum = *clutter_eotf_get_default_luminance (eotf);

  return clutter_color_state_params_new_from_primitives (context,
                                                         colorimetry,
                                                         eotf,
                                                         lum);
}
