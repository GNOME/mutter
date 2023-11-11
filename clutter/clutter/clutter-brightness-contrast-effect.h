/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2010-2012 Inclusive Design Research Centre, OCAD University.
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
 *   Joseph Scheuhammer <clown@alum.mit.edu>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-color.h"
#include "clutter/clutter-effect.h"
#include "clutter/clutter-offscreen-effect.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_BRIGHTNESS_CONTRAST_EFFECT     (clutter_brightness_contrast_effect_get_type ())


struct _ClutterBrightnessContrastEffectClass
{
  ClutterOffscreenEffectClass parent_class;

  CoglPipeline *base_pipeline;
};

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterBrightnessContrastEffect,
                          clutter_brightness_contrast_effect,
                          CLUTTER, BRIGHTNESS_CONTRAST_EFFECT,
                          ClutterOffscreenEffect)

CLUTTER_EXPORT
ClutterEffect * clutter_brightness_contrast_effect_new                          (void);

CLUTTER_EXPORT
void            clutter_brightness_contrast_effect_set_brightness_full          (ClutterBrightnessContrastEffect *effect,
                                                                                 float                            red,
                                                                                 float                            green,
                                                                                 float                            blue);
CLUTTER_EXPORT
void            clutter_brightness_contrast_effect_set_brightness               (ClutterBrightnessContrastEffect *effect,
                                                                                 float                            brightness);
CLUTTER_EXPORT
void            clutter_brightness_contrast_effect_get_brightness               (ClutterBrightnessContrastEffect *effect,
                                                                                 float                           *red,
                                                                                 float                           *green,
                                                                                 float                           *blue);

CLUTTER_EXPORT
void            clutter_brightness_contrast_effect_set_contrast_full            (ClutterBrightnessContrastEffect *effect,
                                                                                 float                            red,
                                                                                 float                            green,
                                                                                 float                            blue);
CLUTTER_EXPORT
void            clutter_brightness_contrast_effect_set_contrast                 (ClutterBrightnessContrastEffect *effect,
                                                                                 float                            contrast);
CLUTTER_EXPORT
void            clutter_brightness_contrast_effect_get_contrast                 (ClutterBrightnessContrastEffect *effect,
                                                                                 float                           *red,
                                                                                 float                           *green,
                                                                                 float                           *blue);

G_END_DECLS
