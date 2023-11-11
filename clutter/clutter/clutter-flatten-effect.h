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
 * Authors:
 *   Neil Roberts <neil@linux.intel.com>
 */

#pragma once

#include "clutter/clutter-offscreen-effect.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_FLATTEN_EFFECT (_clutter_flatten_effect_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterFlattenEffect,
                      _clutter_flatten_effect,
                      CLUTTER,
                      FLATTEN_EFFECT,
                      ClutterOffscreenEffect)


ClutterEffect *_clutter_flatten_effect_new (void);

G_END_DECLS
