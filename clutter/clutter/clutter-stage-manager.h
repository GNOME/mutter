/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2008 OpenedHand
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
 * Author: Emmanuele Bassi <ebassi@linux.intel.com>
 */

#pragma once

#if !defined(__CLUTTER_H_INSIDE__) && !defined(CLUTTER_COMPILATION)
#error "Only <clutter/clutter.h> can be included directly."
#endif

#include "clutter/clutter-types.h"

G_BEGIN_DECLS

#define CLUTTER_TYPE_STAGE_MANAGER              (clutter_stage_manager_get_type ())

CLUTTER_EXPORT
G_DECLARE_DERIVABLE_TYPE (ClutterStageManager,
                          clutter_stage_manager,
                          CLUTTER,
                          STAGE_MANAGER,
                          GObject)

/**
 * ClutterStageManagerClass:
 *
 * The #ClutterStageManagerClass structure contains only private data
 * and should be accessed using the provided API
 */
struct _ClutterStageManagerClass
{
  /*< private >*/
  GObjectClass parent_class;

  void (* stage_added)   (ClutterStageManager *stage_manager,
                          ClutterStage        *stage);
  void (* stage_removed) (ClutterStageManager *stage_manager,
                          ClutterStage        *stage);
};

CLUTTER_EXPORT
ClutterStageManager *clutter_stage_manager_get_default       (void);
CLUTTER_EXPORT
ClutterStage *       clutter_stage_manager_get_default_stage (ClutterStageManager *stage_manager);
CLUTTER_EXPORT
GSList *             clutter_stage_manager_list_stages       (ClutterStageManager *stage_manager);
CLUTTER_EXPORT
const GSList *       clutter_stage_manager_peek_stages       (ClutterStageManager *stage_manager);

G_END_DECLS
