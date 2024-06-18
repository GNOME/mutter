/* CALLY - The Clutter Accessibility Implementation Library
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
 * CallyActor:
 *
 * Implementation of the ATK interfaces for [class@Clutter.Actor]
 *
 * #CallyActor implements the required ATK interfaces of [class@Clutter.Actor]
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
 * Anyway, current implementation of AtkAction is done at CallyActor
 * providing methods to add and remove actions. This is based on the
 * one used at gailcell, and proposed as a change on #AtkAction
 * interface:
 *
 *  https://bugzilla.gnome.org/show_bug.cgi?id=649804
 *
 */

#include "config.h"

#include <glib.h>

#include "clutter/clutter.h"
#include "clutter/clutter-actor-private.h"

#include <math.h>

#include "cally/cally-actor.h"
#include "cally/cally-actor-private.h"

static void cally_actor_initialize (AtkObject *obj,
                                   gpointer   data);
static void cally_actor_finalize   (GObject *obj);

/* AtkObject.h */
static AtkObject*            cally_actor_get_parent          (AtkObject *obj);
static gint                  cally_actor_get_index_in_parent (AtkObject *obj);
static AtkStateSet*          cally_actor_ref_state_set       (AtkObject *obj);
static gint                  cally_actor_get_n_children      (AtkObject *obj);
static AtkObject*            cally_actor_ref_child           (AtkObject *obj,
                                                             gint       i);
static AtkAttributeSet *     cally_actor_get_attributes      (AtkObject *obj);

/* ClutterContainer */
static gint cally_actor_add_actor    (ClutterActor *container,
                                      ClutterActor *actor,
                                      gpointer      data);
static gint cally_actor_remove_actor (ClutterActor *container,
                                      ClutterActor *actor,
                                      gpointer      data);

/* AtkComponent.h */
static void     cally_actor_component_interface_init (AtkComponentIface *iface);
static void     cally_actor_get_extents              (AtkComponent *component,
                                                     gint         *x,
                                                     gint         *y,
                                                     gint         *width,
                                                     gint         *height,
                                                     AtkCoordType  coord_type);
static gint     cally_actor_get_mdi_zorder           (AtkComponent *component);
static gboolean cally_actor_grab_focus               (AtkComponent *component);

/* Misc functions */
static void cally_actor_notify_clutter          (GObject    *obj,
                                                GParamSpec *pspec);
static void cally_actor_real_notify_clutter     (GObject    *obj,
                                                 GParamSpec *pspec);

struct _CallyActorPrivate
{
  GList *children;
};

G_DEFINE_TYPE_WITH_CODE (CallyActor,
                         cally_actor,
                         ATK_TYPE_GOBJECT_ACCESSIBLE,
                         G_ADD_PRIVATE (CallyActor)
                         G_IMPLEMENT_INTERFACE (ATK_TYPE_COMPONENT,
                                                cally_actor_component_interface_init));

/**
 * cally_actor_new:
 * @actor: a #ClutterActor
 *
 * Creates a new #CallyActor for the given @actor
 *
 * Return value: the newly created #AtkObject
 */
AtkObject *
cally_actor_new (ClutterActor *actor)
{
  gpointer   object;
  AtkObject *atk_object;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), NULL);

  object = g_object_new (CALLY_TYPE_ACTOR, NULL);

  atk_object = ATK_OBJECT (object);
  atk_object_initialize (atk_object, actor);

  return atk_object;
}

static void
cally_actor_initialize (AtkObject *obj,
                        gpointer   data)
{
  CallyActor        *self  = NULL;
  CallyActorPrivate *priv  = NULL;
  ClutterActor     *actor = NULL;
  guint             handler_id;

  ATK_OBJECT_CLASS (cally_actor_parent_class)->initialize (obj, data);

  self = CALLY_ACTOR(obj);
  priv = cally_actor_get_instance_private (self);
  actor = CLUTTER_ACTOR (data);

  g_signal_connect (actor,
                    "notify",
                    G_CALLBACK (cally_actor_notify_clutter),
                    NULL);

  g_object_set_data (G_OBJECT (obj), "atk-component-layer",
                     GINT_TO_POINTER (ATK_LAYER_MDI));

  priv->children = clutter_actor_get_children (actor);

  /*
   * We store the handler ids for these signals in case some objects
   * need to remove these handlers.
   */
  handler_id = g_signal_connect (actor,
                                 "child-added",
                                 G_CALLBACK (cally_actor_add_actor),
                                 obj);
  g_object_set_data (G_OBJECT (obj), "cally-add-handler-id",
                     GUINT_TO_POINTER (handler_id));
  handler_id = g_signal_connect (actor,
                                 "child-removed",
                                 G_CALLBACK (cally_actor_remove_actor),
                                 obj);
  g_object_set_data (G_OBJECT (obj), "cally-remove-handler-id",
                     GUINT_TO_POINTER (handler_id));

  obj->role = ATK_ROLE_PANEL; /* typically objects implementing ClutterContainer
                                 interface would be a panel */
}

static void
cally_actor_class_init (CallyActorClass *klass)
{
  AtkObjectClass *class         = ATK_OBJECT_CLASS (klass);
  GObjectClass   *gobject_class = G_OBJECT_CLASS (klass);

  klass->notify_clutter = cally_actor_real_notify_clutter;

  /* GObject */
  gobject_class->finalize = cally_actor_finalize;

  /* AtkObject */
  class->get_parent          = cally_actor_get_parent;
  class->get_index_in_parent = cally_actor_get_index_in_parent;
  class->ref_state_set       = cally_actor_ref_state_set;
  class->initialize          = cally_actor_initialize;
  class->get_n_children      = cally_actor_get_n_children;
  class->ref_child           = cally_actor_ref_child;
  class->get_attributes      = cally_actor_get_attributes;
}

static void
cally_actor_init (CallyActor *cally_actor)
{
  CallyActorPrivate *priv = cally_actor_get_instance_private (cally_actor);
  priv->children = NULL;
}

static void
cally_actor_finalize (GObject *obj)
{
  CallyActor        *cally_actor = NULL;
  CallyActorPrivate *priv       = NULL;

  cally_actor = CALLY_ACTOR (obj);
  priv = cally_actor_get_instance_private (cally_actor);

  if (priv->children)
    {
      g_list_free (priv->children);
      priv->children = NULL;
    }

  G_OBJECT_CLASS (cally_actor_parent_class)->finalize (obj);
}

/* AtkObject */

static AtkObject *
cally_actor_get_parent (AtkObject *obj)
{
  ClutterActor *parent_actor = NULL;
  AtkObject    *parent       = NULL;
  ClutterActor *actor        = NULL;
  CallyActor    *cally_actor   = NULL;

  g_return_val_if_fail (CALLY_IS_ACTOR (obj), NULL);

  /* Check if we have and assigned parent */
  if (obj->accessible_parent)
    return obj->accessible_parent;

  /* Try to get it from the clutter parent */
  cally_actor = CALLY_ACTOR (obj);
  actor = CALLY_GET_CLUTTER_ACTOR (cally_actor);
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
cally_actor_get_index_in_parent (AtkObject *obj)
{
  CallyActor *cally_actor = NULL;
  ClutterActor *actor = NULL;
  ClutterActor *parent_actor = NULL;
  ClutterActor *iter;
  gint index = -1;

  g_return_val_if_fail (CALLY_IS_ACTOR (obj), -1);

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

  cally_actor = CALLY_ACTOR (obj);
  actor = CALLY_GET_CLUTTER_ACTOR (cally_actor);
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
cally_actor_ref_state_set (AtkObject *obj)
{
  ClutterActor         *actor = NULL;
  AtkStateSet          *state_set = NULL;
  ClutterStage         *stage = NULL;
  ClutterActor         *focus_actor = NULL;
  CallyActor            *cally_actor = NULL;

  g_return_val_if_fail (CALLY_IS_ACTOR (obj), NULL);
  cally_actor = CALLY_ACTOR (obj);

  state_set = ATK_OBJECT_CLASS (cally_actor_parent_class)->ref_state_set (obj);

  actor = CALLY_GET_CLUTTER_ACTOR (cally_actor);

  if (actor == NULL) /* Object is defunct */
    {
      atk_state_set_add_state (state_set, ATK_STATE_DEFUNCT);
    }
  else
    {
      if (clutter_actor_get_reactive (actor))
        {
          atk_state_set_add_state (state_set, ATK_STATE_SENSITIVE);
          atk_state_set_add_state (state_set, ATK_STATE_ENABLED);
        }

      if (clutter_actor_is_visible (actor))
        {
          atk_state_set_add_state (state_set, ATK_STATE_VISIBLE);

          /* It would be good to also check if the actor is on screen,
             like the old and removed clutter_actor_is_on_stage*/
          if (clutter_actor_get_paint_visibility (actor))
            atk_state_set_add_state (state_set, ATK_STATE_SHOWING);

        }

      /* See focus section on implementation notes */
      atk_state_set_add_state (state_set, ATK_STATE_FOCUSABLE);

      stage = CLUTTER_STAGE (clutter_actor_get_stage (actor));
      if (stage != NULL)
        {
          focus_actor = clutter_stage_get_key_focus (stage);
          if (focus_actor == actor)
            atk_state_set_add_state (state_set, ATK_STATE_FOCUSED);
        }
    }

  return state_set;
}

static gint
cally_actor_get_n_children (AtkObject *obj)
{
  ClutterActor *actor = NULL;

  g_return_val_if_fail (CALLY_IS_ACTOR (obj), 0);

  actor = CALLY_GET_CLUTTER_ACTOR (obj);

  if (actor == NULL) /* State is defunct */
    return 0;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), 0);

  return clutter_actor_get_n_children (actor);
}

static AtkObject*
cally_actor_ref_child (AtkObject *obj,
                       gint       i)
{
  ClutterActor *actor = NULL;
  ClutterActor *child = NULL;

  g_return_val_if_fail (CALLY_IS_ACTOR (obj), NULL);

  actor = CALLY_GET_CLUTTER_ACTOR (obj);
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

static AtkAttributeSet *
cally_actor_get_attributes (AtkObject *obj)
{
  AtkAttributeSet *attributes;
  AtkAttribute *toolkit;

  toolkit = g_new (AtkAttribute, 1);
  toolkit->name = g_strdup ("toolkit");
  toolkit->value = g_strdup ("clutter");

  attributes = g_slist_append (NULL, toolkit);

  return attributes;
}

/* ClutterContainer */
static gint
cally_actor_add_actor (ClutterActor *container,
                       ClutterActor *actor,
                       gpointer      data)
{
  AtkObject *atk_parent = clutter_actor_get_accessible (container);
  AtkObject *atk_child  = clutter_actor_get_accessible (actor);
  CallyActorPrivate *priv = cally_actor_get_instance_private (CALLY_ACTOR (atk_parent));
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
cally_actor_remove_actor (ClutterActor *container,
                          ClutterActor *actor,
                          gpointer      data)
{
  g_autoptr (AtkObject) atk_child = NULL;
  AtkPropertyValues values = { NULL };
  AtkObject *atk_parent = NULL;
  CallyActorPrivate *priv = NULL;
  gint index;

  g_return_val_if_fail (CLUTTER_IS_ACTOR (container), 0);
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), 0);

  atk_parent = clutter_actor_get_accessible (container);

  if (clutter_actor_has_accessible (actor))
    {
      atk_child = clutter_actor_get_accessible (actor);

      g_assert (ATK_IS_OBJECT (atk_child));
      g_object_ref (atk_child);

      g_value_init (&values.old_value, G_TYPE_POINTER);
      g_value_set_pointer (&values.old_value, atk_parent);

      values.property_name = "accessible-parent";

      g_signal_emit_by_name (atk_child,
                             "property_change::accessible-parent", &values, NULL);
    }

  priv = cally_actor_get_instance_private (CALLY_ACTOR (atk_parent));
  index = g_list_index (priv->children, actor);
  g_list_free (priv->children);

  priv->children = clutter_actor_get_children (CLUTTER_ACTOR (container));

  if (index >= 0 && index <= g_list_length (priv->children))
    g_signal_emit_by_name (atk_parent, "children_changed::remove",
                           index, atk_child, NULL);

  return 1;
}

/* AtkComponent implementation */
static void
cally_actor_component_interface_init (AtkComponentIface *iface)
{
  g_return_if_fail (iface != NULL);

  iface->get_extents    = cally_actor_get_extents;
  iface->get_mdi_zorder = cally_actor_get_mdi_zorder;

  /* focus management */
  iface->grab_focus           = cally_actor_grab_focus;
}

static void
cally_actor_get_extents (AtkComponent *component,
                        gint         *x,
                        gint         *y,
                        gint         *width,
                        gint         *height,
                        AtkCoordType coord_type)
{
  CallyActor   *cally_actor = NULL;
  ClutterActor *actor      = NULL;
  gfloat        f_width, f_height;
  graphene_point3d_t verts[4];
  ClutterActor  *stage = NULL;

  g_return_if_fail (CALLY_IS_ACTOR (component));

  cally_actor = CALLY_ACTOR (component);
  actor = CALLY_GET_CLUTTER_ACTOR (cally_actor);

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
cally_actor_get_mdi_zorder (AtkComponent *component)
{
  CallyActor    *cally_actor = NULL;
  ClutterActor *actor = NULL;

  g_return_val_if_fail (CALLY_IS_ACTOR (component), G_MININT);

  cally_actor = CALLY_ACTOR(component);
  actor = CALLY_GET_CLUTTER_ACTOR (cally_actor);

  return (int) clutter_actor_get_z_position (actor);
}

static gboolean
cally_actor_grab_focus (AtkComponent    *component)
{
  ClutterActor *actor      = NULL;
  ClutterActor *stage      = NULL;
  CallyActor    *cally_actor = NULL;

  g_return_val_if_fail (CALLY_IS_ACTOR (component), FALSE);

  /* See focus section on implementation notes */
  cally_actor = CALLY_ACTOR(component);
  actor = CALLY_GET_CLUTTER_ACTOR (cally_actor);
  stage = clutter_actor_get_stage (actor);

  clutter_stage_set_key_focus (CLUTTER_STAGE (stage),
                               actor);

  return TRUE;
}

/* Misc functions */

/*
 * This function is a signal handler for notify signal which gets emitted
 * when a property changes value on the ClutterActor associated with the object.
 *
 * It calls a function for the CallyActor type
 */
static void
cally_actor_notify_clutter (GObject    *obj,
                            GParamSpec *pspec)
{
  CallyActor      *cally_actor = NULL;
  CallyActorClass *klass      = NULL;

  cally_actor = CALLY_ACTOR (clutter_actor_get_accessible (CLUTTER_ACTOR (obj)));
  klass = CALLY_ACTOR_GET_CLASS (cally_actor);

  if (klass->notify_clutter)
    klass->notify_clutter (obj, pspec);
}

/*
 * This function is a signal handler for notify signal which gets emitted
 * when a property changes value on the ClutterActor associated with a CallyActor
 *
 * It constructs an AtkPropertyValues structure and emits a "property_changed"
 * signal which causes the user specified AtkPropertyChangeHandler
 * to be called.
 */
static void
cally_actor_real_notify_clutter (GObject    *obj,
                                GParamSpec *pspec)
{
  ClutterActor* actor   = CLUTTER_ACTOR (obj);
  AtkObject*    atk_obj = clutter_actor_get_accessible (CLUTTER_ACTOR(obj));
  AtkState      state;
  gboolean      value;

  if (g_strcmp0 (pspec->name, "visible") == 0)
    {
      state = ATK_STATE_VISIBLE;
      value = clutter_actor_is_visible (actor);
    }
  else if (g_strcmp0 (pspec->name, "mapped") == 0)
    {
      /* Clones may temporarily map an actor in order to
       * paint it; we don't want this to generate an ATK
       * state change
       */
      if (clutter_actor_is_painting_unmapped (actor))
        return;

      state = ATK_STATE_SHOWING;
      value = clutter_actor_is_mapped (actor);
    }
  else if (g_strcmp0 (pspec->name, "reactive") == 0)
    {
      state = ATK_STATE_SENSITIVE;
      value = clutter_actor_get_reactive (actor);
    }
  else
    return;

  atk_object_notify_state_change (atk_obj, state, value);
}
