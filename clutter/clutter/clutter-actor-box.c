#include "config.h"

#include <math.h>

#include "clutter/clutter-types.h"
#include "clutter/clutter-interval.h"
#include "clutter/clutter-private.h"
#include "clutter/clutter-actor-box-private.h"

void
_clutter_actor_box_enlarge_for_effects (graphene_rect_t *box)
{
  float width, height;

  if (graphene_rect_get_area (box) == 0.0)
    return;

  /* The aim here is that for a given rectangle defined with floating point
   * coordinates we want to determine a stable quantized size in pixels
   * that doesn't vary due to the original box's sub-pixel position.
   *
   * The reason this is important is because effects will use this
   * API to determine the size of offscreen framebuffers and so for
   * a fixed-size object that may be animated across the screen we
   * want to make sure that the stage paint-box has an equally stable
   * size so that effects aren't made to continuously re-allocate
   * a corresponding fbo.
   *
   * The other thing we consider is that the calculation of this box is
   * subject to floating point precision issues that might be slightly
   * different to the precision issues involved with actually painting the
   * actor, which might result in painting slightly leaking outside the
   * user's calculated paint-volume. For this we simply aim to pad out the
   * paint-volume by at least half a pixel all the way around.
   */
  width = box->size.width;
  height = box->size.height;
  width = CLUTTER_NEARBYINT (width);
  height = CLUTTER_NEARBYINT (height);
  /* XXX: NB the width/height may now be up to 0.5px too small so we
   * must also pad by 0.25px all around to account for this. In total we
   * must padd by at least 0.75px around all sides. */

  /* XXX: The furthest that we can overshoot the bottom right corner by
   * here is 1.75px in total if you consider that the 0.75 padding could
   * just cross an integer boundary and so ceil will effectively add 1.
   */
  graphene_rect_inset (box, 0.75, 0.75);

  /* Now we redefine the top-left relative to the bottom right based on the
   * rounded width/height determined above + a constant so that the overall
   * size of the box will be stable and not dependent on the box's
   * position.
   *
   * Adding 3px to the width/height will ensure we cover the maximum of
   * 1.75px padding on the bottom/right and still ensure we have > 0.75px
   * padding on the top/left.
   */
  box->origin.x = box->size.width - width - 3;
  box->origin.y = box->size.height - height - 3;
}
