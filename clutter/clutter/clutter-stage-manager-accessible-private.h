/* Clutter.
 *
 * Copyright (C) 2009 Igalia, S.L.
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

#include <atk/atk.h>

#include "clutter/clutter-macros.h"
#include "clutter/clutter-stage-manager-private.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE_MANAGER_ACCESSIBLE (clutter_stage_manager_accessible_get_type ())

G_DECLARE_FINAL_TYPE (ClutterStageManagerAccessible,
                      clutter_stage_manager_accessible,
                      CLUTTER,
                      STAGE_MANAGER_ACCESSIBLE,
                      AtkGObjectAccessible)

AtkObject * clutter_stage_manager_accessible_new (ClutterStageManager *stage_manager);

G_END_DECLS
