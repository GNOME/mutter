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

#include "clutter/clutter-offscreen-effect.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_SHADER_EFFECT              (clutter_shader_effect_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterShaderEffect,
                          clutter_shader_effect,
                          CLUTTER,
                          SHADER_EFFECT,
                          ClutterOffscreenEffect)

/**
 * ClutterShaderEffectClass:
 * @get_static_shader_source: Returns the GLSL source code to use for
 *  instances of this shader effect. Note that this function is only
 *  called once per subclass of #ClutterShaderEffect regardless of how
 *  many instances are used. It is expected that subclasses will return
 *  a copy of a static string from this function.
 *
 * The #ClutterShaderEffectClass structure contains
 * only private data
 */
struct _ClutterShaderEffectClass
{
  /*< private >*/
  ClutterOffscreenEffectClass parent_class;

  /*< public >*/
  gchar * (* get_static_shader_source) (ClutterShaderEffect *effect);
};

CLUTTER_EXPORT
ClutterEffect * clutter_shader_effect_new               (ClutterShaderType    shader_type);

CLUTTER_EXPORT
gboolean        clutter_shader_effect_set_shader_source (ClutterShaderEffect *effect,
                                                         const gchar         *source);

CLUTTER_EXPORT
void            clutter_shader_effect_set_uniform       (ClutterShaderEffect *effect,
                                                         const gchar         *name,
                                                         GType                gtype,
                                                         gsize                n_values,
                                                         ...);
CLUTTER_EXPORT
void            clutter_shader_effect_set_uniform_value (ClutterShaderEffect *effect,
                                                         const gchar         *name,
                                                         const GValue        *value);

CLUTTER_EXPORT
CoglShader*     clutter_shader_effect_get_shader        (ClutterShaderEffect *effect);
CLUTTER_EXPORT
CoglProgram*    clutter_shader_effect_get_program       (ClutterShaderEffect *effect);

G_END_DECLS
