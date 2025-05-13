#include <stdlib.h>
#include <string.h>

#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"
#include "cogl/cogl-mutter.h"

static void
assert_match_and_unref (CoglPipeline *pipeline,
                        CoglPipeline *expected_pipeline)
{
  g_assert_cmpstr (cogl_pipeline_get_name (pipeline),
                   ==,
                   cogl_pipeline_get_name (expected_pipeline));
  g_object_unref (pipeline);
}

static CoglPipeline *
create_test_pipeline (CoglContext *context,
                      const char  *name)
{
  CoglPipeline *pipeline;

  pipeline = cogl_pipeline_new (context);
  cogl_pipeline_set_static_name (pipeline, name);
  return pipeline;
}

static void
pipeline_cache_group_pipelines (void)
{
  ClutterContext *context = clutter_test_get_context ();
  ClutterBackend *backend = clutter_test_get_backend ();
  CoglContext *cogl_context = clutter_backend_get_cogl_context (backend);
  ClutterPipelineCache *pipeline_cache = clutter_context_get_pipeline_cache (context);
  static ClutterPipelineGroup group1 = &group1;
  static ClutterPipelineGroup group2 = &group2;
  ClutterColorState *srgb_srgb;
  ClutterColorState *srgb_linear;
  ClutterColorState *bt2020_pq;
  ClutterColorState *bt2020_linear;
  /* SDR content with HDR output */
  CoglPipeline *srgb_srgb_to_bt2020_linear;
  CoglPipeline *bt2020_linear_to_bt2020_pq;
  /* HDR content with HDR output */
  CoglPipeline *bt2020_pq_to_bt2020_linear;
  CoglPipeline *srgb_linear_to_srgb_srgb;
  /* Second pipeline for group2 */
  CoglPipeline *srgb_srgb_to_bt2020_linear_2;

  srgb_srgb = clutter_color_state_params_new (context,
                                              CLUTTER_COLORSPACE_SRGB,
                                              CLUTTER_TRANSFER_FUNCTION_SRGB);
  srgb_linear = clutter_color_state_params_new (context,
                                                CLUTTER_COLORSPACE_SRGB,
                                                CLUTTER_TRANSFER_FUNCTION_LINEAR);
  bt2020_pq = clutter_color_state_params_new (context,
                                              CLUTTER_COLORSPACE_BT2020,
                                              CLUTTER_TRANSFER_FUNCTION_PQ);
  bt2020_linear = clutter_color_state_params_new (context,
                                                  CLUTTER_COLORSPACE_BT2020,
                                                  CLUTTER_TRANSFER_FUNCTION_LINEAR);

  srgb_srgb_to_bt2020_linear = create_test_pipeline (cogl_context,
                                                     "srgb_srgb_to_bt2020_linear");
  bt2020_linear_to_bt2020_pq = create_test_pipeline (cogl_context,
                                                     "bt2020_linear_to_bt2020_pq");
  bt2020_pq_to_bt2020_linear = create_test_pipeline (cogl_context,
                                                     "bt2020_pq_to_bt2020_linear");
  srgb_linear_to_srgb_srgb = create_test_pipeline (cogl_context,
                                                   "srgb_linear_to_srgb_srgb");

  clutter_color_state_add_pipeline_transform (srgb_srgb,
                                              bt2020_linear,
                                              srgb_srgb_to_bt2020_linear,
                                              0);
  clutter_color_state_add_pipeline_transform (bt2020_linear,
                                              bt2020_pq,
                                              bt2020_linear_to_bt2020_pq,
                                              0);
  clutter_color_state_add_pipeline_transform (bt2020_pq,
                                              bt2020_linear,
                                              bt2020_pq_to_bt2020_linear,
                                              0);
  clutter_color_state_add_pipeline_transform (srgb_linear,
                                              srgb_srgb,
                                              srgb_linear_to_srgb_srgb,
                                              0);

  /* Check that it's all empty. */
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group1, 0,
                                                      srgb_srgb, bt2020_linear));
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group1, 0,
                                                      bt2020_linear, bt2020_pq));
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group2, 0,
                                                      srgb_srgb, bt2020_linear));
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group2, 0,
                                                      bt2020_linear, bt2020_pq));

  /* Adding sRGB to HDR pipeline to group1 should not effect group2. */
  clutter_pipeline_cache_set_pipeline (pipeline_cache, group1, 0,
                                       srgb_srgb, bt2020_linear,
                                       srgb_srgb_to_bt2020_linear);
  clutter_pipeline_cache_set_pipeline (pipeline_cache, group1, 0,
                                       bt2020_linear, bt2020_pq,
                                       bt2020_linear_to_bt2020_pq);

  assert_match_and_unref (clutter_pipeline_cache_get_pipeline (pipeline_cache,
                                                               group1, 0,
                                                               srgb_srgb,
                                                               bt2020_linear),
                          srgb_srgb_to_bt2020_linear);
  assert_match_and_unref (clutter_pipeline_cache_get_pipeline (pipeline_cache,
                                                               group1, 0,
                                                               bt2020_linear,
                                                               bt2020_pq),
                          bt2020_linear_to_bt2020_pq);
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group2, 0,
                                                      srgb_srgb, bt2020_linear));
  g_assert_null (clutter_pipeline_cache_get_pipeline (pipeline_cache, group2, 0,
                                                      bt2020_linear, bt2020_pq));

  srgb_srgb_to_bt2020_linear_2 =
    create_test_pipeline (cogl_context, "srgb_srgb_to_bt2020_linear_2");

  clutter_pipeline_cache_set_pipeline (pipeline_cache, group2, 0,
                                       srgb_srgb, bt2020_linear,
                                       srgb_srgb_to_bt2020_linear_2);
  assert_match_and_unref (clutter_pipeline_cache_get_pipeline (pipeline_cache,
                                                               group1, 0,
                                                               srgb_srgb,
                                                               bt2020_linear),
                          srgb_srgb_to_bt2020_linear);
  assert_match_and_unref (clutter_pipeline_cache_get_pipeline (pipeline_cache,
                                                               group2, 0,
                                                               srgb_srgb,
                                                               bt2020_linear),
                          srgb_srgb_to_bt2020_linear_2);
}

static void
pipeline_cache_replace_pipeline (void)
{
  ClutterContext *context = clutter_test_get_context ();
  ClutterBackend *backend = clutter_test_get_backend ();
  CoglContext *cogl_context = clutter_backend_get_cogl_context (backend);
  ClutterPipelineCache *pipeline_cache = clutter_context_get_pipeline_cache (context);
  static ClutterPipelineGroup group = &group;
  ClutterColorState *srgb_srgb;
  ClutterColorState *bt2020_linear;
  CoglPipeline *srgb_srgb_to_bt2020_linear;
  CoglPipeline *srgb_srgb_to_bt2020_linear_2;

  srgb_srgb = clutter_color_state_params_new (context,
                                              CLUTTER_COLORSPACE_SRGB,
                                              CLUTTER_TRANSFER_FUNCTION_SRGB);
  bt2020_linear = clutter_color_state_params_new (context,
                                                  CLUTTER_COLORSPACE_BT2020,
                                                  CLUTTER_TRANSFER_FUNCTION_PQ);

  srgb_srgb_to_bt2020_linear = create_test_pipeline (cogl_context,
                                                     "srgb_srgb_to_bt2020_linear");
  srgb_srgb_to_bt2020_linear_2 =
    create_test_pipeline (cogl_context, "srgb_srgb_to_bt2020_linear_2");

  g_object_add_weak_pointer (G_OBJECT (srgb_srgb_to_bt2020_linear),
                             (gpointer *) &srgb_srgb_to_bt2020_linear);

  clutter_color_state_add_pipeline_transform (srgb_srgb,
                                              bt2020_linear,
                                              srgb_srgb_to_bt2020_linear,
                                              0);

  clutter_pipeline_cache_set_pipeline (pipeline_cache, group, 0,
                                       srgb_srgb, bt2020_linear,
                                       srgb_srgb_to_bt2020_linear);

  g_object_unref (srgb_srgb_to_bt2020_linear);
  g_assert_nonnull (srgb_srgb_to_bt2020_linear);

  clutter_color_state_add_pipeline_transform (srgb_srgb,
                                              bt2020_linear,
                                              srgb_srgb_to_bt2020_linear_2,
                                              0);
  clutter_pipeline_cache_set_pipeline (pipeline_cache, group, 0,
                                       srgb_srgb, bt2020_linear,
                                       srgb_srgb_to_bt2020_linear_2);
  g_assert_null (srgb_srgb_to_bt2020_linear);

  assert_match_and_unref (clutter_pipeline_cache_get_pipeline (pipeline_cache,
                                                               group, 0,
                                                               srgb_srgb,
                                                               bt2020_linear),
                          srgb_srgb_to_bt2020_linear_2);
}

static void
pipeline_slots (void)
{
  ClutterContext *context = clutter_test_get_context ();
  ClutterBackend *backend = clutter_test_get_backend ();
  CoglContext *cogl_context = clutter_backend_get_cogl_context (backend);
  ClutterPipelineCache *pipeline_cache = clutter_context_get_pipeline_cache (context);
  static ClutterPipelineGroup group = &group;
  ClutterColorState *srgb_srgb;
  ClutterColorState *bt2020_linear;
  CoglPipeline *srgb_srgb_to_bt2020_linear;
  CoglPipeline *srgb_srgb_to_bt2020_linear_2;

  srgb_srgb = clutter_color_state_params_new (context,
                                              CLUTTER_COLORSPACE_SRGB,
                                              CLUTTER_TRANSFER_FUNCTION_SRGB);
  bt2020_linear = clutter_color_state_params_new (context,
                                                  CLUTTER_COLORSPACE_BT2020,
                                                  CLUTTER_TRANSFER_FUNCTION_PQ);

  srgb_srgb_to_bt2020_linear = create_test_pipeline (cogl_context,
                                                     "srgb_srgb_to_bt2020_linear");
  srgb_srgb_to_bt2020_linear_2 =
    create_test_pipeline (cogl_context, "srgb_srgb_to_bt2020_linear_2 ");

  clutter_pipeline_cache_set_pipeline (pipeline_cache, group, 0,
                                       srgb_srgb, bt2020_linear,
                                       srgb_srgb_to_bt2020_linear);
  clutter_pipeline_cache_set_pipeline (pipeline_cache, group, 1,
                                       srgb_srgb, bt2020_linear,
                                       srgb_srgb_to_bt2020_linear_2);

  assert_match_and_unref (clutter_pipeline_cache_get_pipeline (pipeline_cache,
                                                               group, 0,
                                                               srgb_srgb,
                                                               bt2020_linear),
                          srgb_srgb_to_bt2020_linear);
  assert_match_and_unref (clutter_pipeline_cache_get_pipeline (pipeline_cache,
                                                               group, 1,
                                                               srgb_srgb,
                                                               bt2020_linear),
                          srgb_srgb_to_bt2020_linear_2);
}

CLUTTER_TEST_SUITE (
  CLUTTER_TEST_UNIT ("/pipeline-cache/group-pipelines", pipeline_cache_group_pipelines)
  CLUTTER_TEST_UNIT ("/pipeline-cache/replace-pipeline", pipeline_cache_replace_pipeline)
  CLUTTER_TEST_UNIT ("/pipeline-cache/pipeline-slots", pipeline_slots)
)
