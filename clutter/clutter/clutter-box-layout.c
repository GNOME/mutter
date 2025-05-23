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
 * Based on the NBTK NbtkBoxLayout actor by:
 *   Thomas Wood <thomas.wood@intel.com>
 */

/**
 * ClutterBoxLayout:
 *
 * A layout manager arranging children on a single line
 *
 * The #ClutterBoxLayout is a [class@Clutter.LayoutManager] implementing the
 * following layout policy:
 *
 *  - all children are arranged on a single line
 *  - the axis used is controlled by the [property@Clutter.BoxLayout:orientation] property
 *  - each child will be allocated to its natural size or, if [property@Clutter.Actor:x-expand] or
 *  [property@Clutter.Actor:y-expand] are set, the available size
 *  - honours the #ClutterActor's [property@Clutter.Actor:x-align] and [property@Clutter.Actor:y-align] properties
 *  to fill the available size
 *  - if the [property@Clutter.BoxLayout:homogeneous] boolean property is set, then all widgets will
 *  get the same size, ignoring expand settings and the preferred sizes
 *
 * It is possible to control the spacing between children of a
 * #ClutterBoxLayout by using [method@Clutter.BoxLayout.set_spacing].
 */

#include "config.h"

#include <math.h>

#include "clutter/clutter-box-layout.h"

#include "clutter/clutter-actor-private.h"
#include "clutter/clutter-debug.h"
#include "clutter/clutter-enum-types.h"
#include "clutter/clutter-layout-meta.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-types.h"

typedef struct _ClutterBoxLayoutPrivate
{
  ClutterActor *container;

  guint spacing;

  ClutterAnimationMode easing_mode;
  guint easing_duration;

  ClutterOrientation orientation;

  guint is_homogeneous : 1;
} ClutterBoxLayoutPrivate;

enum
{
  PROP_0,

  PROP_SPACING,
  PROP_HOMOGENEOUS,
  PROP_ORIENTATION,

  PROP_LAST
};

static GParamSpec *obj_props[PROP_LAST] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (ClutterBoxLayout,
                            clutter_box_layout,
                            CLUTTER_TYPE_LAYOUT_MANAGER)


typedef struct _RequestedSize
{
  ClutterActor *actor;

  gfloat minimum_size;
  gfloat natural_size;
} RequestedSize;

static float distribute_natural_allocation (float          extra_space,
                                            unsigned int   n_requested_sizes,
                                            RequestedSize *sizes);
static void count_expand_children         (ClutterLayoutManager *layout,
                                           ClutterActor         *container,
                                           gint                 *visible_children,
                                           gint                 *expand_children);

static void
clutter_box_layout_set_container (ClutterLayoutManager *layout,
                                  ClutterActor         *container)
{
  ClutterBoxLayout *self = CLUTTER_BOX_LAYOUT (layout);
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);
  ClutterLayoutManagerClass *parent_class;

  priv->container = container;

  if (priv->container != NULL)
    {
      ClutterRequestMode request_mode;

      /* we need to change the :request-mode of the container
       * to match the orientation
       */
      request_mode = priv->orientation == CLUTTER_ORIENTATION_VERTICAL
                   ? CLUTTER_REQUEST_HEIGHT_FOR_WIDTH
                   : CLUTTER_REQUEST_WIDTH_FOR_HEIGHT;
      clutter_actor_set_request_mode (CLUTTER_ACTOR (priv->container),
                                      request_mode);
    }

  parent_class = CLUTTER_LAYOUT_MANAGER_CLASS (clutter_box_layout_parent_class);
  parent_class->set_container (layout, container);
}

static void
get_child_size (ClutterActor       *actor,
		ClutterOrientation  orientation,
		gfloat              for_size,
		gfloat             *min_size_p,
		gfloat             *natural_size_p)
{
  if (orientation == CLUTTER_ORIENTATION_HORIZONTAL)
    clutter_actor_get_preferred_width (actor, for_size, min_size_p, natural_size_p);
  else
    clutter_actor_get_preferred_height (actor, for_size, min_size_p, natural_size_p);
}

/* Handle the request in the orientation of the box (i.e. width request of horizontal box) */
static void
get_preferred_size_for_orientation (ClutterBoxLayout   *self,
                                    ClutterActor       *container,
                                    gfloat              for_size,
                                    gfloat             *min_size_p,
                                    gfloat             *natural_size_p)
{
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);
  ClutterActorIter iter;
  ClutterActor *child;
  gint n_children = 0;
  gfloat minimum, natural;
  float largest_min_size, largest_nat_size;

  minimum = natural = 0;
  largest_min_size = largest_nat_size = 0;

  clutter_actor_iter_init (&iter, container);
  while (clutter_actor_iter_next (&iter, &child))
    {
      gfloat child_min = 0, child_nat = 0;

      if (!clutter_actor_is_visible (child))
	continue;

      n_children++;

      get_child_size (child, priv->orientation,
		      for_size, &child_min, &child_nat);

      if (priv->is_homogeneous)
        {
          largest_min_size = MAX (largest_min_size, child_min);
          largest_nat_size = MAX (largest_nat_size, child_nat);
        }
      else
        {
          minimum += child_min;
          natural += child_nat;
        }
    }

  if (priv->is_homogeneous)
    {
      minimum = largest_min_size * n_children;
      natural = largest_nat_size * n_children;
    }

  if (n_children > 1)
    {
      minimum += priv->spacing * (n_children - 1);
      natural += priv->spacing * (n_children - 1);
    }

  if (min_size_p)
    *min_size_p = minimum;

  if (natural_size_p)
    *natural_size_p = natural;
}

static void
get_base_size_for_opposite_orientation (ClutterBoxLayout   *self,
					ClutterActor       *container,
					gfloat             *min_size_p,
					gfloat             *natural_size_p)
{
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);
  ClutterActorIter iter;
  ClutterActor *child;
  gint n_children = 0;
  gfloat minimum, natural;
  ClutterOrientation opposite_orientation =
    priv->orientation == CLUTTER_ORIENTATION_HORIZONTAL
    ? CLUTTER_ORIENTATION_VERTICAL
    : CLUTTER_ORIENTATION_HORIZONTAL;

  minimum = natural = 0;

  clutter_actor_iter_init (&iter, container);
  while (clutter_actor_iter_next (&iter, &child))
    {
      gfloat child_min = 0, child_nat = 0;

      if (!clutter_actor_is_visible (child))
	continue;

      n_children++;

      get_child_size (child, opposite_orientation, -1, &child_min, &child_nat);

      minimum = MAX (minimum, child_min);
      natural = MAX (natural, child_nat);
    }

  if (min_size_p)
    *min_size_p = minimum;

  if (natural_size_p)
    *natural_size_p = natural;
}


/* Handle the request in the opposite orientation of the box
 * (i.e. height request of horizontal box)
 *
 * This operation requires a virtual allocation in the natural
 * orientation of the box, after that each element must be asked
 * for the size-for-virtually-allocated-size and the maximums of
 * each child sample will be reported as the overall
 * "size-for-size-in-opposite-orientation"
 */
static void
get_preferred_size_for_opposite_orientation (ClutterBoxLayout   *self,
					     ClutterActor       *container,
					     gfloat              for_size,
					     gfloat             *min_size_p,
					     gfloat             *natural_size_p)
{
  ClutterLayoutManager *layout = CLUTTER_LAYOUT_MANAGER (self);
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);
  ClutterActor *child;
  ClutterActorIter iter;
  gint nvis_children = 0, n_extra_widgets = 0;
  gint nexpand_children = 0, i;
  RequestedSize *sizes;
  gfloat minimum, natural, size, extra = 0;
  ClutterOrientation opposite_orientation =
    priv->orientation == CLUTTER_ORIENTATION_HORIZONTAL
    ? CLUTTER_ORIENTATION_VERTICAL
    : CLUTTER_ORIENTATION_HORIZONTAL;

  minimum = natural = 0;

  count_expand_children (layout, container, &nvis_children, &nexpand_children);

  if (nvis_children < 1)
    {
      if (min_size_p)
	*min_size_p = 0;

      if (natural_size_p)
	*natural_size_p = 0;

      return;
    }

  /* First collect the requested sizes in the natural orientation of the box */
  sizes  = g_newa (RequestedSize, nvis_children);
  size   = for_size;

  i = 0;
  clutter_actor_iter_init (&iter, container);
  while (clutter_actor_iter_next (&iter, &child))
    {
      if (!clutter_actor_is_visible (child))
	continue;

      get_child_size (child, priv->orientation, -1,
		      &sizes[i].minimum_size,
		      &sizes[i].natural_size);

      size -= sizes[i].minimum_size;
      i++;
    }

  if (priv->is_homogeneous)
    {
      size            = for_size - (nvis_children - 1) * priv->spacing;
      extra           = size / nvis_children;
      n_extra_widgets = ((gint)size) % nvis_children;
    }
  else
    {
      size -= (nvis_children - 1) * priv->spacing;

      /* Bring children up to size first */
      if (isnormal (size) || size == 0)
        {
          size = distribute_natural_allocation (MAX (0, size),
                                                nvis_children,
                                                sizes);
        }
      else
        {
          g_critical ("Actor %s (%p) received the invalid "
                      "value %f as minimum/natural size\n",
                       G_OBJECT_TYPE_NAME (container), container, size);
          size = 0;
        }

      /* Calculate space which hasn't distributed yet,
       * and is available for expanding children.
       */
      if (nexpand_children > 0)
        {
          extra = size / nexpand_children;
          n_extra_widgets = ((gint)size) % nexpand_children;
        }
    }

  /* Distribute expand space to children */
  i = 0;
  clutter_actor_iter_init (&iter, container);
  while (clutter_actor_iter_next (&iter, &child))
    {
      /* If widget is not visible, skip it. */
      if (!clutter_actor_is_visible (child))
        continue;

      if (priv->is_homogeneous)
	{
	  sizes[i].minimum_size = extra;

          if (n_extra_widgets > 0)
            {
              sizes[i].minimum_size++;
              n_extra_widgets--;
            }
	}
      else
	{
          if (clutter_actor_needs_expand (child, priv->orientation))
            {
              sizes[i].minimum_size += extra;

              if (n_extra_widgets > 0)
                {
                  sizes[i].minimum_size++;
                  n_extra_widgets--;
                }
            }
	}
      i++;
    }

  /* Virtual allocation finished, now we can finally ask for the right size-for-size */
  i = 0;
  clutter_actor_iter_init (&iter, container);
  while (clutter_actor_iter_next (&iter, &child))
    {
      gfloat child_min = 0, child_nat = 0;

      if (!clutter_actor_is_visible (child))
        continue;

      get_child_size (child, opposite_orientation,
		      sizes[i].minimum_size,
		      &child_min, &child_nat);

      minimum = MAX (minimum, child_min);
      natural = MAX (natural, child_nat);

      i++;
    }

  if (min_size_p)
    *min_size_p = minimum;

  if (natural_size_p)
    *natural_size_p = natural;
}


static void
allocate_box_child (ClutterBoxLayout       *self,
                    ClutterActor           *container,
                    ClutterActor           *child,
                    ClutterActorBox        *child_box)
{
  CLUTTER_NOTE (LAYOUT, "Allocation for %s { %.2f, %.2f, %.2f, %.2f }",
                _clutter_actor_get_debug_name (child),
                child_box->x1, child_box->y1,
                child_box->x2 - child_box->x1,
                child_box->y2 - child_box->y1);

  clutter_actor_allocate (child, child_box);
}

static void
clutter_box_layout_get_preferred_width (ClutterLayoutManager *layout,
                                        ClutterActor         *container,
                                        gfloat                for_height,
                                        gfloat               *min_width_p,
                                        gfloat               *natural_width_p)
{
  ClutterBoxLayout        *self = CLUTTER_BOX_LAYOUT (layout);
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);

  if (priv->orientation == CLUTTER_ORIENTATION_VERTICAL)
    {
      if (for_height < 0)
	get_base_size_for_opposite_orientation (self, CLUTTER_ACTOR (container),
						min_width_p, natural_width_p);
      else
	get_preferred_size_for_opposite_orientation (self, CLUTTER_ACTOR (container), for_height,
						     min_width_p, natural_width_p);
    }
  else
    get_preferred_size_for_orientation (self, CLUTTER_ACTOR (container), for_height,
					min_width_p, natural_width_p);
}

static void
clutter_box_layout_get_preferred_height (ClutterLayoutManager *layout,
                                         ClutterActor         *container,
                                         gfloat                for_width,
                                         gfloat               *min_height_p,
                                         gfloat               *natural_height_p)
{
  ClutterBoxLayout        *self = CLUTTER_BOX_LAYOUT (layout);
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);

  if (priv->orientation == CLUTTER_ORIENTATION_HORIZONTAL)
    {
      if (for_width < 0)
	get_base_size_for_opposite_orientation (self, CLUTTER_ACTOR (container),
						min_height_p, natural_height_p);
      else
	get_preferred_size_for_opposite_orientation (self, CLUTTER_ACTOR (container), for_width,
						     min_height_p, natural_height_p);
    }
  else
    get_preferred_size_for_orientation (self, CLUTTER_ACTOR (container), for_width,
					min_height_p, natural_height_p);
}

static void
count_expand_children (ClutterLayoutManager *layout,
                       ClutterActor         *container,
                       gint                 *visible_children,
                       gint                 *expand_children)
{
  ClutterBoxLayout *self = CLUTTER_BOX_LAYOUT (layout);
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);
  ClutterActor *child;
  ClutterActorIter iter;

  *visible_children = *expand_children = 0;

  clutter_actor_iter_init (&iter, container);
  while (clutter_actor_iter_next (&iter, &child))
    {
      if (clutter_actor_is_visible (child))
        {
          *visible_children += 1;

          if (clutter_actor_needs_expand (child, priv->orientation))
            *expand_children += 1;
        }
    }
}

/* Pulled from gtksizerequest.c from Gtk+ */
static gint
compare_gap (gconstpointer p1,
             gconstpointer p2,
             gpointer      data)
{
  RequestedSize *sizes = data;
  const guint *c1 = p1;
  const guint *c2 = p2;

  const int d1 = (int) MAX (sizes[*c1].natural_size -
                            sizes[*c1].minimum_size,
                            0);
  const int d2 = (int) MAX (sizes[*c2].natural_size -
                            sizes[*c2].minimum_size,
                            0);

  gint delta = (d2 - d1);

  if (0 == delta)
    delta = (*c2 - *c1);

  return delta;
}

/*
 * distribute_natural_allocation:
 * @extra_space: Extra space to redistribute among children after subtracting
 *   minimum sizes and any child padding from the overall allocation
 * @n_requested_sizes: Number of requests to fit into the allocation
 * @sizes: An array of structs with a client pointer and a minimum/natural size
 *   in the orientation of the allocation.
 *
 * Distributes @extra_space to child @sizes by bringing smaller
 * children up to natural size first.
 *
 * The remaining space will be added to the @minimum_size member of the
 * RequestedSize struct. If all sizes reach their natural size then
 * the remaining space is returned.
 *
 * Returns: The remainder of @extra_space after redistributing space
 * to @sizes.
 *
 * Pulled from gtksizerequest.c from Gtk+
 */
static float
distribute_natural_allocation (float          extra_space,
                               unsigned int   n_requested_sizes,
                               RequestedSize *sizes)
{
  unsigned int *spreading;
  int i;

  g_return_val_if_fail (isnormal (extra_space) || extra_space == 0, 0);
  g_return_val_if_fail (extra_space >= 0, 0);

  spreading = g_newa (unsigned int, n_requested_sizes);

  for (i = 0; i < n_requested_sizes; i++)
    spreading[i] = i;

  /* Distribute the container's extra space c_gap. We want to assign
   * this space such that the sum of extra space assigned to children
   * (c^i_gap) is equal to c_cap. The case that there's not enough
   * space for all children to take their natural size needs some
   * attention. The goals we want to achieve are:
   *
   *   a) Maximize number of children taking their natural size.
   *   b) The allocated size of children should be a continuous
   *   function of c_gap.  That is, increasing the container size by
   *   one pixel should never make drastic changes in the distribution.
   *   c) If child i takes its natural size and child j doesn't,
   *   child j should have received at least as much gap as child i.
   *
   * The following code distributes the additional space by following
   * these rules.
   */

  /* Sort descending by gap and position. */
  g_sort_array (spreading,
                n_requested_sizes, sizeof (unsigned int),
                compare_gap, sizes);

  /* Distribute available space.
   * This master piece of a loop was conceived by Behdad Esfahbod.
   */
  for (i = n_requested_sizes - 1; extra_space > 0.0 && i >= 0; --i)
    {
      /* Divide remaining space by number of remaining children.
       * Sort order and reducing remaining space by assigned space
       * ensures that space is distributed equally.
       */
      float glue = (extra_space + i) / (i + 1.0f);
      float gap =
        sizes[(spreading[i])].natural_size - sizes[(spreading[i])].minimum_size;

      float extra = MIN (glue, gap);

      sizes[spreading[i]].minimum_size += extra;

      extra_space -= extra;
    }

  return extra_space;
}

/* Pulled from gtkbox.c from Gtk+ */

static void
clutter_box_layout_allocate (ClutterLayoutManager   *layout,
                             ClutterActor           *container,
                             const ClutterActorBox  *box)
{
  ClutterBoxLayout *self = CLUTTER_BOX_LAYOUT (layout);
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);
  ClutterActor *actor, *child;
  gint nvis_children;
  gint nexpand_children;
  gboolean is_rtl;
  ClutterActorIter iter;

  ClutterActorBox child_allocation;
  RequestedSize *sizes;

  gint size;
  gint extra;
  gint n_extra_widgets = 0; /* Number of widgets that receive 1 extra px */
  gint x = 0, y = 0, i;
  gfloat child_size;

  count_expand_children (layout, container, &nvis_children, &nexpand_children);

  CLUTTER_NOTE (LAYOUT, "BoxLayout for %s: visible=%d, expand=%d",
                _clutter_actor_get_debug_name (CLUTTER_ACTOR (container)),
                nvis_children,
                nexpand_children);

  /* If there is no visible child, simply return. */
  if (nvis_children <= 0)
    return;

  sizes = g_newa (RequestedSize, nvis_children);

  if (priv->orientation == CLUTTER_ORIENTATION_VERTICAL)
    size = (int) (box->y2 - box->y1 - (nvis_children - 1) * priv->spacing);
  else
    size = (int) (box->x2 - box->x1 - (nvis_children - 1) * priv->spacing);

  actor = CLUTTER_ACTOR (container);

  /* Retrieve desired size for visible children. */
  i = 0;
  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      if (!clutter_actor_is_visible (child))
        continue;

      if (priv->orientation == CLUTTER_ORIENTATION_VERTICAL)
        clutter_actor_get_preferred_height (child,
                                            box->x2 - box->x1,
                                            &sizes[i].minimum_size,
                                            &sizes[i].natural_size);
      else
        clutter_actor_get_preferred_width (child,
                                           box->y2 - box->y1,
                                           &sizes[i].minimum_size,
                                           &sizes[i].natural_size);


      /* Assert the api is working properly */
      if (sizes[i].minimum_size < 0)
        g_error ("ClutterBoxLayout child %s minimum %s: %f < 0 for %s %f",
                 _clutter_actor_get_debug_name (child),
                 priv->orientation == CLUTTER_ORIENTATION_VERTICAL
                   ? "height"
                   : "width",
                 sizes[i].minimum_size,
                 priv->orientation == CLUTTER_ORIENTATION_VERTICAL
                   ? "width"
                   : "height",
                 priv->orientation == CLUTTER_ORIENTATION_VERTICAL
                   ? box->x2 - box->x1
                   : box->y2 - box->y1);

      if (sizes[i].natural_size < sizes[i].minimum_size)
        g_error ("ClutterBoxLayout child %s natural %s: %f < minimum %f for %s %f",
                 _clutter_actor_get_debug_name (child),
                 priv->orientation == CLUTTER_ORIENTATION_VERTICAL
                   ? "height"
                   : "width",
                 sizes[i].natural_size,
                 sizes[i].minimum_size,
                 priv->orientation == CLUTTER_ORIENTATION_VERTICAL
                   ? "width"
                   : "height",
                 priv->orientation == CLUTTER_ORIENTATION_VERTICAL
                   ? box->x2 - box->x1
                   : box->y2 - box->y1);

      size -= (int) sizes[i].minimum_size;

      sizes[i].actor = child;

      i += 1;
    }

  if (priv->is_homogeneous)
    {
      /* If were homogeneous we still need to run the above loop to get the
       * minimum sizes for children that are not going to fill
       */
      if (priv->orientation == CLUTTER_ORIENTATION_VERTICAL)
        size = (int) (box->y2 - box->y1 - (nvis_children - 1) * priv->spacing);
      else
        size = (int) (box->x2 - box->x1 - (nvis_children - 1) * priv->spacing);

      extra = size / nvis_children;
      n_extra_widgets = size % nvis_children;
    }
  else
    {
      /* Bring children up to size first */
      size = (gint) distribute_natural_allocation (MAX (0, (float) size),
                                                   nvis_children,
                                                   sizes);

      /* Calculate space which hasn't distributed yet,
       * and is available for expanding children.
       */
      if (nexpand_children > 0)
        {
          extra = size / nexpand_children;
          n_extra_widgets = size % nexpand_children;
        }
      else
        extra = 0;
    }

  if (priv->orientation == CLUTTER_ORIENTATION_HORIZONTAL)
    {
      ClutterTextDirection text_dir;

      text_dir = clutter_actor_get_text_direction (CLUTTER_ACTOR (container));
      is_rtl = (text_dir == CLUTTER_TEXT_DIRECTION_RTL) ? TRUE : FALSE;
    }
  else
    is_rtl = FALSE;

  /* Allocate child positions. */
  if (priv->orientation == CLUTTER_ORIENTATION_VERTICAL)
    {
      child_allocation.x1 = box->x1;
      child_allocation.x2 = MAX (1.0f, box->x2);
      y = (int) box->y1;
    }
  else
    {
      child_allocation.y1 = box->y1;
      child_allocation.y2 = MAX (1.0f, box->y2);
      x = (int) box->x1;
    }

  i = 0;
  clutter_actor_iter_init (&iter, actor);
  while (clutter_actor_iter_next (&iter, &child))
    {
      /* If widget is not visible, skip it. */
      if (!clutter_actor_is_visible (child))
        continue;

      /* Assign the child's size. */
      if (priv->is_homogeneous)
        {
          child_size = extra;

          if (n_extra_widgets > 0)
            {
              child_size++;
              n_extra_widgets--;
            }
        }
      else
        {
          child_size = sizes[i].minimum_size;

          if (clutter_actor_needs_expand (child, priv->orientation))
            {
              child_size += extra;

              if (n_extra_widgets > 0)
                {
                  child_size++;
                  n_extra_widgets--;
                }
            }
        }

      /* Assign the child's position. */
      if (priv->orientation == CLUTTER_ORIENTATION_VERTICAL)
        {
          if (clutter_actor_needs_expand (child, priv->orientation))
            {
              child_allocation.y1 = y;
              child_allocation.y2 = child_allocation.y1 + MAX (1.0f, child_size);
            }
          else
            {
              child_allocation.y1 = y + (child_size - sizes[i].minimum_size) / 2;
              child_allocation.y2 = child_allocation.y1 + sizes[i].minimum_size;
            }

          y += (int) (child_size + priv->spacing);
        }
      else /* CLUTTER_ORIENTATION_HORIZONTAL */
        {
          if (clutter_actor_needs_expand (child, priv->orientation))
            {
              child_allocation.x1 = x;
              child_allocation.x2 = child_allocation.x1 + MAX (1.0f, child_size);
            }
          else
            {
              child_allocation.x1 = x + (child_size - sizes[i].minimum_size) / 2;
              child_allocation.x2 = child_allocation.x1 + sizes[i].minimum_size;
            }

          x += (int) (child_size + priv->spacing);

          if (is_rtl)
            {
              gfloat width = child_allocation.x2 - child_allocation.x1;

              child_allocation.x2 = box->x1 + (box->x2 - child_allocation.x1);
              child_allocation.x1 = child_allocation.x2 - width;
            }

        }

        allocate_box_child (CLUTTER_BOX_LAYOUT (layout),
                            container,
                            child,
                            &child_allocation);

        i += 1;
    }
}

static void
clutter_box_layout_set_property (GObject      *gobject,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  ClutterBoxLayout *self = CLUTTER_BOX_LAYOUT (gobject);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      clutter_box_layout_set_orientation (self, g_value_get_enum (value));
      break;

    case PROP_HOMOGENEOUS:
      clutter_box_layout_set_homogeneous (self, g_value_get_boolean (value));
      break;

    case PROP_SPACING:
      clutter_box_layout_set_spacing (self, g_value_get_uint (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_box_layout_get_property (GObject    *gobject,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  ClutterBoxLayout *self = CLUTTER_BOX_LAYOUT (gobject);
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      g_value_set_enum (value, priv->orientation);
      break;

    case PROP_HOMOGENEOUS:
      g_value_set_boolean (value, priv->is_homogeneous);
      break;

    case PROP_SPACING:
      g_value_set_uint (value, priv->spacing);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
clutter_box_layout_class_init (ClutterBoxLayoutClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterLayoutManagerClass *layout_class;

  layout_class = CLUTTER_LAYOUT_MANAGER_CLASS (klass);

  layout_class->get_preferred_width = clutter_box_layout_get_preferred_width;
  layout_class->get_preferred_height = clutter_box_layout_get_preferred_height;
  layout_class->allocate = clutter_box_layout_allocate;
  layout_class->set_container = clutter_box_layout_set_container;

  /**
   * ClutterBoxLayout:orientation:
   *
   * The orientation of the #ClutterBoxLayout, either horizontal
   * or vertical
   */
  obj_props[PROP_ORIENTATION] =
    g_param_spec_enum ("orientation", NULL, NULL,
                       CLUTTER_TYPE_ORIENTATION,
                       CLUTTER_ORIENTATION_HORIZONTAL,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  /**
   * ClutterBoxLayout:homogeneous:
   *
   * Whether the #ClutterBoxLayout should arrange its children
   * homogeneously, i.e. all children get the same size
   */
  obj_props[PROP_HOMOGENEOUS] =
    g_param_spec_boolean ("homogeneous", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  /**
   * ClutterBoxLayout:spacing:
   *
   * The spacing between children of the #ClutterBoxLayout, in pixels
   */
  obj_props[PROP_SPACING] =
    g_param_spec_uint ("spacing", NULL, NULL,
                       0, G_MAXUINT, 0,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  gobject_class->set_property = clutter_box_layout_set_property;
  gobject_class->get_property = clutter_box_layout_get_property;
  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);
}

static void
clutter_box_layout_init (ClutterBoxLayout *self)
{
  ClutterBoxLayoutPrivate *priv =
    clutter_box_layout_get_instance_private (self);

  priv->orientation = CLUTTER_ORIENTATION_HORIZONTAL;
  priv->is_homogeneous = FALSE;
  priv->spacing = 0;

  priv->easing_mode = CLUTTER_EASE_OUT_CUBIC;
  priv->easing_duration = 500;
}

/**
 * clutter_box_layout_new:
 *
 * Creates a new #ClutterBoxLayout layout manager
 *
 * Return value: the newly created #ClutterBoxLayout
 */
ClutterLayoutManager *
clutter_box_layout_new (void)
{
  return g_object_new (CLUTTER_TYPE_BOX_LAYOUT, NULL);
}

/**
 * clutter_box_layout_set_spacing:
 * @layout: a #ClutterBoxLayout
 * @spacing: the spacing between children of the layout, in pixels
 *
 * Sets the spacing between children of @layout
 */
void
clutter_box_layout_set_spacing (ClutterBoxLayout *layout,
                                guint             spacing)
{
  ClutterBoxLayoutPrivate *priv;

  g_return_if_fail (CLUTTER_IS_BOX_LAYOUT (layout));

  priv = clutter_box_layout_get_instance_private (layout);
  if (priv->spacing != spacing)
    {
      ClutterLayoutManager *manager;

      priv->spacing = spacing;

      manager = CLUTTER_LAYOUT_MANAGER (layout);

      clutter_layout_manager_layout_changed (manager);

      g_object_notify (G_OBJECT (layout), "spacing");
    }
}

/**
 * clutter_box_layout_get_spacing:
 * @layout: a #ClutterBoxLayout
 *
 * Retrieves the spacing set using [method@Clutter.BoxLayout.set_spacing]
 *
 * Return value: the spacing between children of the #ClutterBoxLayout
 */
guint
clutter_box_layout_get_spacing (ClutterBoxLayout *layout)
{
  ClutterBoxLayoutPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_BOX_LAYOUT (layout), 0);

  priv = clutter_box_layout_get_instance_private (layout);
  return priv->spacing;
}

/**
 * clutter_box_layout_set_orientation:
 * @layout: a #ClutterBoxLayout
 * @orientation: the orientation of the #ClutterBoxLayout
 *
 * Sets the orientation of the #ClutterBoxLayout layout manager.
 */
void
clutter_box_layout_set_orientation (ClutterBoxLayout   *layout,
                                    ClutterOrientation  orientation)
{
  ClutterLayoutManager *manager;
  ClutterBoxLayoutPrivate *priv;

  g_return_if_fail (CLUTTER_IS_BOX_LAYOUT (layout));

  priv = clutter_box_layout_get_instance_private (layout);
  if (priv->orientation == orientation)
    return;

  priv->orientation = orientation;

  if (priv->container != NULL)
    {
      clutter_actor_set_request_mode (CLUTTER_ACTOR (priv->container),
                                      orientation == CLUTTER_ORIENTATION_VERTICAL
                                        ? CLUTTER_REQUEST_HEIGHT_FOR_WIDTH
                                        : CLUTTER_REQUEST_WIDTH_FOR_HEIGHT);
    }

  manager = CLUTTER_LAYOUT_MANAGER (layout);

  clutter_layout_manager_layout_changed (manager);

  g_object_notify_by_pspec (G_OBJECT (layout), obj_props[PROP_ORIENTATION]);
}

/**
 * clutter_box_layout_get_orientation:
 * @layout: a #ClutterBoxLayout
 *
 * Retrieves the orientation of the @layout.
 *
 * Return value: the orientation of the layout
 */
ClutterOrientation
clutter_box_layout_get_orientation (ClutterBoxLayout *layout)
{
  ClutterBoxLayoutPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_BOX_LAYOUT (layout),
                        CLUTTER_ORIENTATION_HORIZONTAL);

  priv = clutter_box_layout_get_instance_private (layout);
  return priv->orientation;
}

/**
 * clutter_box_layout_set_homogeneous:
 * @layout: a #ClutterBoxLayout
 * @homogeneous: %TRUE if the layout should be homogeneous
 *
 * Sets whether the size of @layout children should be
 * homogeneous
 */
void
clutter_box_layout_set_homogeneous (ClutterBoxLayout *layout,
				                            gboolean          homogeneous)
{
  ClutterBoxLayoutPrivate *priv;

  g_return_if_fail (CLUTTER_IS_BOX_LAYOUT (layout));

  priv = clutter_box_layout_get_instance_private (layout);
  if (priv->is_homogeneous != homogeneous)
    {
      ClutterLayoutManager *manager;

      priv->is_homogeneous = !!homogeneous;

      manager = CLUTTER_LAYOUT_MANAGER (layout);

      clutter_layout_manager_layout_changed (manager);

      g_object_notify (G_OBJECT (layout), "homogeneous");
    }
}

/**
 * clutter_box_layout_get_homogeneous:
 * @layout: a #ClutterBoxLayout
 *
 * Retrieves if the children sizes are allocated homogeneously.
 *
 * Return value: %TRUE if the #ClutterBoxLayout is arranging its children
 *   homogeneously, and %FALSE otherwise
 */
gboolean
clutter_box_layout_get_homogeneous (ClutterBoxLayout *layout)
{
  ClutterBoxLayoutPrivate *priv;

  g_return_val_if_fail (CLUTTER_IS_BOX_LAYOUT (layout), FALSE);

  priv = clutter_box_layout_get_instance_private (layout);
  return priv->is_homogeneous;
}

