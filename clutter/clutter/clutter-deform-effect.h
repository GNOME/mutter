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
#include "clutter/clutter-offscreen-effect.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_DEFORM_EFFECT              (clutter_deform_effect_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterDeformEffect,
                          clutter_deform_effect,
                          CLUTTER,
                          DEFORM_EFFECT,
                          ClutterOffscreenEffect)

/**
 * ClutterDeformEffectClass:
 * @deform_vertex: virtual function; sub-classes should override this
 *   function to compute the deformation of each vertex
 *
 * The #ClutterDeformEffectClass structure contains
 * only private data
 */
struct _ClutterDeformEffectClass
{
  /*< private >*/
  ClutterOffscreenEffectClass parent_class;

  /*< public >*/
  void (* deform_vertex) (ClutterDeformEffect *effect,
                          gfloat               width,
                          gfloat               height,
                          CoglTextureVertex   *vertex);
};

CLUTTER_EXPORT
void            clutter_deform_effect_set_back_material (ClutterDeformEffect *effect,
                                                         CoglPipeline        *material);
CLUTTER_EXPORT
CoglPipeline*   clutter_deform_effect_get_back_material (ClutterDeformEffect *effect);
CLUTTER_EXPORT
void            clutter_deform_effect_set_n_tiles       (ClutterDeformEffect *effect,
                                                         guint                x_tiles,
                                                         guint                y_tiles);
CLUTTER_EXPORT
void            clutter_deform_effect_get_n_tiles       (ClutterDeformEffect *effect,
                                                         guint               *x_tiles,
                                                         guint               *y_tiles);

CLUTTER_EXPORT
void            clutter_deform_effect_invalidate        (ClutterDeformEffect *effect);

G_END_DECLS
