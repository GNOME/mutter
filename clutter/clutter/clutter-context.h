/*
 * Copyright (C) 2006 OpenedHand
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

#include "clutter-backend.h"
#include "clutter-settings.h"

typedef ClutterBackend * (* ClutterBackendConstructor) (ClutterContext *context,
                                                        gpointer        user_data);

#define CLUTTER_TYPE_CONTEXT (clutter_context_get_type ())
CLUTTER_EXPORT
G_DECLARE_FINAL_TYPE (ClutterContext, clutter_context,
                      CLUTTER, CONTEXT, GObject)

/**
 * clutter_context_new: (skip)
 */
ClutterContext * clutter_context_new (ClutterBackendConstructor   backend_constructor,
                                      gpointer                    user_data,
                                      GError                    **error);

/**
 * clutter_context_destroy: (skip)
 */
CLUTTER_EXPORT
void clutter_context_destroy (ClutterContext *context);

/**
 * clutter_context_get_backend:
 *
 * Returns: (transfer none): The %ClutterBackend
 */
CLUTTER_EXPORT
ClutterBackend * clutter_context_get_backend (ClutterContext *context);

ClutterTextDirection clutter_context_get_text_direction (ClutterContext *context);

CLUTTER_EXPORT
ClutterPipelineCache * clutter_context_get_pipeline_cache (ClutterContext *clutter_context);

/**
 * clutter_context_get_color_manager:
 *
 * Returns: (transfer none): The %ClutterColorManager
 */
CLUTTER_EXPORT
ClutterColorManager * clutter_context_get_color_manager (ClutterContext *context);

/**
 * clutter_context_get_settings:
 *
 * Returns: (transfer none): The %ClutterSettings
 */
CLUTTER_EXPORT
ClutterSettings * clutter_context_get_settings (ClutterContext *context);
