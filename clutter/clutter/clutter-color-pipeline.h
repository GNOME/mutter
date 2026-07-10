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

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_COLOR_PIPELINE (clutter_color_pipeline_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterColorPipeline,
                      clutter_color_pipeline,
                      CLUTTER, COLOR_PIPELINE,
                      GObject)

CLUTTER_EXPORT
void clutter_color_pipeline_add_op (ClutterColorPipeline *color_pipeline,
                                    ClutterColorOp       *op);

CLUTTER_EXPORT
void clutter_color_pipeline_take_op (ClutterColorPipeline *color_pipeline,
                                     ClutterColorOp       *op);

CLUTTER_EXPORT
char * clutter_color_pipeline_to_string (ClutterColorPipeline *color_pipeline);

CLUTTER_EXPORT
void clutter_color_pipeline_do_transform (ClutterColorPipeline *color_pipeline,
                                          float                *data,
                                          size_t                n_samples);

CLUTTER_EXPORT
gboolean clutter_color_pipeline_is_empty (ClutterColorPipeline *color_pipeline);

CLUTTER_EXPORT
const GList * clutter_color_pipeline_get_ops (ClutterColorPipeline *color_pipeline);

CLUTTER_EXPORT
void clutter_color_pipeline_append (ClutterColorPipeline *color_pipeline,
                                    ClutterColorPipeline *other);

CLUTTER_EXPORT
void clutter_color_pipeline_simplify (ClutterColorPipeline *color_pipeline);

CLUTTER_EXPORT
void clutter_color_pipeline_combine (ClutterColorPipeline *color_pipeline);

G_END_DECLS
