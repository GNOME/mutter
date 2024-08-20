/* Clutter.
 *
 * Copyright (C) 2008 Igalia, S.L.
 *
 * Author: Alejandro Piñeiro Iglesias <apinheiro@igalia.com>
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * ClutterStageManagerAccessible:
 *
 * The root object of the accessibility tree-like
 * hierarchy, exposing the application level.
 *
 * Somewhat equivalent to #GailTopLevel. We consider that this class
 * expose the a11y information of the [class@Clutter.StageManager], as the
 * children of this object are the different [class@Clutter.Stage] managed (so
 * the [class@GObject.Object] used in the [method@Atk.Object.initialize] is the
 * [class@Clutter.StageManager]).
 */

#include "atk/atk.h"
#include "config.h"

#include "clutter/clutter-stage-manager-accessible-private.h"

#include "clutter/clutter-actor.h"
#include "clutter/clutter-stage-private.h"

struct _ClutterStageManagerAccessible {
  AtkGObjectAccessible parent;
};

G_DEFINE_FINAL_TYPE (ClutterStageManagerAccessible, clutter_stage_manager_accessible,  ATK_TYPE_GOBJECT_ACCESSIBLE)

static void
clutter_stage_manager_accessible_init (ClutterStageManagerAccessible *manager_accessible)
{
}

/**
 * clutter_stage_manager_accessible_new:
 *
 * Creates a new #ClutterStageManagerAccessible object.
 *
 * Return value: the newly created #AtkObject
 */
AtkObject*
clutter_stage_manager_accessible_new (ClutterStageManager *stage_manager)
{
  GObject *object = NULL;
  AtkObject *accessible = NULL;

  object = g_object_new (CLUTTER_TYPE_STAGE_MANAGER_ACCESSIBLE, NULL);

  accessible = ATK_OBJECT (object);

  atk_object_initialize (accessible, stage_manager);

  return accessible;
}

/* AtkObject.h */
static void
clutter_stage_manager_accessible_initialize (AtkObject *accessible,
                                             gpointer   data)
{
  ClutterStageManager *stage_manager = NULL;
  const GSList *iter = NULL;
  const GSList *stage_list = NULL;
  ClutterStage *stage = NULL;
  AtkObject *stage_accessible = NULL;
  ClutterStageManagerAccessible *manager_accessible =
    CLUTTER_STAGE_MANAGER_ACCESSIBLE (accessible);

  accessible->role = ATK_ROLE_APPLICATION;
  accessible->accessible_parent = NULL;

  /* children initialization */
  stage_manager = CLUTTER_STAGE_MANAGER (data);
  stage_list = clutter_stage_manager_peek_stages (stage_manager);

  for (iter = stage_list; iter != NULL; iter = g_slist_next (iter))
    {
      stage = CLUTTER_STAGE (iter->data);
      stage_accessible = clutter_actor_get_accessible (CLUTTER_ACTOR (stage));

      atk_object_set_parent (stage_accessible, ATK_OBJECT (manager_accessible));
    }

  ATK_OBJECT_CLASS (clutter_stage_manager_accessible_parent_class)->initialize (accessible, data);
}


static gint
clutter_stage_manager_accessible_get_n_children (AtkObject *obj)
{
  ClutterStageManagerAccessible *manager_accessible =
    CLUTTER_STAGE_MANAGER_ACCESSIBLE (obj);
  GObject *stage_manager =
    atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (manager_accessible));
  const GSList *stages =
    clutter_stage_manager_peek_stages (CLUTTER_STAGE_MANAGER (stage_manager));

  return g_slist_length ((GSList *)stages);
}

static AtkObject*
clutter_stage_manager_accessible_ref_child (AtkObject *obj,
                                            gint       i)
{
  ClutterStageManagerAccessible *manager_accessible =
    CLUTTER_STAGE_MANAGER_ACCESSIBLE (obj);
  GObject *stage_manager =
    atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (manager_accessible));
  const GSList *stages =
    clutter_stage_manager_peek_stages (CLUTTER_STAGE_MANAGER (stage_manager));
  gint n_stages = g_slist_length ((GSList *)stages);
  ClutterStage *item = NULL;
  AtkObject *accessible_item = NULL;

  g_return_val_if_fail ((i < n_stages)&&(i >= 0), NULL);

  item = g_slist_nth_data ((GSList *)stages, i);
  if (!item)
    return NULL;

  accessible_item = clutter_actor_get_accessible (CLUTTER_ACTOR (item));
  if (accessible_item)
    g_object_ref (accessible_item);

  return accessible_item;
}

static AtkObject*
clutter_stage_manager_accessible_get_parent (AtkObject *obj)
{
  return NULL;
}

static const char *
clutter_stage_manager_accessible_get_name (AtkObject *obj)
{
  return g_get_prgname ();
}

static void
clutter_stage_manager_accessible_class_init (ClutterStageManagerAccessibleClass *klass)
{
  AtkObjectClass *class = ATK_OBJECT_CLASS (klass);

  /* AtkObject */
  class->get_n_children = clutter_stage_manager_accessible_get_n_children;
  class->ref_child = clutter_stage_manager_accessible_ref_child;
  class->get_parent = clutter_stage_manager_accessible_get_parent;
  class->initialize = clutter_stage_manager_accessible_initialize;
  class->get_name = clutter_stage_manager_accessible_get_name;
}
