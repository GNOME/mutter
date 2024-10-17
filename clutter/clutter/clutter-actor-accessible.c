/* Clutter.
 *
 * Copyright (C) 2008 Igalia, S.L.
 *
 * Author: Alejandro Piñeiro Iglesias <apinheiro@igalia.com>
 *
 * Some parts are based on GailWidget from GAIL
 * GAIL - The GNOME Accessibility Implementation Library
 * Copyright 2001, 2002, 2003 Sun Microsystems Inc.
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

/**
 * ClutterActorAccessible:
 *
 * Implementation of the ATK interfaces for [class@Clutter.Actor]
 *
 * #ClutterActorAccessible implements the required ATK interfaces of [class@Clutter.Actor]
 * exposing the common elements on each actor (position, extents, etc).
 */

/*
 *
 * IMPLEMENTATION NOTES:
 *
 * ####
 *
 * Focus: clutter hasn't got the focus concept in the same way that GTK, but it
 * has a key focus managed by the stage. Basically any actor can be focused using
 * clutter_stage_set_key_focus. So, we will use this approach: all actors are
 * focusable, and we get the currently focused using clutter_stage_get_key_focus
 * This affects focus related stateset and some atk_component focus methods (like
 * grab focus).
 *
 * In the same way, we will manage the focus state change management
 * on the cally-stage object. The reason is avoid missing a focus
 * state change event if the object is focused just before the
 * accessibility object being created.
 *
 * #AtkAction implementation: on previous releases ClutterActor added
 * the actions "press", "release" and "click", as at that time some
 * general-purpose actors like textures were directly used as buttons.
 *
 * But now, new toolkits appeared, providing high-level widgets, like
 * buttons. So in this environment, it doesn't make sense to keep
 * adding them as default.
 *
 * Anyway, current implementation of AtkAction is done at ClutterActorAccessible
 * providing methods to add and remove actions. This is based on the
 * one used at gailcell, and proposed as a change on #AtkAction
 * interface:
 *
 *  https://bugzilla.gnome.org/show_bug.cgi?id=649804
 *
 */

#include "config.h"

#include <math.h>

#include <atk/atk.h>
#include <glib.h>

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-actor-accessible.h"
#include "clutter/clutter-stage.h"

/* AtkComponent.h */
static void clutter_actor_accessible_component_interface_init (AtkComponentIface *iface);

struct _ClutterActorAccessiblePrivate
{
  GList *children;
};

G_DEFINE_TYPE_WITH_CODE (ClutterActorAccessible,
                         clutter_actor_accessible,
                         ATK_TYPE_GOBJECT_ACCESSIBLE,
                         G_ADD_PRIVATE (ClutterActorAccessible)
                         G_IMPLEMENT_INTERFACE (ATK_TYPE_COMPONENT,
                                                clutter_actor_accessible_component_interface_init));

/* ClutterContainer */
static gint
clutter_actor_accessible_add_actor (ClutterActor *container,
                                    ClutterActor *actor,
                                    gpointer      data)
{
  AtkObject *atk_parent = clutter_actor_get_accessible (container);
  AtkObject *atk_child  = clutter_actor_get_accessible (actor);
  ClutterActorAccessiblePrivate *priv = clutter_actor_accessible_get_instance_private (CLUTTER_ACTOR_ACCESSIBLE  (atk_parent));
  gint index;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (container), 0);
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), 0);

  g_object_notify (G_OBJECT (atk_child), "accessible_parent");

  g_list_free (priv->children);

  priv->children = clutter_actor_get_children (CLUTTER_ACTOR (container));

  index = g_list_index (priv->children, actor);
  g_signal_emit_by_name (atk_parent, "children_changed::add",
                         index, atk_child, NULL);

  return 1;
}

static gint
clutter_actor_accessible_remove_actor (ClutterActor *container,
                                       ClutterActor *actor,
                                       gpointer      data)
{
  g_autoptr (AtkObject) atk_child = NULL;
  AtkPropertyValues values = { NULL };
  AtkObject *atk_parent = NULL;
  ClutterActorAccessiblePrivate *priv = NULL;
  gint index;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (container), 0);
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), 0);

  atk_parent = clutter_actor_get_accessible (container);
  atk_child = clutter_actor_get_accessible (actor);

  if (atk_child)
    {
      g_assert (ATK_IS_OBJECT (atk_child));
      g_object_ref (atk_child);

      g_value_init (&values.old_value, G_TYPE_POINTER);
      g_value_set_pointer (&values.old_value, atk_parent);

      values.property_name = "accessible-parent";

      g_signal_emit_by_name (atk_child,
                             "property_change::accessible-parent", &values, NULL);
    }

  priv = clutter_actor_accessible_get_instance_private (CLUTTER_ACTOR_ACCESSIBLE  (atk_parent));
  index = g_list_index (priv->children, actor);
  g_list_free (priv->children);

  priv->children = clutter_actor_get_children (CLUTTER_ACTOR (container));

  if (index >= 0 && index <= g_list_length (priv->children))
    g_signal_emit_by_name (atk_parent, "children_changed::remove",
                           index, atk_child, NULL);

  return 1;
}

static void
clutter_actor_accessible_initialize (AtkObject *obj,
                                     gpointer   data)
{
  ClutterActorAccessible        *self  = NULL;
  ClutterActorAccessiblePrivate *priv  = NULL;
  ClutterActor     *actor = NULL;
  guint             handler_id;

  ATK_OBJECT_CLASS (clutter_actor_accessible_parent_class)->initialize (obj, data);

  self = CLUTTER_ACTOR_ACCESSIBLE (obj);
  priv = clutter_actor_accessible_get_instance_private (self);
  actor = CLUTTER_ACTOR (data);

  g_object_set_data (G_OBJECT (obj), "atk-component-layer",
                     GINT_TO_POINTER (ATK_LAYER_MDI));

  priv->children = clutter_actor_get_children (actor);

  /*
   * We store the handler ids for these signals in case some objects
   * need to remove these handlers.
   */
  handler_id = g_signal_connect (actor,
                                 "child-added",
                                 G_CALLBACK (clutter_actor_accessible_add_actor),
                                 obj);
  g_object_set_data (G_OBJECT (obj), "cally-add-handler-id",
                     GUINT_TO_POINTER (handler_id));
  handler_id = g_signal_connect (actor,
                                 "child-removed",
                                 G_CALLBACK (clutter_actor_accessible_remove_actor),
                                 obj);
  g_object_set_data (G_OBJECT (obj), "cally-remove-handler-id",
                     GUINT_TO_POINTER (handler_id));

  obj->role = ATK_ROLE_PANEL; /* typically objects implementing ClutterContainer
                                 interface would be a panel */
}

/* AtkObject */
static const gchar *
clutter_actor_accessible_get_name (AtkObject *obj)
{
  const gchar* name = NULL;
  ClutterActor *actor;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (obj), NULL);

  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (obj);
  if (actor)
    name = clutter_actor_get_accessible_name (actor);

  if (!name)
    name = ATK_OBJECT_CLASS (clutter_actor_accessible_parent_class)->get_name (obj);

  return name;
}

static AtkRole
clutter_actor_accessible_get_role (AtkObject *obj)
{
  ClutterActor *actor = NULL;
  AtkRole role = ATK_ROLE_INVALID;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (obj), role);

  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (obj);

  if (actor == NULL)
    return role;

  role = actor->accessible_role;
  if (role == ATK_ROLE_INVALID)
    role = ATK_OBJECT_CLASS (clutter_actor_accessible_parent_class)->get_role (obj);

  return role;
}

static void
clutter_actor_accessible_init (ClutterActorAccessible *actor_accessible)
{
  ClutterActorAccessiblePrivate *priv = clutter_actor_accessible_get_instance_private (actor_accessible);
  priv->children = NULL;
}

static void
clutter_actor_accessible_finalize (GObject *obj)
{
  ClutterActorAccessible *actor_accessible =
    CLUTTER_ACTOR_ACCESSIBLE  (obj);
  ClutterActorAccessiblePrivate *priv =
    clutter_actor_accessible_get_instance_private (actor_accessible);

  if (priv->children)
    {
      g_list_free (priv->children);
      priv->children = NULL;
    }

  G_OBJECT_CLASS (clutter_actor_accessible_parent_class)->finalize (obj);
}

static AtkObject *
clutter_actor_accessible_get_parent (AtkObject *obj)
{
  ClutterActor *parent_actor = NULL;
  AtkObject    *parent       = NULL;
  ClutterActor *actor        = NULL;
  ClutterActorAccessible *actor_accessible = NULL;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (obj), NULL);

  /* Check if we have and assigned parent */
  if (obj->accessible_parent)
    return obj->accessible_parent;

  /* Try to get it from the clutter parent */
  actor_accessible = CLUTTER_ACTOR_ACCESSIBLE  (obj);
  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (actor_accessible);
  if (actor == NULL)  /* Object is defunct */
    return NULL;

  parent_actor = clutter_actor_get_parent (actor);
  if (parent_actor == NULL)
    return NULL;

  parent = clutter_actor_get_accessible (parent_actor);

  /* FIXME: I need to review the clutter-embed, to check if in this case I
   * should get the widget accessible
   */

  return parent;
}

static gint
clutter_actor_accessible_get_index_in_parent (AtkObject *obj)
{
  ClutterActorAccessible *actor_accessible = NULL;
  ClutterActor *actor = NULL;
  ClutterActor *parent_actor = NULL;
  ClutterActor *iter;
  gint index = -1;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (obj), -1);

  if (obj->accessible_parent)
    {
      gint n_children, i;
      gboolean found = FALSE;

      n_children = atk_object_get_n_accessible_children (obj->accessible_parent);
      for (i = 0; i < n_children; i++)
        {
          AtkObject *child;

          child = atk_object_ref_accessible_child (obj->accessible_parent, i);
          if (child == obj)
            found = TRUE;

          g_object_unref (child);
          if (found)
            return i;
        }
      return -1;
    }

  actor_accessible = CLUTTER_ACTOR_ACCESSIBLE  (obj);
  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (actor_accessible);
  if (actor == NULL) /* Object is defunct */
    return -1;

  index = 0;
  parent_actor = clutter_actor_get_parent (actor);
  if (parent_actor == NULL)
    return -1;

  for (iter = clutter_actor_get_first_child (parent_actor);
       iter != NULL && iter != actor;
       iter = clutter_actor_get_next_sibling (iter))
    {
      index += 1;
    }

  return index;
}

static AtkStateSet*
clutter_actor_accessible_ref_state_set (AtkObject *obj)
{
  ClutterActor *actor = NULL;
  g_autoptr (AtkStateSet) parent_state = NULL;
  AtkStateSet *combined_state, *actor_state = NULL;
  ClutterActorAccessible *actor_accessible = NULL;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (obj), NULL);
  actor_accessible = CLUTTER_ACTOR_ACCESSIBLE (obj);

  parent_state = ATK_OBJECT_CLASS (clutter_actor_accessible_parent_class)->ref_state_set (obj);

  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (actor_accessible);

  if (actor == NULL) /* Object is defunct */
    {
      atk_state_set_add_state (parent_state, ATK_STATE_DEFUNCT);
      combined_state = g_steal_pointer (&parent_state);
    }
  else
    {
      actor_state = clutter_actor_get_accessible_state (actor);

      if (actor_state)
        combined_state = atk_state_set_or_sets (parent_state,
                                                actor_state);
      else
        combined_state = g_steal_pointer (&parent_state);
    }

  return combined_state;
}

static gint
clutter_actor_accessible_get_n_children (AtkObject *obj)
{
  ClutterActor *actor = NULL;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (obj), 0);

  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (obj);

  if (actor == NULL) /* State is defunct */
    return 0;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), 0);

  return clutter_actor_get_n_children (actor);
}

static AtkObject*
clutter_actor_accessible_ref_child (AtkObject *obj,
                                    gint       i)
{
  ClutterActor *actor = NULL;
  ClutterActor *child = NULL;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (obj), NULL);

  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (obj);
  if (actor == NULL) /* State is defunct */
    return NULL;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), NULL);

  if (i >= clutter_actor_get_n_children (actor))
    return NULL;

  child = clutter_actor_get_child_at_index (actor, i);
  if (child == NULL)
    return NULL;

  return g_object_ref (clutter_actor_get_accessible (child));
}

static void
clutter_actor_accessible_class_init (ClutterActorAccessibleClass *klass)
{
  AtkObjectClass *class         = ATK_OBJECT_CLASS (klass);
  GObjectClass   *gobject_class = G_OBJECT_CLASS (klass);

  /* GObject */
  gobject_class->finalize = clutter_actor_accessible_finalize;

  /* AtkObject */
  class->get_role = clutter_actor_accessible_get_role;
  class->get_name = clutter_actor_accessible_get_name;
  class->get_parent = clutter_actor_accessible_get_parent;
  class->get_index_in_parent = clutter_actor_accessible_get_index_in_parent;
  class->ref_state_set = clutter_actor_accessible_ref_state_set;
  class->initialize = clutter_actor_accessible_initialize;
  class->get_n_children = clutter_actor_accessible_get_n_children;
  class->ref_child = clutter_actor_accessible_ref_child;
}

/* AtkComponent implementation */
static void
clutter_actor_accessible_get_extents (AtkComponent *component,
                                      gint         *x,
                                      gint         *y,
                                      gint         *width,
                                      gint         *height,
                                      AtkCoordType coord_type)
{
  ClutterActorAccessible *actor_accessible = NULL;
  ClutterActor *actor      = NULL;
  gfloat        f_width, f_height;
  graphene_point3d_t verts[4];
  ClutterActor  *stage = NULL;

  g_return_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (component));

  actor_accessible = CLUTTER_ACTOR_ACCESSIBLE  (component);
  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (actor_accessible);

  if (actor == NULL) /* actor is defunct */
    return;

  /* If the actor is not placed in any stage, we can't compute the
   * extents */
  stage = clutter_actor_get_stage (actor);
  if (stage == NULL)
    return;

  clutter_actor_get_abs_allocation_vertices (actor, verts);
  clutter_actor_get_transformed_size (actor, &f_width, &f_height);

  *x = (int) verts[0].x;
  *y = (int) verts[0].y;
  *width = (int) ceilf (f_width);
  *height = (int) ceilf (f_height);
}

static gint
clutter_actor_accessible_get_mdi_zorder (AtkComponent *component)
{
  ClutterActorAccessible *actor_accessible = NULL;
  ClutterActor *actor = NULL;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (component), G_MININT);

  actor_accessible = CLUTTER_ACTOR_ACCESSIBLE (component);
  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (actor_accessible);

  return (int) clutter_actor_get_z_position (actor);
}

static gboolean
clutter_actor_accessible_grab_focus (AtkComponent *component)
{
  ClutterActor *actor      = NULL;
  ClutterActor *stage      = NULL;
  ClutterActorAccessible    *actor_accessible = NULL;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (component), FALSE);

  /* See focus section on implementation notes */
  actor_accessible = CLUTTER_ACTOR_ACCESSIBLE (component);
  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (actor_accessible);
  stage = clutter_actor_get_stage (actor);

  clutter_stage_set_key_focus (CLUTTER_STAGE (stage),
                               actor);

  return TRUE;
}

static double
clutter_actor_accessible_get_alpha (AtkComponent *component)
{
  ClutterActor *actor;

  g_return_val_if_fail (CLUTTER_IS_ACTOR_ACCESSIBLE (component), 1.0);

  actor = CLUTTER_ACTOR_FROM_ACCESSIBLE (component);

  if (!actor)
    return 1.0;

  return clutter_actor_get_opacity (actor) / 255.0;
}

static void
clutter_actor_accessible_component_interface_init (AtkComponentIface *iface)
{
  g_return_if_fail (iface != NULL);

  iface->get_extents = clutter_actor_accessible_get_extents;
  iface->get_mdi_zorder = clutter_actor_accessible_get_mdi_zorder;
  iface->get_alpha = clutter_actor_accessible_get_alpha;

  /* focus management */
  iface->grab_focus = clutter_actor_accessible_grab_focus;
}
