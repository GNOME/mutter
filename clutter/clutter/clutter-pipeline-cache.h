/*
 * Copyright (C) 2023 Red Hat
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
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-color-state.h"
#include "cogl/cogl.h"

typedef gpointer ClutterPipelineGroup;

#define CLUTTER_TYPE_PIPELINE_CACHE (clutter_pipeline_cache_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterPipelineCache, clutter_pipeline_cache,
                      CLUTTER, PIPELINE_CACHE, GObject)

CLUTTER_EXPORT
CoglPipeline * clutter_pipeline_cache_get_pipeline (ClutterPipelineCache *pipeline_cache,
                                                    ClutterPipelineGroup  group,
                                                    int                   slot,
                                                    ClutterColorState    *source_color_state,
                                                    ClutterColorState    *target_color_state);

CLUTTER_EXPORT
void clutter_pipeline_cache_set_pipeline (ClutterPipelineCache *pipeline_cache,
                                          ClutterPipelineGroup  group,
                                          int                   slot,
                                          ClutterColorState    *source_color_state,
                                          ClutterColorState    *target_color_state,
                                          CoglPipeline         *pipeline);

CLUTTER_EXPORT
void clutter_pipeline_cache_unset_pipeline (ClutterPipelineCache *pipeline_cache,
                                            ClutterPipelineGroup  group,
                                            int                   slot,
                                            ClutterColorState    *source_color_state,
                                            ClutterColorState    *target_color_state);

CLUTTER_EXPORT
void clutter_pipeline_cache_unset_all_pipelines (ClutterPipelineCache *pipeline_cache,
                                                 ClutterPipelineGroup  group);
