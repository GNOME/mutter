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

G_BEGIN_DECLS

#define CLUTTER_TYPE_DESATURATE_EFFECT          (clutter_desaturate_effect_get_type ())

struct _ClutterDesaturateEffectClass
{
  ClutterOffscreenEffectClass parent_class;

  CoglPipeline *base_pipeline;
};

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterDesaturateEffect,
                          clutter_desaturate_effect,
                          CLUTTER, DESATURATE_EFFECT,
                          ClutterOffscreenEffect)

CLUTTER_EXPORT
ClutterEffect *clutter_desaturate_effect_new        (gdouble                  factor);

CLUTTER_EXPORT
void           clutter_desaturate_effect_set_factor (ClutterDesaturateEffect *effect,
                                                     gdouble                  factor);
CLUTTER_EXPORT
gdouble        clutter_desaturate_effect_get_factor (ClutterDesaturateEffect *effect);

G_END_DECLS
