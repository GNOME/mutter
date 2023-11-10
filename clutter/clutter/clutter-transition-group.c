/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2012 Intel Corporation
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
 * ClutterTransitionGroup:
 * 
 * Group transitions together
 *
 * The #ClutterTransitionGroup allows running multiple [class@Transition]
 * instances concurrently.
 *
 * The transitions inside a group will run within the boundaries of the
 * group; for instance, if a transition has a duration of 10 seconds, and
 * the group that contains it has a duration of 5 seconds, only the first
 * 5 seconds of the transition will be played.
 */

#include "config.h"

#include "clutter/clutter-transition-group.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-timeline-private.h"

struct _ClutterTransitionGroup
{
  ClutterTransition parent_instance;

  GHashTable *transitions;
};

G_DEFINE_FINAL_TYPE (ClutterTransitionGroup, clutter_transition_group, CLUTTER_TYPE_TRANSITION)

static void
clutter_transition_group_new_frame (ClutterTimeline *timeline,
                                    gint             elapsed)
{
  ClutterTransitionGroup *transition = CLUTTER_TRANSITION_GROUP (timeline);
  GHashTableIter iter;
  gpointer element;
  gint64 msecs;

  /* get the time elapsed since the last ::new-frame... */
  msecs = clutter_timeline_get_delta (timeline);

  g_hash_table_iter_init (&iter, transition->transitions);
  while (g_hash_table_iter_next (&iter, &element, NULL))
    {
      ClutterTimeline *t = element;

      /* ... and advance every timeline */
      clutter_timeline_set_direction (t, clutter_timeline_get_direction (timeline));
      clutter_timeline_set_duration (t, clutter_timeline_get_duration (timeline));

      _clutter_timeline_advance (t, msecs);
    }
}

static void
clutter_transition_group_attached (ClutterTransition *transition,
                                   ClutterAnimatable *animatable)
{
  ClutterTransitionGroup *group = CLUTTER_TRANSITION_GROUP (transition);
  GHashTableIter iter;
  gpointer element;

  g_hash_table_iter_init (&iter, group->transitions);
  while (g_hash_table_iter_next (&iter, &element, NULL))
    {
      ClutterTransition *t = element;

      clutter_transition_set_animatable (t, animatable);
    }
}

static void
clutter_transition_group_detached (ClutterTransition *transition,
                                   ClutterAnimatable *animatable)
{
  ClutterTransitionGroup *group = CLUTTER_TRANSITION_GROUP (transition);
  GHashTableIter iter;
  gpointer element;

  g_hash_table_iter_init (&iter, group->transitions);
  while (g_hash_table_iter_next (&iter, &element, NULL))
    {
      ClutterTransition *t = element;

      clutter_transition_set_animatable (t, NULL);
    }
}

static void
clutter_transition_group_started (ClutterTimeline *timeline)
{
  ClutterTransitionGroup *transition = CLUTTER_TRANSITION_GROUP (timeline);
  GHashTableIter iter;
  gpointer element;

  g_hash_table_iter_init (&iter, transition->transitions);
  while (g_hash_table_iter_next (&iter, &element, NULL))
    {
      ClutterTransition *t = element;

      g_signal_emit_by_name (t, "started");
    }
}

static void
clutter_transition_group_finalize (GObject *gobject)
{
  ClutterTransitionGroup *transition = CLUTTER_TRANSITION_GROUP (gobject);

  g_hash_table_unref (transition->transitions);

  G_OBJECT_CLASS (clutter_transition_group_parent_class)->finalize (gobject);
}

static void
clutter_transition_group_class_init (ClutterTransitionGroupClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterTimelineClass *timeline_class = CLUTTER_TIMELINE_CLASS (klass);
  ClutterTransitionClass *transition_class = CLUTTER_TRANSITION_CLASS (klass);

  gobject_class->finalize = clutter_transition_group_finalize;

  timeline_class->started = clutter_transition_group_started;
  timeline_class->new_frame = clutter_transition_group_new_frame;

  transition_class->attached = clutter_transition_group_attached;
  transition_class->detached = clutter_transition_group_detached;
}

static void
clutter_transition_group_init (ClutterTransitionGroup *self)
{
  self->transitions =
    g_hash_table_new_full (NULL, NULL, (GDestroyNotify) g_object_unref, NULL);
}

/**
 * clutter_transition_group_new:
 *
 * Creates a new #ClutterTransitionGroup instance.
 *
 * Return value: the newly created #ClutterTransitionGroup. Use
 *   [method@GObject.Object.unref] when done to deallocate the resources it
 *   uses
 */
ClutterTransition *
clutter_transition_group_new (void)
{
  return g_object_new (CLUTTER_TYPE_TRANSITION_GROUP, NULL);
}

/**
 * clutter_transition_group_add_transition:
 * @group: a #ClutterTransitionGroup
 * @transition: a #ClutterTransition
 *
 * Adds @transition to @group.
 *
 * This function acquires a reference on @transition that will be released
 * when calling [method@TransitionGroup.remove_transition].
 */
void
clutter_transition_group_add_transition (ClutterTransitionGroup *group,
                                         ClutterTransition      *transition)
{
  g_return_if_fail (CLUTTER_IS_TRANSITION_GROUP (group));
  g_return_if_fail (CLUTTER_IS_TRANSITION (transition));

  g_hash_table_add (group->transitions, g_object_ref (transition));
}

/**
 * clutter_transition_group_remove_transition:
 * @group: a #ClutterTransitionGroup
 * @transition: a #ClutterTransition
 *
 * Removes @transition from @group.
 *
 * This function releases the reference acquired on @transition when
 * calling [method@TransitionGroup.add_transition].
 */
void
clutter_transition_group_remove_transition (ClutterTransitionGroup *group,
                                            ClutterTransition      *transition)
{
  g_return_if_fail (CLUTTER_IS_TRANSITION_GROUP (group));

  g_hash_table_remove (group->transitions, transition);
}

/**
 * clutter_transition_group_remove_all:
 * @group: a #ClutterTransitionGroup
 *
 * Removes all transitions from @group.
 *
 * This function releases the reference acquired when calling
 * [method@TransitionGroup.add_transition].
 */
void
clutter_transition_group_remove_all (ClutterTransitionGroup *group)
{
  g_return_if_fail (CLUTTER_IS_TRANSITION_GROUP (group));

  g_hash_table_remove_all (group->transitions);
}
