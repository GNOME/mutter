/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2009  Intel Corporation
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
 *      Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"
#include "clutter/clutter-layout-manager.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_LAYOUT_META (clutter_layout_meta_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterLayoutMeta, clutter_layout_meta, CLUTTER, LAYOUT_META, GObject)

struct _ClutterLayoutMetaClass
{
  GObjectClass parent_class;
};

CLUTTER_EXPORT
ClutterActor         *clutter_layout_meta_get_container (ClutterLayoutMeta    *data);
CLUTTER_EXPORT
ClutterActor         *clutter_layout_meta_get_actor     (ClutterLayoutMeta    *data);
CLUTTER_EXPORT
ClutterLayoutManager *clutter_layout_meta_get_manager   (ClutterLayoutMeta    *data);
CLUTTER_EXPORT
gboolean              clutter_layout_meta_is_for        (ClutterLayoutMeta    *data,
                                                         ClutterLayoutManager *manager,
                                                         ClutterActor         *container,
                                                         ClutterActor         *actor);

G_END_DECLS
