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


/**
 * ClutterTextureVertex:
 * @x: Model x-coordinate
 * @y: Model y-coordinate
 * @z: Model z-coordinate
 * @tx: Texture x-coordinate
 * @ty: Texture y-coordinate
 * @color: The color to use at this vertex. This is ignored if
 *   use_color is %FALSE when calling cogl_polygon()
 *
 * Used to specify vertex information when calling cogl_polygon()
 */
typedef struct _ClutterTextureVertex
{
  float x, y, z;
  float tx, ty;

  CoglColor color;
} ClutterTextureVertex;

#ifndef __GI_SCANNER__
G_STATIC_ASSERT (sizeof (ClutterTextureVertex) == 24);
#endif

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
  void (* deform_vertex) (ClutterDeformEffect  *effect,
                          gfloat                width,
                          gfloat                height,
                          ClutterTextureVertex *vertex);
};

CLUTTER_EXPORT
void            clutter_deform_effect_set_back_pipeline (ClutterDeformEffect *effect,
                                                         CoglPipeline        *pipeline);
CLUTTER_EXPORT
CoglPipeline*   clutter_deform_effect_get_back_pipeline (ClutterDeformEffect *effect);
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
