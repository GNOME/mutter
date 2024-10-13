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
#define CLUTTER_TYPE_COLOR_STATE_ICC (clutter_color_state_icc_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorStateIcc,
                      clutter_color_state_icc,
                      CLUTTER,
                      COLOR_STATE_ICC,
                      ClutterColorState)

CLUTTER_EXPORT
ClutterColorState *clutter_color_state_icc_new (ClutterContext  *context,
                                                const uint8_t   *icc_bytes,
                                                uint32_t         icc_length,
                                                GError         **error);

CLUTTER_EXPORT
const MtkAnonymousFile *clutter_color_state_icc_get_file (ClutterColorStateIcc *color_state_icc);

G_END_DECLS
