/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2011  Intel Corporation.
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

#include <pango/pango.h>

#include "clutter/clutter-paint-nodes.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_TEXT_NODE (clutter_text_node_get_type ())
#define CLUTTER_TEXT_NODE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLUTTER_TYPE_TEXT_NODE, ClutterTextNode))
#define CLUTTER_IS_TEXT_NODE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_TYPE_TEXT_NODE))

typedef struct _ClutterTextNode ClutterTextNode;
typedef struct _ClutterTextNodeClass ClutterTextNodeClass;

CLUTTER_EXPORT
GType clutter_text_node_get_type (void) G_GNUC_CONST;

CLUTTER_EXPORT
ClutterPaintNode * clutter_text_node_new (PangoLayout     *layout,
                                          const CoglColor *color);

G_END_DECLS
