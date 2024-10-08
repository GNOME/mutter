#include <stdlib.h>
#include <string.h>

#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

static void
actor_initial_state (void)
{
  ClutterActor *actor;

  actor = clutter_actor_new ();
  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  if (!g_test_quiet ())
    g_print ("initial state - visible: %s, realized: %s, mapped: %s\n",
             clutter_actor_is_visible (actor) ? "yes" : "no",
             clutter_actor_is_realized (actor) ? "yes" : "no",
             clutter_actor_is_mapped (actor) ? "yes" : "no");

  g_assert_false ((clutter_actor_is_realized (actor)));
  g_assert_false ((clutter_actor_is_mapped (actor)));
  g_assert_false ((clutter_actor_is_visible (actor)));

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
actor_shown_not_parented (void)
{
  ClutterActor *actor;

  actor = clutter_actor_new ();
  g_object_ref_sink (actor);
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  clutter_actor_show (actor);

  if (!g_test_quiet ())
    g_print ("show without a parent - visible: %s, realized: %s, mapped: %s\n",
             clutter_actor_is_visible (actor) ? "yes" : "no",
             clutter_actor_is_realized (actor) ? "yes" : "no",
             clutter_actor_is_mapped (actor) ? "yes" : "no");

  g_assert_false (clutter_actor_is_realized (actor));
  g_assert_false (clutter_actor_is_mapped (actor));
  g_assert_true (clutter_actor_is_visible (actor));

  clutter_actor_destroy (actor);
  g_assert_null (actor);
}

static void
actor_realized (void)
{
  ClutterActor *actor;
  ClutterActor *stage;

  stage = clutter_test_get_stage ();

  actor = clutter_actor_new ();

  g_assert_false ((clutter_actor_is_realized (actor)));

  clutter_actor_hide (actor); /* don't show, so won't map */
  clutter_actor_add_child (stage, actor);
  clutter_actor_realize (actor);

  g_assert_true (clutter_actor_is_realized (actor));

  g_assert_false ((clutter_actor_is_mapped (actor)));
  g_assert_false ((clutter_actor_is_visible (actor)));

  clutter_actor_destroy (actor);
}

static void
actor_mapped (void)
{
  ClutterActor *actor;
  ClutterActor *stage;

  stage = clutter_test_get_stage ();
  clutter_actor_show (stage);

  actor = clutter_actor_new ();

  g_assert_false ((clutter_actor_is_realized (actor)));
  g_assert_false ((clutter_actor_is_mapped (actor)));

  clutter_actor_add_child (stage, actor);

  if (!g_test_quiet ())
    g_print ("adding to a container should map - "
             "visible: %s, realized: %s, mapped: %s\n",
             clutter_actor_is_visible (actor) ? "yes" : "no",
             clutter_actor_is_realized (actor) ? "yes" : "no",
             clutter_actor_is_mapped (actor) ? "yes" : "no");

  g_assert_true (clutter_actor_is_realized (actor));
  g_assert_true (clutter_actor_is_mapped (actor));
  g_assert_true (clutter_actor_is_visible (actor));

  clutter_actor_hide (actor);

  if (!g_test_quiet ())
    g_print ("hiding should unmap - "
             "visible: %s, realized: %s, mapped: %s\n",
             clutter_actor_is_visible (actor) ? "yes" : "no",
             clutter_actor_is_realized (actor) ? "yes" : "no",
             clutter_actor_is_mapped (actor) ? "yes" : "no");

  g_assert_true (clutter_actor_is_realized (actor));
  g_assert_false (clutter_actor_is_mapped (actor));
  g_assert_false (clutter_actor_is_visible (actor));

  clutter_actor_destroy (actor);
}

static void
actor_visibility_not_recursive (void)
{
  ClutterActor *actor, *group;
  ClutterActor *stage;

  stage = clutter_test_get_stage ();

  group = clutter_actor_new ();
  actor = clutter_actor_new ();

  clutter_actor_hide (group); /* don't show, so won't map */
  clutter_actor_hide (actor); /* don't show, so won't map */

  g_assert_false ((clutter_actor_is_visible (stage)));
  g_assert_false ((clutter_actor_is_visible (group)));
  g_assert_false ((clutter_actor_is_visible (actor)));

  clutter_actor_add_child (stage, group);
  clutter_actor_add_child (group, actor);

  clutter_actor_show (actor);
  g_assert_true (clutter_actor_is_visible (actor));
  g_assert_false (clutter_actor_is_visible (group));
  g_assert_false (clutter_actor_is_visible (stage));

  clutter_actor_show (stage);
  g_assert_true (clutter_actor_is_visible (actor));
  g_assert_false (clutter_actor_is_visible (group));
  g_assert_true (clutter_actor_is_visible (stage));

  clutter_actor_hide (actor);
  clutter_actor_hide (group);
  clutter_actor_hide (stage);
  g_assert_false (clutter_actor_is_visible (actor));

  clutter_actor_show (stage);
  g_assert_false (clutter_actor_is_visible (actor));

  clutter_actor_destroy (actor);
  clutter_actor_destroy (group);
}

static void
actor_realize_not_recursive (void)
{
  ClutterActor *actor, *group;
  ClutterActor *stage;

  stage = clutter_test_get_stage ();
  clutter_actor_show (stage);

  group = clutter_actor_new ();

  actor = clutter_actor_new ();

  clutter_actor_hide (group); /* don't show, so won't map */
  clutter_actor_hide (actor); /* don't show, so won't map */

  g_assert_false ((clutter_actor_is_realized (group)));
  g_assert_false ((clutter_actor_is_realized (actor)));

  clutter_actor_add_child (stage, group);
  clutter_actor_add_child (group, actor);

  clutter_actor_realize (group);

  g_assert_true (clutter_actor_is_realized (group));

  g_assert_false ((clutter_actor_is_mapped (group)));
  g_assert_false ((clutter_actor_is_visible (group)));

  /* realizing group did not realize the child */
  g_assert_false (clutter_actor_is_realized (actor));
  g_assert_false ((clutter_actor_is_mapped (actor)));
  g_assert_false ((clutter_actor_is_visible (actor)));

  clutter_actor_destroy (actor);
  clutter_actor_destroy (group);
}

static void
actor_map_recursive (void)
{
  ClutterActor *actor, *group;
  ClutterActor *stage;

  stage = clutter_test_get_stage ();
  clutter_actor_show (stage);

  group = clutter_actor_new ();

  actor = clutter_actor_new ();

  clutter_actor_hide (group); /* hide at first */
  clutter_actor_show (actor); /* show at first */

  g_assert_false ((clutter_actor_is_realized (group)));
  g_assert_false ((clutter_actor_is_realized (actor)));
  g_assert_false ((clutter_actor_is_mapped (group)));
  g_assert_false ((clutter_actor_is_mapped (actor)));
  g_assert_false ((clutter_actor_is_visible (group)));
  g_assert_true ((clutter_actor_is_visible (actor)));

  clutter_actor_add_child (stage, group);
  clutter_actor_add_child (group, actor);

  g_assert_false ((clutter_actor_is_realized (group)));
  g_assert_false ((clutter_actor_is_realized (actor)));
  g_assert_false ((clutter_actor_is_mapped (group)));
  g_assert_false ((clutter_actor_is_mapped (actor)));
  g_assert_false ((clutter_actor_is_visible (group)));
  g_assert_true ((clutter_actor_is_visible (actor)));

  /* show group, which should map and realize both
   * group and child.
   */
  clutter_actor_show (group);
  g_assert_true (clutter_actor_is_realized (group));
  g_assert_true (clutter_actor_is_realized (actor));
  g_assert_true (clutter_actor_is_mapped (group));
  g_assert_true (clutter_actor_is_mapped (actor));
  g_assert_true (clutter_actor_is_visible (group));
  g_assert_true (clutter_actor_is_visible (actor));

  clutter_actor_destroy (actor);
  clutter_actor_destroy (group);
}

static void
actor_show_on_set_parent (void)
{
  ClutterActor *actor, *group;
  gboolean show_on_set_parent;
  ClutterActor *stage;

  stage = clutter_test_get_stage ();

  group = clutter_actor_new ();

  g_assert_false ((clutter_actor_is_visible (group)));

  clutter_actor_add_child (stage, group);

  actor = clutter_actor_new ();
  g_object_get (actor,
                "show-on-set-parent", &show_on_set_parent,
                NULL);

  g_assert_false ((clutter_actor_is_visible (actor)));
  g_assert_true (show_on_set_parent);

  clutter_actor_add_child (group, actor);
  g_object_get (actor,
                "show-on-set-parent", &show_on_set_parent,
                NULL);

  g_assert_true (clutter_actor_is_visible (actor));
  g_assert_true (show_on_set_parent);

  g_object_ref (actor);
  clutter_actor_remove_child (group, actor);
  g_object_get (actor,
                "show-on-set-parent", &show_on_set_parent,
                NULL);

  g_assert_false (clutter_actor_is_realized (actor));
  g_assert_false (clutter_actor_is_mapped (actor));
  g_assert_true (clutter_actor_is_visible (actor));
  g_assert_true (show_on_set_parent);

  clutter_actor_destroy (actor);
  clutter_actor_destroy (group);

  actor = clutter_actor_new ();
  clutter_actor_add_child (stage, actor);
  clutter_actor_hide (actor);
  g_object_get (actor,
                "show-on-set-parent", &show_on_set_parent,
                NULL);
  g_assert_false (clutter_actor_is_visible (actor));
  g_assert_false (clutter_actor_is_mapped (actor));
  g_assert_true (show_on_set_parent);

  clutter_actor_destroy (actor);

  actor = clutter_actor_new ();
  clutter_actor_hide (actor);
  clutter_actor_add_child (stage, actor);
  g_object_get (actor,
                "show-on-set-parent", &show_on_set_parent,
                NULL);
  g_assert_false (clutter_actor_is_visible (actor));
  g_assert_false (clutter_actor_is_mapped (actor));
  g_assert_false (show_on_set_parent);

  clutter_actor_destroy (actor);
}

static void
clone_no_map (void)
{
  ClutterActor *stage;
  ClutterActor *group;
  ClutterActor *actor;
  ClutterActor *clone;

  stage = clutter_test_get_stage ();
  clutter_actor_show (stage);

  group = clutter_actor_new ();
  actor = clutter_actor_new ();

  clutter_actor_hide (group);

  clutter_actor_add_child (group, actor);
  clutter_actor_add_child (stage, group);

  g_assert_false ((clutter_actor_is_mapped (group)));
  g_assert_false ((clutter_actor_is_mapped (actor)));

  clone = clutter_clone_new (group);

  clutter_actor_add_child (stage, clone);

  g_assert_true (clutter_actor_is_mapped (clone));
  g_assert_false ((clutter_actor_is_mapped (group)));
  g_assert_false ((clutter_actor_is_mapped (actor)));

  clutter_actor_destroy (actor);
  clutter_actor_destroy (CLUTTER_ACTOR (clone));
  clutter_actor_destroy (CLUTTER_ACTOR (group));
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/invariants/initial-state", actor_initial_state)
  CLUTTER_TEST_UNIT ("/actor/invariants/show-not-parented", actor_shown_not_parented)
  CLUTTER_TEST_UNIT ("/actor/invariants/realized", actor_realized)
  CLUTTER_TEST_UNIT ("/actor/invariants/mapped", actor_mapped)
  CLUTTER_TEST_UNIT ("/actor/invariants/visibility-not-recursive", actor_visibility_not_recursive)
  CLUTTER_TEST_UNIT ("/actor/invariants/realize-not-recursive", actor_realize_not_recursive)
  CLUTTER_TEST_UNIT ("/actor/invariants/map-recursive", actor_map_recursive)
  CLUTTER_TEST_UNIT ("/actor/invariants/show-on-set-parent", actor_show_on_set_parent)
  CLUTTER_TEST_UNIT ("/actor/invariants/clone-no-map", clone_no_map)
)
