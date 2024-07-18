/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2022  Intel Corporation.
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
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_COLOR_STATE (clutter_color_state_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorState, clutter_color_state,
                      CLUTTER, COLOR_STATE,
                      GObject)

CLUTTER_EXPORT
ClutterColorState * clutter_color_state_new (ClutterContext          *context,
                                             ClutterColorspace        colorspace,
                                             ClutterTransferFunction  transfer_function);

CLUTTER_EXPORT
ClutterColorState * clutter_color_state_new_full (ClutterContext          *context,
                                                  ClutterColorspace        colorspace,
                                                  ClutterTransferFunction  transfer_function,
                                                  float                    min_lum,
                                                  float                    max_lum,
                                                  float                    ref_lum);

CLUTTER_EXPORT
char * clutter_color_state_to_string (ClutterColorState *color_state);

CLUTTER_EXPORT
unsigned int clutter_color_state_get_id (ClutterColorState *color_state);

CLUTTER_EXPORT
ClutterColorspace clutter_color_state_get_colorspace (ClutterColorState *color_state);

CLUTTER_EXPORT
ClutterTransferFunction clutter_color_state_get_transfer_function (ClutterColorState *color_state);

CLUTTER_EXPORT
void clutter_color_state_get_luminances (ClutterColorState *color_state,
                                         float             *min_lum_out,
                                         float             *max_lum_out,
                                         float             *ref_lum_out);

CLUTTER_EXPORT
void clutter_color_state_add_pipeline_transform (ClutterColorState *color_state,
                                                 ClutterColorState *target_color_state,
                                                 CoglPipeline      *pipeline);

CLUTTER_EXPORT
void clutter_color_state_update_uniforms (ClutterColorState *color_state,
                                          ClutterColorState *target_color_state,
                                          CoglPipeline      *pipeline);


CLUTTER_EXPORT
gboolean clutter_color_state_equals (ClutterColorState *color_state,
                                     ClutterColorState *other_color_state);

CLUTTER_EXPORT
ClutterEncodingRequiredFormat clutter_color_state_required_format (ClutterColorState *color_state);

CLUTTER_EXPORT
ClutterColorState * clutter_color_state_get_blending (ClutterColorState *color_state,
                                                      gboolean           force);

CLUTTER_EXPORT
void clutter_transfer_function_get_default_luminances (ClutterTransferFunction  transfer_function,
                                                       float                   *min_lum_out,
                                                       float                   *max_lum_out,
                                                       float                   *ref_lum_out);

G_END_DECLS
