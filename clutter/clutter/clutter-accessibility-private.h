/* Clutter.
 *
 * Copyright (C) 2008 Igalia, S.L.
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
#include "clutter/clutter-stage.h"

G_BEGIN_DECLS

void _clutter_accessibility_override_atk_util (void);

gboolean clutter_accessibility_snoop_key_event (ClutterStage    *stage,
                                                ClutterKeyEvent *key);

G_END_DECLS
