/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010  Intel Corporation.
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

#include "clutter/clutter-effect.h"
#include "clutter/clutter-offscreen-effect.h"
#include "cogl/cogl-color.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_COLORIZE_EFFECT    (clutter_colorize_effect_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterColorizeEffect,
                          clutter_colorize_effect,
                          CLUTTER, COLORIZE_EFFECT,
                          ClutterOffscreenEffect)

struct _ClutterColorizeEffectClass
{
  ClutterOffscreenEffectClass parent_class;

  CoglPipeline *base_pipeline;
};

CLUTTER_EXPORT
ClutterEffect *clutter_colorize_effect_new      (const CoglColor *tint);

CLUTTER_EXPORT
void           clutter_colorize_effect_set_tint (ClutterColorizeEffect *effect,
                                                 const CoglColor       *tint);
CLUTTER_EXPORT
void           clutter_colorize_effect_get_tint (ClutterColorizeEffect *effect,
                                                 CoglColor             *tint);

G_END_DECLS
