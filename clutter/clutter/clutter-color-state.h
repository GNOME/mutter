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

typedef struct _ClutterLuminance ClutterLuminance;

G_BEGIN_DECLS

typedef enum
{
  CLUTTER_COLOR_STATE_TRANSFORM_OPAQUE = 1 << 0,
} ClutterColorStateTransformFlags;

#define CLUTTER_TYPE_COLOR_STATE (clutter_color_state_get_type ())
CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterColorState,
                          clutter_color_state,
                          CLUTTER, COLOR_STATE,
                          GObject)

struct _ClutterColorStateClass
{
  GObjectClass parent_class;

  gboolean (* equals) (ClutterColorState *color_state,
                       ClutterColorState *other_color_state);

  char * (* to_string) (ClutterColorState *color_state);

  ClutterEncodingRequiredFormat (* required_format) (ClutterColorState *color_state);

  ClutterColorState * (* get_blending) (ClutterColorState *color_state,
                                        gboolean           force);

  guint (* hash) (ClutterColorState *color_state);

  const ClutterLuminance * (* get_luminance) (ClutterColorState *color_state);
};

CLUTTER_EXPORT
char * clutter_color_state_to_string (ClutterColorState *color_state);

CLUTTER_EXPORT
uint64_t clutter_color_state_get_id (ClutterColorState *color_state);

CLUTTER_EXPORT
guint clutter_color_state_hash (ClutterColorState *color_state);

CLUTTER_EXPORT
gboolean clutter_color_state_equals (ClutterColorState *color_state,
                                     ClutterColorState *other_color_state);

CLUTTER_EXPORT
ClutterEncodingRequiredFormat clutter_color_state_required_format (ClutterColorState *color_state);

CLUTTER_EXPORT
ClutterColorState * clutter_color_state_get_blending (ClutterColorState *color_state,
                                                      gboolean           force);

CLUTTER_EXPORT
const ClutterLuminance * clutter_color_state_get_luminance (ClutterColorState *color_state);

CLUTTER_EXPORT
ClutterContext * clutter_color_state_get_context (ClutterColorState *color_state);

G_END_DECLS
