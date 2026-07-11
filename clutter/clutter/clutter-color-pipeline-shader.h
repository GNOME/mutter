/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2026 Red Hat, Inc.
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
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-color-state.h"
#include "clutter/clutter-types.h"

CLUTTER_EXPORT
void clutter_color_pipeline_shader_add_transform (ClutterColorPipeline *color_pipeline,
                                                  CoglPipeline         *cogl_pipeline);

CLUTTER_EXPORT
void clutter_color_pipeline_shader_set_color_state (CoglPipeline                    *cogl_pipeline,
                                                    ClutterColorState               *source_color_state,
                                                    ClutterColorState               *target_color_state,
                                                    ClutterColorStateTransformFlags  flags);

CLUTTER_EXPORT
gboolean clutter_color_pipeline_shader_needs_color_state (ClutterColorState               *source_color_state,
                                                          ClutterColorState               *target_color_state,
                                                          ClutterColorStateTransformFlags  flags);
