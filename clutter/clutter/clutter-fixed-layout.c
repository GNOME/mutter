/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Copyright (C) 2009  Intel Corporation.
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
 *
 * Based on the fixed layout code inside clutter-group.c
 */

/**
 * ClutterFixedLayout:
 *
 * A fixed layout manager
 *
 * #ClutterFixedLayout is a layout manager implementing the same
 * layout policies as #ClutterGroup.
 */

#include "config.h"

#include "clutter/clutter-debug.h"
#include "clutter/clutter-fixed-layout.h"
#include "clutter/clutter-private.h"

G_DEFINE_TYPE (ClutterFixedLayout,
               clutter_fixed_layout,
               CLUTTER_TYPE_LAYOUT_MANAGER);

static void
clutter_fixed_layout_get_preferred_width (ClutterLayoutManager *manager,
                                          ClutterActor         *container,
                                          gfloat                for_height,
                                          gfloat               *min_width_p,
                                          gfloat               *nat_width_p)
{
  ClutterActor *actor, *child;
  gdouble min_right;
  gdouble natural_right;

  min_right = 0;
  natural_right = 0;

  actor = CLUTTER_ACTOR (container);

  for (child = clutter_actor_get_first_child (actor);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      gfloat child_x, child_min, child_natural;

      if (!clutter_actor_is_visible (child))
        continue;

      child_x = clutter_actor_get_x (child);

      clutter_actor_get_preferred_size (child,
                                        &child_min, NULL,
                                        &child_natural, NULL);

      if (child_x + child_min > min_right)
        min_right = child_x + child_min;

      if (child_x + child_natural > natural_right)
        natural_right = child_x + child_natural;
    }

  if (min_width_p)
    *min_width_p = (float) min_right;

  if (nat_width_p)
    *nat_width_p = (float) natural_right;
}

static void
clutter_fixed_layout_get_preferred_height (ClutterLayoutManager *manager,
                                           ClutterActor         *container,
                                           gfloat                for_width,
                                           gfloat               *min_height_p,
                                           gfloat               *nat_height_p)
{
  ClutterActor *actor, *child;
  gdouble min_bottom;
  gdouble natural_bottom;

  min_bottom = 0;
  natural_bottom = 0;

  actor = CLUTTER_ACTOR (container);

  for (child = clutter_actor_get_first_child (actor);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      gfloat child_y, child_min, child_natural;

      if (!clutter_actor_is_visible (child))
        continue;

      child_y = clutter_actor_get_y (child);

      clutter_actor_get_preferred_size (child,
                                        NULL, &child_min,
                                        NULL, &child_natural);

      if (child_y + child_min > min_bottom)
        min_bottom = child_y + child_min;

      if (child_y + child_natural > natural_bottom)
        natural_bottom = child_y + child_natural;
    }

  if (min_height_p)
    *min_height_p = (float) min_bottom;

  if (nat_height_p)
    *nat_height_p = (float) natural_bottom;
}

static void
clutter_fixed_layout_allocate (ClutterLayoutManager   *manager,
                               ClutterActor           *container,
                               const ClutterActorBox  *allocation)
{
  ClutterActor *child;

  for (child = clutter_actor_get_first_child (CLUTTER_ACTOR (container));
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      float x = 0.f;
      float y = 0.f;

      clutter_actor_get_fixed_position (child, &x, &y);
      clutter_actor_allocate_preferred_size (child, x, y);
    }
}

static void
clutter_fixed_layout_class_init (ClutterFixedLayoutClass *klass)
{
  ClutterLayoutManagerClass *manager_class =
    CLUTTER_LAYOUT_MANAGER_CLASS (klass);

  manager_class->get_preferred_width =
    clutter_fixed_layout_get_preferred_width;
  manager_class->get_preferred_height =
    clutter_fixed_layout_get_preferred_height;
  manager_class->allocate = clutter_fixed_layout_allocate;
}

static void
clutter_fixed_layout_init (ClutterFixedLayout *self)
{
}

/**
 * clutter_fixed_layout_new:
 *
 * Creates a new #ClutterFixedLayout
 *
 * Return value: the newly created #ClutterFixedLayout
 */
ClutterLayoutManager *
clutter_fixed_layout_new (void)
{
  return g_object_new (CLUTTER_TYPE_FIXED_LAYOUT, NULL);
}
