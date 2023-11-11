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
 *
 * Based on MxDeformPageTurn, written by:
 *   Chris Lord <chris@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-deform-effect.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_PAGE_TURN_EFFECT           (clutter_page_turn_effect_get_type ())

CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterPageTurnEffect,
                      clutter_page_turn_effect,
                      CLUTTER, PAGE_TURN_EFFECT,
                      ClutterDeformEffect)

CLUTTER_EXPORT
ClutterEffect *clutter_page_turn_effect_new (gdouble period,
                                             gdouble angle,
                                             gfloat  radius);

CLUTTER_EXPORT
void    clutter_page_turn_effect_set_period (ClutterPageTurnEffect *effect,
                                             gdouble                period);
CLUTTER_EXPORT
gdouble clutter_page_turn_effect_get_period (ClutterPageTurnEffect *effect);
CLUTTER_EXPORT
void    clutter_page_turn_effect_set_angle  (ClutterPageTurnEffect *effect,
                                             gdouble                angle);
CLUTTER_EXPORT
gdouble clutter_page_turn_effect_get_angle  (ClutterPageTurnEffect *effect);
CLUTTER_EXPORT
void    clutter_page_turn_effect_set_radius (ClutterPageTurnEffect *effect,
                                             gfloat                 radius);
CLUTTER_EXPORT
gfloat  clutter_page_turn_effect_get_radius (ClutterPageTurnEffect *effect);

G_END_DECLS
