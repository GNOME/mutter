#include <stdlib.h>
#include <string.h>

#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static void
actor_pivot (void)
{
  ClutterActor *stage, *actor_implicit, *actor_explicit;
  graphene_matrix_t transform, result_implicit, result_explicit;
  ClutterActorBox allocation = CLUTTER_ACTOR_BOX_INIT (0, 0, 90, 30);
  gfloat angle = 30;

  stage = clutter_test_get_stage ();

  actor_implicit = clutter_actor_new ();
  actor_explicit = clutter_actor_new ();

  clutter_actor_add_child (stage, actor_implicit);
  clutter_actor_add_child (stage, actor_explicit);

  clutter_actor_show (stage);

  /* Fake allocation or pivot-point will not have any effect */
  clutter_actor_allocate (actor_implicit, &allocation);
  clutter_actor_allocate (actor_explicit, &allocation);

  clutter_actor_set_pivot_point (actor_implicit, 0.5, 0.5);
  clutter_actor_set_pivot_point (actor_explicit, 0.5, 0.5);

  /* Implicit transformation */
  clutter_actor_set_rotation_angle (actor_implicit, CLUTTER_Z_AXIS, angle);

  /* Explicit transformation */
  graphene_matrix_init_rotate (&transform, angle, graphene_vec3_z_axis ());
  clutter_actor_set_transform (actor_explicit, &transform);

  clutter_actor_get_transform (actor_implicit, &result_implicit);
  clutter_actor_get_transform (actor_explicit, &result_explicit);

  g_assert (graphene_matrix_equal (&result_implicit, &result_explicit));

  clutter_actor_destroy (actor_implicit);
  clutter_actor_destroy (actor_explicit);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/transforms/pivot-point", actor_pivot)
)
