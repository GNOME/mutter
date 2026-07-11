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
#include "clutter/clutter-color-utils.h"

G_BEGIN_DECLS

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
                                                         float                    mastering_max_lum);

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
void clutter_color_state_params_do_tone_mapping (ClutterColorState *color_state,
                                                 ClutterColorState *other_color_state,
                                                 float             *data,
                                                 int                n_samples);

G_END_DECLS
