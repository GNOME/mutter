/* Clutter.
 *
 * Copyright (C) 2008 Igalia, S.L.
 *
 * Author: Alejandro Piñeiro Iglesias <apinheiro@igalia.com>
 *
 * Some parts are based on GailWidget from GAIL
 * GAIL - The GNOME Accessibility Implementation Library
 * Copyright 2001, 2002, 2003 Sun Microsystems Inc.
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

#include <atk/atk.h>

#include "clutter/clutter-macros.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_ACTOR_ACCESSIBLE            (clutter_actor_accessible_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterActorAccessible,
                          clutter_actor_accessible,
                          CLUTTER,
                          ACTOR_ACCESSIBLE,
                          AtkGObjectAccessible)

typedef struct _ClutterActorAccessible ClutterActorAccessible;
typedef struct _ClutterActorAccessibleClass ClutterActorAccessibleClass;
typedef struct _ClutterActorAccessiblePrivate ClutterActorAccessiblePrivate;

struct _ClutterActorAccessibleClass
{
  /*< private >*/
  AtkGObjectAccessibleClass parent_class;
};

G_END_DECLS
