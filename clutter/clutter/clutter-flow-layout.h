/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2009  Intel Corporation.
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
 *   Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-layout-manager.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_FLOW_LAYOUT                (clutter_flow_layout_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterFlowLayout,
                      clutter_flow_layout,
                      CLUTTER, FLOW_LAYOUT,
                      ClutterLayoutManager)

CLUTTER_EXPORT
ClutterLayoutManager * clutter_flow_layout_new                (ClutterOrientation      orientation);

CLUTTER_EXPORT
void                   clutter_flow_layout_set_orientation    (ClutterFlowLayout      *layout,
                                                               ClutterOrientation      orientation);
CLUTTER_EXPORT
ClutterOrientation     clutter_flow_layout_get_orientation    (ClutterFlowLayout      *layout);
CLUTTER_EXPORT
void                   clutter_flow_layout_set_homogeneous    (ClutterFlowLayout      *layout,
                                                               gboolean                homogeneous);
CLUTTER_EXPORT
gboolean               clutter_flow_layout_get_homogeneous    (ClutterFlowLayout      *layout);

CLUTTER_EXPORT
void                   clutter_flow_layout_set_column_spacing (ClutterFlowLayout      *layout,
                                                               gfloat                  spacing);
CLUTTER_EXPORT
gfloat                 clutter_flow_layout_get_column_spacing (ClutterFlowLayout      *layout);
CLUTTER_EXPORT
void                   clutter_flow_layout_set_row_spacing    (ClutterFlowLayout      *layout,
                                                               gfloat                  spacing);
CLUTTER_EXPORT
gfloat                 clutter_flow_layout_get_row_spacing    (ClutterFlowLayout      *layout);

CLUTTER_EXPORT
void                   clutter_flow_layout_set_column_width   (ClutterFlowLayout      *layout,
                                                               gfloat                  min_width,
                                                               gfloat                  max_width);
CLUTTER_EXPORT
void                   clutter_flow_layout_get_column_width   (ClutterFlowLayout      *layout,
                                                               gfloat                 *min_width,
                                                               gfloat                 *max_width);
CLUTTER_EXPORT
void                   clutter_flow_layout_set_row_height     (ClutterFlowLayout      *layout,
                                                               gfloat                  min_height,
                                                               gfloat                  max_height);
CLUTTER_EXPORT
void                   clutter_flow_layout_get_row_height     (ClutterFlowLayout      *layout,
                                                               gfloat                 *min_height,
                                                               gfloat                 *max_height);
CLUTTER_EXPORT
void                   clutter_flow_layout_set_snap_to_grid   (ClutterFlowLayout      *layout,
                                                               gboolean                snap_to_grid);
CLUTTER_EXPORT
gboolean               clutter_flow_layout_get_snap_to_grid   (ClutterFlowLayout      *layout);

G_END_DECLS
