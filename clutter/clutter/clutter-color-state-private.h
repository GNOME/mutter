/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024  Red Hat
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

#pragma once

#include "clutter/clutter-color-state.h"

typedef struct _ClutterColorTransformKey
{
  /* 3 bits to define TransferFunction enums
   * + 1 bit to define Gamma TF */
  guint source_eotf_bits : 4;
  guint target_eotf_bits : 4;
  /* When there is a luminance mapping snippet */
  guint luminance_bit    : 1;
  /* When there is a color trans snippet */
  guint color_trans_bit  : 1;
  /* When there is a tone mapping snippet */
  guint tone_mapping_bit  : 1;
  /* When there is a 3D LUT snippet */
  guint lut_3d           : 1;
  /* Alpha channel is always 1.0 */
  guint opaque_bit       : 1;
} ClutterColorTransformKey;

typedef struct _ClutterColorOpSnippet
{
  const char *source;
  const char *name;
} ClutterColorOpSnippet;

void clutter_color_transform_key_init (ClutterColorTransformKey        *key,
                                       ClutterColorState               *color_state,
                                       ClutterColorState               *target_color_state,
                                       ClutterColorStateTransformFlags  flags);

guint clutter_color_transform_key_hash (gconstpointer data);

gboolean clutter_color_transform_key_equal (gconstpointer data1,
                                            gconstpointer data2);

void clutter_color_op_snippet_append_global (const ClutterColorOpSnippet *color_snippet,
                                             GString                     *snippet_global);

void clutter_color_op_snippet_append_source (const ClutterColorOpSnippet *color_snippet,
                                             GString                     *snippet_source,
                                             const char                  *snippet_color_var);

void clutter_color_state_init_3d_lut_transform_key (ClutterColorState               *color_state,
                                                    ClutterColorState               *target_color_state,
                                                    ClutterColorStateTransformFlags  flags,
                                                    ClutterColorTransformKey        *key);

void clutter_color_state_append_3d_lut_transform_snippet (ClutterColorState *color_state,
                                                          ClutterColorState *target_color_state,
                                                          GString           *snippet_globals,
                                                          GString           *snippet_source,
                                                          const char        *snippet_color_var);

void clutter_color_state_update_3d_lut_uniforms (ClutterColorState *color_state,
                                                 ClutterColorState *target_color_state,
                                                 CoglPipeline      *pipeline);

ClutterContext * clutter_color_state_get_context (ClutterColorState *color_state);
