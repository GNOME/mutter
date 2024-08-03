/* CALLY - The Clutter Accessibility Implementation Library
 *
 * Copyright (C) 2010 Igalia, S.L.
 *
 * Author: Alejandro Piñeiro Iglesias <apinheiro@igalia.com>
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

#include "clutter/clutter-actor-accessible.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_CLONE_ACCESSIBLE (clutter_clone_accessible_get_type ())

G_DECLARE_FINAL_TYPE (ClutterCloneAccessible,
                      clutter_clone_accessible,
                      CLUTTER,
                      CLONE_ACCESSIBLE,
                      ClutterActorAccessible)

G_END_DECLS
