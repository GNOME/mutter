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

/**
 * ClutterStageManager:
 *
 * Maintains the list of stages
 *
 * #ClutterStageManager is a singleton object, owned by Clutter, which
 * maintains the list of currently active stages
 */

#include "config.h"

#include "clutter/clutter-stage-manager-private.h"

#include "clutter/clutter-context-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-marshal.h"
#include "clutter/clutter-private.h"

enum
{
  PROP_0,
};

struct _ClutterStageManager
{
  GObject parent;

  GSList *stages;
};

G_DEFINE_FINAL_TYPE (ClutterStageManager,
                     clutter_stage_manager,
                     G_TYPE_OBJECT);

static void
clutter_stage_manager_dispose (GObject *gobject)
{
  ClutterStageManager *stage_manager = CLUTTER_STAGE_MANAGER (gobject);

  g_slist_free_full (stage_manager->stages,
                     (GDestroyNotify) clutter_actor_destroy);
  stage_manager->stages = NULL;

  G_OBJECT_CLASS (clutter_stage_manager_parent_class)->dispose (gobject);
}

static void
clutter_stage_manager_class_init (ClutterStageManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose      = clutter_stage_manager_dispose;
}

static void
clutter_stage_manager_init (ClutterStageManager *stage_manager)
{
}

/**
 * clutter_stage_manager_peek_stages:
 * @stage_manager: a #ClutterStageManager
 *
 * Lists all currently used stages.
 *
 * Return value: (transfer none) (element-type Clutter.Stage): a pointer
 *   to the internal list of #ClutterStage objects. The returned list
 *   is owned by the #ClutterStageManager and should never be modified
 *   or freed
 */
const GSList *
clutter_stage_manager_peek_stages (ClutterStageManager *stage_manager)
{
  return stage_manager->stages;
}

void
_clutter_stage_manager_add_stage (ClutterStageManager *stage_manager,
                                  ClutterStage        *stage)
{
  AtkObject *stage_accessible = clutter_actor_get_accessible (CLUTTER_ACTOR (stage));
  AtkObject *stage_manager_accessible =
    atk_gobject_accessible_for_object (G_OBJECT (stage_manager));
  int index = -1;

  if (g_slist_find (stage_manager->stages, stage))
    {
      g_warning ("Trying to add a stage to the list of managed stages, "
                 "but it is already in it, aborting.");
      return;
    }

  g_object_ref_sink (stage);

  stage_manager->stages = g_slist_append (stage_manager->stages, stage);
  index = g_slist_index (stage_manager->stages, stage);

  if (stage_accessible && stage_manager_accessible)
    {
      atk_object_set_parent (stage_accessible, stage_manager_accessible);
      g_signal_emit_by_name (stage_manager_accessible, "children_changed::add",
                             index, stage_accessible, NULL);
      g_signal_emit_by_name (stage_manager_accessible, "create", 0);
    }
}

void
_clutter_stage_manager_remove_stage (ClutterStageManager *stage_manager,
                                     ClutterStage        *stage)
{
  AtkObject *stage_accessible = clutter_actor_get_accessible (CLUTTER_ACTOR (stage));
  AtkObject *stage_manager_accessible =
    atk_gobject_accessible_for_object (G_OBJECT (stage_manager));
  int index = -1;
  /* this might be called multiple times from a ::dispose, so it
   * needs to just return without warning
   */
  if (!g_slist_find (stage_manager->stages, stage))
    return;

  index = g_slist_index (stage_manager->stages, stage);
  stage_manager->stages = g_slist_remove (stage_manager->stages, stage);

  if (stage_manager_accessible && stage_accessible)
    {
      atk_object_set_parent (stage_accessible, NULL);
      g_signal_emit_by_name (stage_manager_accessible, "children_changed::remove",
                             index, stage_accessible, NULL);
      g_signal_emit_by_name (stage_accessible, "destroy", 0);
    }

  g_object_unref (stage);
}
