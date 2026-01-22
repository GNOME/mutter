/*
 * Copyright (C) 2026 Red Hat Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-action.h"
#include "clutter/clutter-enums.h"

#define CLUTTER_TYPE_SCROLL_CONTROLLER (clutter_scroll_controller_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterScrollController,
                      clutter_scroll_controller,
                      CLUTTER, SCROLL_CONTROLLER,
                      ClutterAction)

CLUTTER_EXPORT
ClutterAction * clutter_scroll_controller_new (ClutterScrollControllerFlags flags);
