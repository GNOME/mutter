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

/**
 * ClutterConstraint:
 * 
 * Abstract class for constraints on position or size
 *
 * #ClutterConstraint is a base abstract class for modifiers of a #ClutterActor
 * position or size.
 *
 * A #ClutterConstraint sub-class should contain the logic for modifying
 * the position or size of the #ClutterActor to which it is applied, by
 * updating the actor's allocation. Each #ClutterConstraint can change the
 * allocation of the actor to which they are applied by overriding the
 * [vfunc@Clutter.Constraint.update_allocation] virtual function.
 *
 * ## Using Constraints
 *
 * Constraints can be used with fixed layout managers, like
 * #ClutterFixedLayout, or with actors implicitly using a fixed layout
 * manager, like #ClutterGroup and #ClutterStage.
 *
 * Constraints provide a way to build user interfaces by using
 * relations between #ClutterActors, without explicit fixed
 * positioning and sizing, similarly to how fluid layout managers like
 * #ClutterBoxLayout lay out their children.
 *
 * Constraints are attached to a #ClutterActor, and are available
 * for inspection using [method@Clutter.Actor.get_constraints].
 *
 * Clutter provides different implementation of the #ClutterConstraint
 * abstract class, for instance:
 *
 *  - #ClutterAlignConstraint, a constraint that can be used to align
 *  an actor to another one on either the horizontal or the vertical
 *  axis, using a normalized value between 0 and 1.
 *  - #ClutterBindConstraint, a constraint binds the X, Y, width or height
 *  of an actor to the corresponding position or size of a source actor,
 *  with or without an offset.
 *  - #ClutterSnapConstraint, a constraint that "snaps" together the edges
 *  of two #ClutterActors; if an actor uses two constraints on both its
 *  horizontal or vertical edges then it can also expand to fit the empty
 *  space.
 *
 * It is important to note that Clutter does not avoid loops or
 * competing constraints; if two or more #ClutterConstraints
 * are operating on the same positional or dimensional attributes of an
 * actor, or if the constraints on two different actors depend on each
 * other, then the behavior is undefined.
 *
 * ## Implementing a ClutterConstraint
 *
 * Creating a sub-class of #ClutterConstraint requires the
 * implementation of the [vfunc@Clutter.Constraint.update_allocation]
 * virtual function.
 *
 * The `update_allocation()` virtual function is called during the
 * allocation sequence of a #ClutterActor, and allows any #ClutterConstraint
 * attached to that actor to modify the allocation before it is passed to
 * the actor's #ClutterActorClass.allocate() implementation.
 *
 * The #ClutterActorBox passed to the `update_allocation()` implementation
 * contains the original allocation of the #ClutterActor, plus the eventual
 * modifications applied by the other #ClutterConstraints, in the same order
 * the constraints have been applied to the actor.
 *
 * It is not necessary for a #ClutterConstraint sub-class to chain
 * up to the parent's implementation.
 *
 * If a #ClutterConstraint is parametrized - i.e. if it contains
 * properties that affect the way the constraint is implemented - it should
 * call clutter_actor_queue_relayout() on the actor to which it is attached
 * to whenever any parameter is changed. The actor to which it is attached
 * can be recovered at any point using clutter_actor_meta_get_actor().
 */

#include "config.h"

#include <string.h>

#include "clutter/clutter-constraint-private.h"

#include "clutter/clutter-actor.h"
#include "clutter/clutter-actor-meta-private.h"
#include "clutter/clutter-private.h"

G_DEFINE_ABSTRACT_TYPE (ClutterConstraint,
                        clutter_constraint,
                        CLUTTER_TYPE_ACTOR_META);

static void
constraint_update_allocation (ClutterConstraint *constraint,
                              ClutterActor      *actor,
                              ClutterActorBox   *allocation)
{
}

static void
constraint_update_preferred_size (ClutterConstraint  *constraint,
                                  ClutterActor       *actor,
                                  ClutterOrientation  direction,
                                  float               for_size,
                                  float              *minimum_size,
                                  float              *natural_size)
{
}

static void
clutter_constraint_set_enabled (ClutterActorMeta *meta,
                                gboolean          is_enabled)
{
  ClutterActorMetaClass *parent_class =
    CLUTTER_ACTOR_META_CLASS (clutter_constraint_parent_class);
  ClutterActor *actor;

  actor = clutter_actor_meta_get_actor (meta);
  if (actor)
    clutter_actor_queue_relayout (actor);

  parent_class->set_enabled (meta, is_enabled);
}

static void
clutter_constraint_class_init (ClutterConstraintClass *klass)
{
  ClutterActorMetaClass *actor_meta_class = CLUTTER_ACTOR_META_CLASS (klass);

  actor_meta_class->set_enabled = clutter_constraint_set_enabled;

  klass->update_allocation = constraint_update_allocation;
  klass->update_preferred_size = constraint_update_preferred_size;
}

static void
clutter_constraint_init (ClutterConstraint *self)
{
}

/*< private >
 * clutter_constraint_update_allocation:
 * @constraint: a #ClutterConstraint
 * @actor: a #ClutterActor
 * @allocation: (inout): the allocation to modify
 *
 * Asks the @constraint to update the @allocation of a #ClutterActor.
 *
 * Returns: %TRUE if the allocation was updated
 */
gboolean
clutter_constraint_update_allocation (ClutterConstraint *constraint,
                                      ClutterActor      *actor,
                                      ClutterActorBox   *allocation)
{
  ClutterActorBox old_alloc;

  g_return_val_if_fail (CLUTTER_IS_CONSTRAINT (constraint), FALSE);
  g_return_val_if_fail (CLUTTER_IS_ACTOR (actor), FALSE);
  g_return_val_if_fail (allocation != NULL, FALSE);

  old_alloc = *allocation;

  CLUTTER_CONSTRAINT_GET_CLASS (constraint)->update_allocation (constraint,
                                                                actor,
                                                                allocation);

  return !clutter_actor_box_equal (allocation, &old_alloc);
}

/**
 * clutter_constraint_update_preferred_size:
 * @constraint: a #ClutterConstraint
 * @actor: a #ClutterActor
 * @direction: a #ClutterOrientation
 * @for_size: the size in the opposite direction
 * @minimum_size: (inout): the minimum size to modify
 * @natural_size: (inout): the natural size to modify
 *
 * Asks the @constraint to update the size request of a #ClutterActor.
 */
void
clutter_constraint_update_preferred_size (ClutterConstraint  *constraint,
                                          ClutterActor       *actor,
                                          ClutterOrientation  direction,
                                          float               for_size,
                                          float              *minimum_size,
                                          float              *natural_size)
{
  g_return_if_fail (CLUTTER_IS_CONSTRAINT (constraint));
  g_return_if_fail (CLUTTER_IS_ACTOR (actor));

  CLUTTER_CONSTRAINT_GET_CLASS (constraint)->update_preferred_size (constraint, actor,
                                                                    direction,
                                                                    for_size,
                                                                    minimum_size,
                                                                    natural_size);
}
