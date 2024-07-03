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
 *
 */

#pragma once

#include "clutter/clutter-color-manager.h"
#include "clutter/clutter-private.h"

unsigned int clutter_color_manager_get_next_id (ClutterColorManager *color_manager);

CoglSnippet * clutter_color_manager_lookup_snippet (ClutterColorManager            *color_manager,
                                                    const ClutterColorTransformKey *key);

void clutter_color_manager_add_snippet (ClutterColorManager            *color_manager,
                                        const ClutterColorTransformKey *key,
                                        CoglSnippet                    *snippet);
