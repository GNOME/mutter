#include "clutter-build-config.h"

#include <glib-object.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include "deprecated/clutter-actor.h"

#include "clutter-actor-private.h"
#include "clutter-private.h"

/**
 * clutter_actor_set_geometry:
 * @self: A #ClutterActor
 * @geometry: A #ClutterGeometry
 *
 * Sets the actor's fixed position and forces its minimum and natural
 * size, in pixels. This means the untransformed actor will have the
 * given geometry. This is the same as calling clutter_actor_set_position()
 * and clutter_actor_set_size().
 *
 * Deprecated: 1.10: Use clutter_actor_set_position() and
 *   clutter_actor_set_size() instead.
 */
void
clutter_actor_set_geometry (ClutterActor          *self,
			    const ClutterGeometry *geometry)
{
  g_object_freeze_notify (G_OBJECT (self));

  clutter_actor_set_position (self, geometry->x, geometry->y);
  clutter_actor_set_size (self, geometry->width, geometry->height);

  g_object_thaw_notify (G_OBJECT (self));
}

/**
 * clutter_actor_get_geometry:
 * @self: A #ClutterActor
 * @geometry: (out caller-allocates): A location to store actors #ClutterGeometry
 *
 * Gets the size and position of an actor relative to its parent
 * actor. This is the same as calling clutter_actor_get_position() and
 * clutter_actor_get_size(). It tries to "do what you mean" and get the
 * requested size and position if the actor's allocation is invalid.
 *
 * Deprecated: 1.10: Use clutter_actor_get_position() and
 *   clutter_actor_get_size(), or clutter_actor_get_allocation_geometry()
 *   instead.
 */
void
clutter_actor_get_geometry (ClutterActor    *self,
			    ClutterGeometry *geometry)
{
  gfloat x, y, width, height;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (geometry != NULL);

  clutter_actor_get_position (self, &x, &y);
  clutter_actor_get_size (self, &width, &height);

  geometry->x = (int) x;
  geometry->y = (int) y;
  geometry->width = (int) width;
  geometry->height = (int) height;
}

/**
 * clutter_actor_get_allocation_geometry:
 * @self: A #ClutterActor
 * @geom: (out): allocation geometry in pixels
 *
 * Gets the layout box an actor has been assigned.  The allocation can
 * only be assumed valid inside a paint() method; anywhere else, it
 * may be out-of-date.
 *
 * An allocation does not incorporate the actor's scale or anchor point;
 * those transformations do not affect layout, only rendering.
 *
 * The returned rectangle is in pixels.
 *
 * Since: 0.8
 *
 * Deprecated: 1.12: Use clutter_actor_get_allocation_box() instead.
 */
void
clutter_actor_get_allocation_geometry (ClutterActor    *self,
                                       ClutterGeometry *geom)
{
  ClutterActorBox box;

  g_return_if_fail (CLUTTER_IS_ACTOR (self));
  g_return_if_fail (geom != NULL);

  clutter_actor_get_allocation_box (self, &box);

  geom->x = CLUTTER_NEARBYINT (clutter_actor_box_get_x (&box));
  geom->y = CLUTTER_NEARBYINT (clutter_actor_box_get_y (&box));
  geom->width = CLUTTER_NEARBYINT (clutter_actor_box_get_width (&box));
  geom->height = CLUTTER_NEARBYINT (clutter_actor_box_get_height (&box));
}
