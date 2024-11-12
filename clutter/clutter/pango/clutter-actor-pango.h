/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2024 Red Hat
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

#include <pango/pango.h>

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

CLUTTER_EXPORT
PangoContext * clutter_actor_get_pango_context (ClutterActor *self);

CLUTTER_EXPORT
PangoContext * clutter_actor_create_pango_context (ClutterActor *self);

CLUTTER_EXPORT
PangoLayout * clutter_actor_create_pango_layout (ClutterActor *self,
                                                 const gchar  *text);

G_END_DECLS
