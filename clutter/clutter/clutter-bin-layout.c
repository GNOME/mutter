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
 */

/**
 * ClutterBinLayout:
 *
 * A simple layout manager
 *
 * #ClutterBinLayout is a layout manager which implements the following
 * policy:
 *
 *   - the preferred size is the maximum preferred size
 *   between all the children of the container using the
 *   layout;
 *   - each child is allocated in "layers", on on top
 *   of the other;
 *   - for each layer there are horizontal and vertical
 *   alignment policies.
 */

#include "config.h"

#include <math.h>

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-animatable.h"
#include "clutter/clutter-bin-layout.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-layout-meta.h"
#include "clutter/clutter-private.h"

G_DEFINE_TYPE (ClutterBinLayout,
               clutter_bin_layout,
               CLUTTER_TYPE_LAYOUT_MANAGER)

static void
clutter_bin_layout_get_preferred_width (ClutterLayoutManager *manager,
                                        ClutterActor         *container,
                                        gfloat                for_height,
                                        gfloat               *min_width_p,
                                        gfloat               *nat_width_p)
{
  ClutterActor *actor = CLUTTER_ACTOR (container);
  ClutterActorIter iter;
  ClutterActor *child;
  gfloat min_width, nat_width;

  min_width = nat_width = 0.0;

  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      gfloat minimum, natural;

      if (!clutter_actor_is_visible (child))
        continue;

      clutter_actor_get_preferred_width (child, for_height,
                                         &minimum,
                                         &natural);

      min_width = MAX (min_width, minimum);
      nat_width = MAX (nat_width, natural);
    }

  if (min_width_p)
    *min_width_p = min_width;

  if (nat_width_p)
    *nat_width_p = nat_width;
}

static void
clutter_bin_layout_get_preferred_height (ClutterLayoutManager *manager,
                                         ClutterActor         *container,
                                         gfloat                for_width,
                                         gfloat               *min_height_p,
                                         gfloat               *nat_height_p)
{
  ClutterActor *actor = CLUTTER_ACTOR (container);
  ClutterActorIter iter;
  ClutterActor *child;
  gfloat min_height, nat_height;

  min_height = nat_height = 0.0;

  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      gfloat minimum, natural;

      if (!clutter_actor_is_visible (child))
        continue;

      clutter_actor_get_preferred_height (child, for_width,
                                          &minimum,
                                          &natural);

      min_height = MAX (min_height, minimum);
      nat_height = MAX (nat_height, natural);
    }

  if (min_height_p)
    *min_height_p = min_height;

  if (nat_height_p)
    *nat_height_p = nat_height;
}

static gdouble
get_actor_align_factor (ClutterActorAlign alignment)
{
  switch (alignment)
    {
    case CLUTTER_ACTOR_ALIGN_CENTER:
      return 0.5;

    case CLUTTER_ACTOR_ALIGN_START:
      return 0.0;

    case CLUTTER_ACTOR_ALIGN_END:
      return 1.0;

    case CLUTTER_ACTOR_ALIGN_FILL:
      return 0.0;
    }

  return 0.0;
}

static void
clutter_bin_layout_allocate (ClutterLayoutManager   *manager,
                             ClutterActor           *container,
                             const ClutterActorBox  *allocation)
{
  gfloat allocation_x, allocation_y;
  gfloat available_w, available_h;
  ClutterActor *actor, *child;
  ClutterActorIter iter;

  clutter_actor_box_get_origin (allocation, &allocation_x, &allocation_y);
  clutter_actor_box_get_size (allocation, &available_w, &available_h);

  actor = CLUTTER_ACTOR (container);

  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      ClutterActorBox child_alloc = { 0, };
      gdouble x_align, y_align;
      gboolean x_fill, y_fill, is_fixed_position_set;
      float fixed_x, fixed_y;

      if (!clutter_actor_is_visible (child))
        continue;

      fixed_x = fixed_y = 0.f;
      g_object_get (child,
                    "fixed-position-set", &is_fixed_position_set,
                    "fixed-x", &fixed_x,
                    "fixed-y", &fixed_y,
                    NULL);

      if (is_fixed_position_set)
	{
          if (is_fixed_position_set)
            child_alloc.x1 = fixed_x;
          else
            child_alloc.x1 = clutter_actor_get_x (child);
	}
      else
        child_alloc.x1 = allocation_x;

      if (is_fixed_position_set)
	{
	  if (is_fixed_position_set)
            child_alloc.y1 = fixed_y;
          else
	    child_alloc.y1 = clutter_actor_get_y (child);
	}
      else
        child_alloc.y1 = allocation_y;

      child_alloc.x2 = allocation_x + available_w;
      child_alloc.y2 = allocation_y + available_h;

      if (clutter_actor_needs_expand (child, CLUTTER_ORIENTATION_HORIZONTAL))
        {
          ClutterActorAlign align;

          align = clutter_actor_get_x_align (child);
          x_fill = align == CLUTTER_ACTOR_ALIGN_FILL;
          x_align = get_actor_align_factor (align);
        }
      else
        {
          x_fill = FALSE;

          if (!is_fixed_position_set)
            x_align = 0.5;
          else
            x_align = 0.0;
        }

      if (clutter_actor_needs_expand (child, CLUTTER_ORIENTATION_VERTICAL))
        {
          ClutterActorAlign align;

          align = clutter_actor_get_y_align (child);
          y_fill = align == CLUTTER_ACTOR_ALIGN_FILL;
          y_align = get_actor_align_factor (align);
        }
      else
        {
          y_fill = FALSE;

          if (!is_fixed_position_set)
            y_align = 0.5;
          else
            y_align = 0.0;
        }

      clutter_actor_allocate_align_fill (child, &child_alloc,
                                         x_align, y_align,
                                         x_fill, y_fill);
    }
}

static void
clutter_bin_layout_class_init (ClutterBinLayoutClass *klass)
{
  ClutterLayoutManagerClass *layout_class =
    CLUTTER_LAYOUT_MANAGER_CLASS (klass);

  layout_class->get_preferred_width = clutter_bin_layout_get_preferred_width;
  layout_class->get_preferred_height = clutter_bin_layout_get_preferred_height;
  layout_class->allocate = clutter_bin_layout_allocate;
}

static void
clutter_bin_layout_init (ClutterBinLayout *self)
{
}

/**
 * clutter_bin_layout_new:
 *
 * Creates a new #ClutterBinLayout layout manager
 *
 * Return value: the newly created layout manager
 */
ClutterLayoutManager *
clutter_bin_layout_new (void)
{
  return g_object_new (CLUTTER_TYPE_BIN_LAYOUT,
                       NULL);
}
