#include <stdlib.h>
#include <string.h>

#include <clutter/clutter-mutter.h>

#include "tests/clutter-test-utils.h"

#define TEST_TYPE_PAINT_TRANSFORM_ACTOR (test_paint_transform_actor_get_type ())
G_DECLARE_FINAL_TYPE (TestPaintTransformActor,
                      test_paint_transform_actor,
                      TEST,
                      PAINT_TRANSFORM_ACTOR,
                      ClutterActor)

struct _TestPaintTransformActor
{
  ClutterActor parent_instance;

  ClutterActor *clone;
  graphene_point3d_t origin_in_clone;
  gboolean was_painted;
};

G_DEFINE_TYPE (TestPaintTransformActor,
               test_paint_transform_actor,
               CLUTTER_TYPE_ACTOR)

static void
test_paint_transform_actor_paint (ClutterActor        *actor,
                                  ClutterPaintContext *paint_context)
{
  TestPaintTransformActor *test_actor = TEST_PAINT_TRANSFORM_ACTOR (actor);
  graphene_matrix_t transform;
  graphene_matrix_t clone_to_eye;
  graphene_point3d_t origin = GRAPHENE_POINT3D_INIT_ZERO;
  graphene_point3d_t transformed_origin;
  graphene_point3d_t expected_origin;

  clutter_actor_get_effective_eye_transformation_matrix (actor,
                                                         paint_context,
                                                         &transform);
  graphene_matrix_transform_point3d (&transform,
                                     &origin,
                                     &transformed_origin);
  clutter_actor_get_relative_transformation_matrix (test_actor->clone,
                                                    NULL,
                                                    &clone_to_eye);
  graphene_matrix_transform_point3d (&clone_to_eye,
                                     &test_actor->origin_in_clone,
                                     &expected_origin);

  g_assert_cmpfloat_with_epsilon (transformed_origin.x,
                                  expected_origin.x,
                                  0.001f);
  g_assert_cmpfloat_with_epsilon (transformed_origin.y,
                                  expected_origin.y,
                                  0.001f);
  test_actor->was_painted = TRUE;
}

static void
test_paint_transform_actor_class_init (TestPaintTransformActorClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  actor_class->paint = test_paint_transform_actor_paint;
}

static void
test_paint_transform_actor_init (TestPaintTransformActor *self)
{
}

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

static void
actor_clone_paint_transform (void)
{
  ClutterActor *stage = clutter_test_get_stage ();
  ClutterActor *source = clutter_actor_new ();
  TestPaintTransformActor *test_actor =
    g_object_new (TEST_TYPE_PAINT_TRANSFORM_ACTOR, NULL);
  ClutterActor *clone = clutter_clone_new (source);
  gulong presented_handler_id;
  gboolean was_presented = FALSE;

  test_actor->clone = clone;
  test_actor->origin_in_clone = GRAPHENE_POINT3D_INIT (10.0f, 21.0f, 0.0f);

  clutter_actor_set_position (source, 100.0f, 100.0f);
  clutter_actor_set_size (source, 100.0f, 100.0f);
  clutter_actor_set_position (CLUTTER_ACTOR (test_actor), 5.0f, 7.0f);
  clutter_actor_set_size (CLUTTER_ACTOR (test_actor), 20.0f, 30.0f);
  clutter_actor_add_child (source, CLUTTER_ACTOR (test_actor));
  clutter_actor_hide (source);
  clutter_actor_add_child (stage, source);

  clutter_actor_set_position (clone, 300.0f, 200.0f);
  clutter_actor_set_size (clone, 200.0f, 300.0f);
  clutter_actor_add_child (stage, clone);

  presented_handler_id =
    g_signal_connect (stage, "presented", G_CALLBACK (on_presented),
                      &was_presented);
  clutter_actor_show (stage);

  while (!was_presented)
    g_main_context_iteration (NULL, FALSE);

  g_signal_handler_disconnect (stage, presented_handler_id);
  g_assert_true (test_actor->was_painted);

  clutter_actor_destroy (clone);
  clutter_actor_destroy (source);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/actor/clone/unmapped", actor_clone_unmapped)
  CLUTTER_TEST_UNIT ("/actor/clone/foreach-mapped", actor_clone_foreach_mapped)
  CLUTTER_TEST_UNIT ("/actor/clone/paint-transform", actor_clone_paint_transform)
)
