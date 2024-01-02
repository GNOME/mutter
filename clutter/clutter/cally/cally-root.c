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


/* GObject */
static void cally_root_finalize   (GObject *object);

/* AtkObject.h */
static void             cally_root_initialize           (AtkObject *accessible,
                                                         gpointer   data);
static gint             cally_root_get_n_children       (AtkObject *obj);
static AtkObject *      cally_root_ref_child            (AtkObject *obj,
                                                         gint i);
static AtkObject *      cally_root_get_parent           (AtkObject *obj);
static const char *     cally_root_get_name             (AtkObject *obj);

/* Private */
static void             cally_util_stage_added_cb       (ClutterStageManager *stage_manager,
                                                         ClutterStage *stage,
                                                         gpointer data);
static void             cally_util_stage_removed_cb     (ClutterStageManager *stage_manager,
                                                         ClutterStage *stage,
                                                         gpointer data);

typedef struct _CallyRootPrivate
{
/* We save the CallyStage objects. Other option could save the stage
 * list, and then just get the a11y object on the ref_child, etc. But
 * the ref_child is more common that the init and the stage-add,
 * stage-remove, so we avoid getting the accessible object
 * constantly
 */
  GSList *stage_list;

  /* signals id */
  gulong stage_added_id;
  gulong stage_removed_id;
} CallyRootPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CallyRoot, cally_root,  ATK_TYPE_GOBJECT_ACCESSIBLE)

static void
cally_root_class_init (CallyRootClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  AtkObjectClass *class = ATK_OBJECT_CLASS (klass);

  gobject_class->finalize = cally_root_finalize;

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
  CallyRootPrivate *priv = cally_root_get_instance_private (root);

  priv->stage_list = NULL;
  priv->stage_added_id = 0;
  priv->stage_removed_id = 0;
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

static void
cally_root_finalize (GObject *object)
{
  CallyRoot *root = CALLY_ROOT (object);
  GObject *stage_manager = NULL;
  CallyRootPrivate *priv;

  g_return_if_fail (CALLY_IS_ROOT (object));

  priv = cally_root_get_instance_private (root);
  if (priv->stage_list)
    {
      g_slist_free (priv->stage_list);
      priv->stage_list = NULL;
    }

  stage_manager = atk_gobject_accessible_get_object (ATK_GOBJECT_ACCESSIBLE (root));

  g_clear_signal_handler (&priv->stage_added_id, stage_manager);

  g_clear_signal_handler (&priv->stage_removed_id, stage_manager);

  G_OBJECT_CLASS (cally_root_parent_class)->finalize (object);
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
  CallyRootPrivate *priv = cally_root_get_instance_private (root);


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

      priv->stage_list = g_slist_append (priv->stage_list, cally_stage);
    }

  priv->stage_added_id =
    g_signal_connect (G_OBJECT (stage_manager), "stage-added",
                      G_CALLBACK (cally_util_stage_added_cb), root);

  priv->stage_removed_id =
    g_signal_connect (G_OBJECT (stage_manager), "stage-removed",
                      G_CALLBACK (cally_util_stage_removed_cb), root);

  ATK_OBJECT_CLASS (cally_root_parent_class)->initialize (accessible, data);
}


static gint
cally_root_get_n_children (AtkObject *obj)
{
  CallyRoot *root = CALLY_ROOT (obj);
  CallyRootPrivate *priv = cally_root_get_instance_private (root);

  return g_slist_length (priv->stage_list);
}

static AtkObject*
cally_root_ref_child (AtkObject *obj,
                      gint       i)
{
  CallyRoot *cally_root = CALLY_ROOT (obj);
  CallyRootPrivate *priv = cally_root_get_instance_private (cally_root);
  GSList *stage_list = NULL;
  gint num = 0;
  AtkObject *item = NULL;

  stage_list = priv->stage_list;
  num = g_slist_length (stage_list);

  g_return_val_if_fail ((i < num)&&(i >= 0), NULL);

  item = g_slist_nth_data (stage_list, i);
  if (!item)
    {
      return NULL;
    }

  g_object_ref (item);

  return item;
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

/* -------------------------------- PRIVATE --------------------------------- */

static void
cally_util_stage_added_cb (ClutterStageManager *stage_manager,
                           ClutterStage        *stage,
                           gpointer             data)
{
  CallyRoot *root = CALLY_ROOT (data);
  AtkObject *cally_stage = NULL;
  CallyRootPrivate *priv = cally_root_get_instance_private (root);

  gint index = -1;

  cally_stage = clutter_actor_get_accessible (CLUTTER_ACTOR (stage));

  atk_object_set_parent (cally_stage, ATK_OBJECT (root));

  priv->stage_list = g_slist_append (priv->stage_list, cally_stage);

  index = g_slist_index (priv->stage_list, cally_stage);
  g_signal_emit_by_name (root, "children_changed::add",
                         index, cally_stage, NULL);
  g_signal_emit_by_name (cally_stage, "create", 0);
}

static void
cally_util_stage_removed_cb (ClutterStageManager *stage_manager,
                             ClutterStage        *stage,
                             gpointer             data)
{
  CallyRoot *root = CALLY_ROOT (data);
  AtkObject *cally_stage = NULL;
  CallyRootPrivate *priv
    = cally_root_get_instance_private (root);
  gint index = -1;

  cally_stage = clutter_actor_get_accessible (CLUTTER_ACTOR (stage));

  index = g_slist_index (priv->stage_list, cally_stage);

  priv->stage_list = g_slist_remove (priv->stage_list,
                                     cally_stage);

  index = g_slist_index (priv->stage_list, cally_stage);
  g_signal_emit_by_name (root, "children_changed::remove",
                         index, cally_stage, NULL);
  g_signal_emit_by_name (cally_stage, "destroy", 0);
}
