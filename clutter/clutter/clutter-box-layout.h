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
 *
 * Based on the NBTK NbtkBoxLayout actor by:
 *   Thomas Wood <thomas.wood@intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-layout-manager.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_BOX_LAYOUT                 (clutter_box_layout_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterBoxLayout,
                          clutter_box_layout,
                          CLUTTER, BOX_LAYOUT,
                          ClutterLayoutManager)

struct _ClutterBoxLayoutClass
{
  /*< private >*/
  ClutterLayoutManagerClass parent_class;
};

CLUTTER_EXPORT
ClutterLayoutManager *  clutter_box_layout_new                 (void);

CLUTTER_EXPORT
void                    clutter_box_layout_set_orientation      (ClutterBoxLayout    *layout,
                                                                 ClutterOrientation   orientation);
CLUTTER_EXPORT
ClutterOrientation      clutter_box_layout_get_orientation      (ClutterBoxLayout    *layout);

CLUTTER_EXPORT
void                    clutter_box_layout_set_spacing          (ClutterBoxLayout    *layout,
                                                                 guint                spacing);
CLUTTER_EXPORT
guint                   clutter_box_layout_get_spacing          (ClutterBoxLayout    *layout);
CLUTTER_EXPORT
void                    clutter_box_layout_set_homogeneous      (ClutterBoxLayout    *layout,
                                                                 gboolean             homogeneous);
CLUTTER_EXPORT
gboolean                clutter_box_layout_get_homogeneous      (ClutterBoxLayout    *layout);

G_END_DECLS
