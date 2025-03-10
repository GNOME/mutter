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
G_DECLARE_DERIVABLE_TYPE (ClutterColorState,
                          clutter_color_state,
                          CLUTTER, COLOR_STATE,
                          GObject)

struct _ClutterColorStateClass
{
  GObjectClass parent_class;

  void (* init_color_transform_key) (ClutterColorState        *color_state,
                                     ClutterColorState        *target_color_state,
                                     ClutterColorTransformKey *key);

  CoglSnippet * (* create_transform_snippet) (ClutterColorState *color_state,
                                              ClutterColorState *target_color_state);

  void (* update_uniforms) (ClutterColorState *color_state,
                            ClutterColorState *target_color_state,
                            CoglPipeline      *pipeline);

  void (* do_transform) (ClutterColorState *color_state,
                         ClutterColorState *target_color_state,
                         const float       *input,
                         float             *output,
                         int                n_samples);

  gboolean (* equals) (ClutterColorState *color_state,
                       ClutterColorState *other_color_state);

  char * (* to_string) (ClutterColorState *color_state);

  ClutterEncodingRequiredFormat (* required_format) (ClutterColorState *color_state);

  ClutterColorState * (* get_blending) (ClutterColorState *color_state,
                                        gboolean           force);
};

CLUTTER_EXPORT
char * clutter_color_state_to_string (ClutterColorState *color_state);

CLUTTER_EXPORT
unsigned int clutter_color_state_get_id (ClutterColorState *color_state);

CLUTTER_EXPORT
void clutter_color_state_add_pipeline_transform (ClutterColorState *color_state,
                                                 ClutterColorState *target_color_state,
                                                 CoglPipeline      *pipeline);

CLUTTER_EXPORT
void clutter_color_state_update_uniforms (ClutterColorState *color_state,
                                          ClutterColorState *target_color_state,
                                          CoglPipeline      *pipeline);

CLUTTER_EXPORT
void clutter_color_state_do_transform (ClutterColorState *color_state,
                                       ClutterColorState *target_color_state,
                                       const float       *input,
                                       float             *output,
                                       int                n_samples);

CLUTTER_EXPORT
gboolean clutter_color_state_equals (ClutterColorState *color_state,
                                     ClutterColorState *other_color_state);

CLUTTER_EXPORT
ClutterEncodingRequiredFormat clutter_color_state_required_format (ClutterColorState *color_state);

CLUTTER_EXPORT
ClutterColorState * clutter_color_state_get_blending (ClutterColorState *color_state,
                                                      gboolean           force);

G_END_DECLS
