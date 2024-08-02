/* CALLY - The Clutter Accessibility Implementation Library
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
 * CallyRoot:
 *
 * Root object for the Cally toolkit
 *
 * #CallyRoot is the root object of the accessibility tree-like
 * hierarchy, exposing the application level.
 *
 * Somewhat equivalent to #GailTopLevel. We consider that this class
 * expose the a11y information of the [class@Clutter.StageManager], as the
 * children of this object are the different [class@Clutter.Stage] managed (so
 * the [class@GObject.Object] used in the [method@Atk.Object.initialize] is the
 * [class@Clutter.StageManager]).
 */

#include "config.h"

#include "cally/cally-root.h"

#include "clutter/clutter-actor.h"
#include "clutter/clutter-stage-private.h"
#include "clutter/clutter-stage-manager.h"

/* AtkObject.h */
static void             cally_root_initialize           (AtkObject *accessible,
                                                         gpointer   data);
static gint             cally_root_get_n_children       (AtkObject *obj);
static AtkObject *      cally_root_ref_child            (AtkObject *obj,
                                                         gint i);
static AtkObject *      cally_root_get_parent           (AtkObject *obj);
static const char *     cally_root_get_name             (AtkObject *obj);

G_DEFINE_TYPE (CallyRoot, cally_root,  ATK_TYPE_GOBJECT_ACCESSIBLE)

static void
cally_root_class_init (CallyRootClass *klass)
{
  AtkObjectClass *class = ATK_OBJECT_CLASS (klass);

  /* AtkObject */
  class->get_n_children = cally_root_get_n_children;
  class->ref_child = cally_root_ref_child;
  class->get_parent = cally_root_get_parent;
  class->initialize = cally_root_initialize;
  class->get_name = cally_root_get_name;
}

static void
cally_root_init (CallyRoot *root)
{
}

/**
 * cally_root_new:
 *
 * Creates a new #CallyRoot object.
 *
 * Return value: the newly created #AtkObject
 */
AtkObject*
cally_root_new (void)
{
  GObject *object = NULL;
  AtkObject *accessible = NULL;
  ClutterStageManager *stage_manager = NULL;

  object = g_object_new (CALLY_TYPE_ROOT, NULL);

  accessible = ATK_OBJECT (object);
  stage_manager = clutter_stage_manager_get_default ();

  atk_object_initialize (accessible, stage_manager);

  return accessible;
}

/* AtkObject.h */
static void
cally_root_initialize (AtkObject              *accessible,
                       gpointer                data)
{
  ClutterStageManager *stage_manager = NULL;
  const GSList        *iter          = NULL;
  const GSList        *stage_list    = NULL;
  ClutterStage        *clutter_stage = NULL;
  AtkObject           *cally_stage   = NULL;
  CallyRoot *root = CALLY_ROOT (accessible);

  accessible->role = ATK_ROLE_APPLICATION;
  accessible->accessible_parent = NULL;

  /* children initialization */
  stage_manager = CLUTTER_STAGE_MANAGER (data);
  stage_list = clutter_stage_manager_peek_stages (stage_manager);

  for (iter = stage_list; iter != NULL; iter = g_slist_next (iter))
    {
      clutter_stage = CLUTTER_STAGE (iter->data);
      cally_stage = clutter_actor_get_accessible (CLUTTER_ACTOR (clutter_stage));

      atk_object_set_parent (cally_stage, ATK_OBJECT (root));
    }

  ATK_OBJECT_CLASS (cally_root_parent_class)->initialize (accessible, data);
}


static gint
cally_root_get_n_children (AtkObject *obj)
{
  CallyRoot *root = CALLY_ROOT (obj);
  GObject *stage_manager =
    atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (root));
  const GSList *stages =
    clutter_stage_manager_peek_stages (CLUTTER_STAGE_MANAGER (stage_manager));

  return g_slist_length ((GSList *)stages);
}

static AtkObject*
cally_root_ref_child (AtkObject *obj,
                      gint       i)
{
  CallyRoot *cally_root = CALLY_ROOT (obj);
  GObject *stage_manager =
      atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (cally_root));
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
cally_root_get_parent (AtkObject *obj)
{
  return NULL;
}

static const char *
cally_root_get_name (AtkObject *obj)
{
  return g_get_prgname ();
}
