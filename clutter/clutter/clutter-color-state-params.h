/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
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

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-color-state.h"

G_BEGIN_DECLS

typedef enum
{
  CLUTTER_COLORSPACE_SRGB,
  CLUTTER_COLORSPACE_BT2020,
  CLUTTER_COLORSPACE_NTSC,
  CLUTTER_COLORSPACE_PAL,
  CLUTTER_COLORSPACE_P3,
} ClutterColorspace;

typedef enum
{
  CLUTTER_TRANSFER_FUNCTION_SRGB,
  CLUTTER_TRANSFER_FUNCTION_PQ,
  CLUTTER_TRANSFER_FUNCTION_BT709,
  CLUTTER_TRANSFER_FUNCTION_LINEAR,
} ClutterTransferFunction;

typedef enum
{
  CLUTTER_COLORIMETRY_TYPE_COLORSPACE,
  CLUTTER_COLORIMETRY_TYPE_PRIMARIES,
} ClutterColorimetryType;

typedef enum
{
  CLUTTER_EOTF_TYPE_NAMED,
  CLUTTER_EOTF_TYPE_GAMMA,
} ClutterEOTFType;

typedef enum
{
  CLUTTER_LUMINANCE_TYPE_DERIVED,
  CLUTTER_LUMINANCE_TYPE_EXPLICIT,
} ClutterLuminanceType;

typedef struct _ClutterPrimaries
{
  float r_x, r_y;
  float g_x, g_y;
  float b_x, b_y;
  float w_x, w_y;
} ClutterPrimaries;

typedef struct _ClutterColorimetry
{
  ClutterColorimetryType type : 1;
  union
  {
    ClutterColorspace colorspace;
    ClutterPrimaries *primaries;
  };
} ClutterColorimetry;

typedef struct _ClutterEOTF
{
  ClutterEOTFType type : 1;
  union
  {
    ClutterTransferFunction tf_name;
    float gamma_exp;
  };
} ClutterEOTF;

typedef struct _ClutterLuminance
{
  ClutterLuminanceType type : 1;
  guint ref_is_1_0 : 1;
  float min;
  float max;
  float ref;
} ClutterLuminance;

typedef enum
{
  CLUTTER_CICP_PRIMARIES_SRGB = 1,
  CLUTTER_CICP_PRIMARIES_PAL = 5,
  CLUTTER_CICP_PRIMARIES_NTSC = 6,
  CLUTTER_CICP_PRIMARIES_NTSC_2 = 7,
  CLUTTER_CICP_PRIMARIES_BT2020 = 9,
  CLUTTER_CICP_PRIMARIES_P3 = 12,
} ClutterCicpPrimaries;

typedef enum
{
  CLUTTER_CICP_TRANSFER_BT709 = 1,
  CLUTTER_CICP_TRANSFER_GAMMA22 = 4,
  CLUTTER_CICP_TRANSFER_GAMMA28 = 5,
  CLUTTER_CICP_TRANSFER_BT601 = 6,
  CLUTTER_CICP_TRANSFER_LINEAR = 8,
  CLUTTER_CICP_TRANSFER_SRGB = 13,
  CLUTTER_CICP_TRANSFER_BT2020 = 14,
  CLUTTER_CICP_TRANSFER_BT2020_2 = 15,
  CLUTTER_CICP_TRANSFER_PQ = 16,
  CLUTTER_CICP_TRANSFER_HLG = 18,
} ClutterCicpTransfer;

typedef struct
{
  ClutterCicpPrimaries primaries;
  ClutterCicpTransfer transfer;
  uint8_t matrix_coefficients;
  uint8_t video_full_range_flag;
} ClutterCicp;

#define CLUTTER_TYPE_COLOR_STATE_PARAMS (clutter_color_state_params_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorStateParams, clutter_color_state_params,
                      CLUTTER, COLOR_STATE_PARAMS,
                      ClutterColorState)

CLUTTER_EXPORT
ClutterColorState * clutter_color_state_params_new (ClutterContext          *context,
                                                    ClutterColorspace        colorspace,
                                                    ClutterTransferFunction  transfer_function);

CLUTTER_EXPORT
ClutterColorState * clutter_color_state_params_new_full (ClutterContext          *context,
                                                         ClutterColorspace        colorspace,
                                                         ClutterTransferFunction  transfer_function,
                                                         ClutterPrimaries        *primaries,
                                                         float                    gamma_exp,
                                                         float                    min_lum,
                                                         float                    max_lum,
                                                         float                    ref_lum,
                                                         gboolean                 ref_is_1_0);

CLUTTER_EXPORT
ClutterColorState * clutter_color_state_params_new_from_primitives (ClutterContext     *context,
                                                                    ClutterColorimetry  colorimetry,
                                                                    ClutterEOTF         eotf,
                                                                    ClutterLuminance    luminance);

CLUTTER_EXPORT
ClutterColorState * clutter_color_state_params_new_from_cicp (ClutterContext     *context,
                                                              const ClutterCicp  *cicp,
                                                              GError            **error);

CLUTTER_EXPORT
const ClutterColorimetry * clutter_color_state_params_get_colorimetry (ClutterColorStateParams *color_state_params);

CLUTTER_EXPORT
const ClutterEOTF * clutter_color_state_params_get_eotf (ClutterColorStateParams *color_state_params);

CLUTTER_EXPORT
const ClutterLuminance * clutter_color_state_params_get_luminance (ClutterColorStateParams *color_state_params);

CLUTTER_EXPORT
const ClutterLuminance * clutter_eotf_get_default_luminance (ClutterEOTF eotf);

CLUTTER_EXPORT
const ClutterPrimaries * clutter_colorspace_to_primaries (ClutterColorspace colorspace);

CLUTTER_EXPORT
void clutter_primaries_ensure_normalized_range (ClutterPrimaries *primaries);

CLUTTER_EXPORT
void clutter_color_state_params_do_tone_mapping (ClutterColorState *color_state,
                                                 ClutterColorState *other_color_state,
                                                 float             *data,
                                                 int                n_samples);

G_END_DECLS
