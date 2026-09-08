#include <stdlib.h>
#include <string.h>

#include <clutter/clutter-mutter.h>

#include "tests/clutter-test-utils.h"

static void
on_presented (ClutterStage     *stage,
              ClutterStageView *view,
              ClutterFrameInfo *frame_info,
              gboolean         *was_presented)
{
  *was_presented = TRUE;
}

static void
actor_clone_unmapped (void)
{
  ClutterActor *container;
  ClutterActor *actor;
  ClutterActor *clone;
  ClutterActor *stage;
  gulong presented_handler_id;
  gboolean was_presented;

  stage = clutter_test_get_stage ();

  container = clutter_actor_new ();
  g_object_add_weak_pointer (G_OBJECT (container), (gpointer *) &container);

  actor = clutter_actor_new ();
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);

  clone = clutter_clone_new (actor);
  g_object_add_weak_pointer (G_OBJECT (clone), (gpointer *) &clone);

  clutter_actor_hide (container);
  clutter_actor_hide (actor);

  clutter_actor_add_child (stage, container);
  clutter_actor_add_child (container, actor);
  clutter_actor_add_child (stage, clone);

  clutter_actor_set_offscreen_redirect (actor, CLUTTER_OFFSCREEN_REDIRECT_ALWAYS);

  presented_handler_id =
    g_signal_connect (stage, "presented", G_CALLBACK (on_presented),
                      &was_presented);

  clutter_actor_show (stage);

  was_presented = FALSE;
  while (!was_presented)
    g_main_context_iteration (NULL, FALSE);

  g_signal_handler_disconnect (stage, presented_handler_id);

  clutter_actor_destroy (clone);
  clutter_actor_destroy (actor);
  clutter_actor_destroy (container);
  g_assert_null (clone);
  g_assert_null (actor);
  g_assert_null (container);
}

typedef struct
{
  ClutterActor *source;
  ClutterActor *clone;
  int count;
} CloneForeachData;

static void
on_mapped_clone (ClutterActor *source,
                 ClutterActor *clone,
                 gpointer      user_data)
{
  CloneForeachData *data = user_data;

  g_assert_true (source == data->source);
  g_assert_true (clone == data->clone);
  data->count++;
}

static void
actor_clone_foreach_mapped (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterActor *source = clutter_actor_new ();
  ClutterActor *descendant = clutter_actor_new ();
  ClutterActor *clone = clutter_clone_new (source);
  CloneForeachData data = {
    .source = source,
    .clone = clone,
  };
  gulong presented_handler_id;
  gboolean was_presented = FALSE;

  clutter_actor_add_child (source, descendant);
  clutter_actor_hide (source);
  clutter_actor_add_child (stage, source);
  clutter_actor_add_child (stage, clone);

  presented_handler_id =
    g_signal_connect (stage, "presented", G_CALLBACK (on_presented),
                      &was_presented);
  clutter_actor_show (stage);

  while (!was_presented)
    g_main_context_iteration (NULL, FALSE);

  g_signal_handler_disconnect (stage, presented_handler_id);

  clutter_actor_foreach_mapped_clone (descendant, on_mapped_clone, &data);
  g_assert_cmpint (data.count, ==, 1);

  clutter_actor_destroy (clone);
  clutter_actor_destroy (source);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/clone/unmapped", actor_clone_unmapped)
  CLUTTER_TEST_UNIT ("/actor/clone/foreach-mapped", actor_clone_foreach_mapped)
)
