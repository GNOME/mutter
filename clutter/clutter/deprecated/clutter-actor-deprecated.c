#include "clutter-build-config.h"

#include <glib-object.h>

#define CLUTTER_DISABLE_DEPRECATION_WARNINGS
#include "deprecated/clutter-actor.h"

#include "clutter-actor-private.h"
#include "clutter-private.h"

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
