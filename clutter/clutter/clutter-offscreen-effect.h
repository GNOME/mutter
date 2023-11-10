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

#include "cogl/cogl.h"
#include "clutter/clutter-effect.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_OFFSCREEN_EFFECT           (clutter_offscreen_effect_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterOffscreenEffect,
                          clutter_offscreen_effect,
                          CLUTTER,
                          OFFSCREEN_EFFECT,
                          ClutterEffect)

/**
 * ClutterOffscreenEffectClass:
 * @create_texture: virtual function
 * @paint_target: virtual function
 *
 * The #ClutterOffscreenEffectClass structure contains only private data
 */
struct _ClutterOffscreenEffectClass
{
  /*< private >*/
  ClutterEffectClass parent_class;

  /*< public >*/
  CoglTexture* (* create_texture) (ClutterOffscreenEffect *effect,
                                   gfloat                  width,
                                   gfloat                  height);
  CoglPipeline* (* create_pipeline) (ClutterOffscreenEffect *effect,
                                     CoglTexture            *texture);
  void (* paint_target) (ClutterOffscreenEffect *effect,
                         ClutterPaintNode       *node,
                         ClutterPaintContext    *paint_context);
};

CLUTTER_EXPORT
CoglPipeline *  clutter_offscreen_effect_get_pipeline           (ClutterOffscreenEffect *effect);

CLUTTER_EXPORT
CoglTexture*    clutter_offscreen_effect_get_texture            (ClutterOffscreenEffect *effect);

CLUTTER_EXPORT
void            clutter_offscreen_effect_paint_target           (ClutterOffscreenEffect *effect,
                                                                 ClutterPaintNode       *node,
                                                                 ClutterPaintContext    *paint_context);
CLUTTER_EXPORT
CoglTexture* clutter_offscreen_effect_create_texture         (ClutterOffscreenEffect *effect,
                                                              gfloat                  width,
                                                              gfloat                  height);

CLUTTER_EXPORT
gboolean        clutter_offscreen_effect_get_target_size        (ClutterOffscreenEffect *effect,
                                                                 gfloat                 *width,
                                                                 gfloat                 *height);

G_END_DECLS
