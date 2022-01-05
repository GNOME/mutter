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

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#ifndef __CLUTTER_BOX_LAYOUT_DEPRECATED_H__
#define __CLUTTER_BOX_LAYOUT_DEPRECATED_H__

#include <clutter/clutter-box-layout.h>

G_BEGIN_DECLS

CLUTTER_DEPRECATED
void             clutter_box_layout_set_pack_start       (ClutterBoxLayout    *layout,
                                                          gboolean             pack_start);
CLUTTER_DEPRECATED
gboolean         clutter_box_layout_get_pack_start       (ClutterBoxLayout    *layout);

G_END_DECLS

#endif /* __CLUTTER_BOX_LAYOUT_DEPRECATED_H__ */

