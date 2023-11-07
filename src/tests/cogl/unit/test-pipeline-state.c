#include "config.h"

#include "cogl/cogl.h"
#include "cogl/cogl-pipeline-state.h"
#include "tests/cogl-test-utils.h"

static void
test_pipeline_state_blend_constant_ancestry (void)
{
  CoglPipeline *pipeline;
  CoglNode *node;
  int pipeline_length = 0;
  int i;

  /* Repeatedly making a copy of a pipeline and changing the same
   * state (in this case the blend constant) shouldn't cause a long
   * chain of pipelines to be created because the redundant ancestry
   * should be pruned. */

  pipeline = cogl_pipeline_new (test_ctx);

  for (i = 0; i < 20; i++)
    {
      CoglColor color;
      CoglPipeline *tmp_pipeline;

      cogl_color_init_from_4f (&color, i / 20.0f, 0.0f, 0.0f, 1.0f);

      tmp_pipeline = cogl_pipeline_copy (pipeline);
      g_object_unref (pipeline);
      pipeline = tmp_pipeline;

      cogl_pipeline_set_blend_constant (pipeline, &color);
    }

  for (node = (CoglNode *) pipeline; node; node = node->parent)
    pipeline_length++;

  g_assert_cmpint (pipeline_length, <=, 2);

  g_object_unref (pipeline);
}

COGL_TEST_SUITE (
  g_test_add_func ("/pipeline-state/blend-constant-ancestry",
                   test_pipeline_state_blend_constant_ancestry);
)
