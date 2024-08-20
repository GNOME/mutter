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

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE_MANAGER              (clutter_stage_manager_get_type ())

G_DECLARE_FINAL_TYPE (ClutterStageManager,
                      clutter_stage_manager,
                      CLUTTER,
                      STAGE_MANAGER,
                      GObject)

const GSList * clutter_stage_manager_peek_stages (ClutterStageManager *stage_manager);

void _clutter_stage_manager_add_stage (ClutterStageManager *stage_manager,
                                       ClutterStage        *stage);

void _clutter_stage_manager_remove_stage (ClutterStageManager *stage_manager,
                                          ClutterStage        *stage);

G_END_DECLS
