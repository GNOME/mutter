#include "config.h"

#include "cogl/cogl.h"
#include "cogl/cogl-pipeline-state.h"
#include "tests/cogl-test-utils.h"

static void
test_pipeline_state_uniform_ancestry (void)
{
  CoglPipeline *pipeline;
  CoglNode *node;
  int pipeline_length = 0;
  int i;

  pipeline = cogl_pipeline_new (test_ctx);

  /* Repeatedly making a copy of a pipeline and changing a uniform
   * shouldn't cause a long chain of pipelines to be created */

  for (i = 0; i < 20; i++)
    {
      CoglPipeline *tmp_pipeline;
      int uniform_location;

      tmp_pipeline = cogl_pipeline_copy (pipeline);
      g_object_unref (pipeline);
      pipeline = tmp_pipeline;

      uniform_location =
        cogl_pipeline_get_uniform_location (pipeline, "a_uniform");

      cogl_pipeline_set_uniform_1i (pipeline, uniform_location, i);
    }

  for (node = (CoglNode *) pipeline; node; node = node->parent)
    pipeline_length++;

  g_assert_cmpint (pipeline_length, <=, 2);

  g_object_unref (pipeline);
}

COGL_TEST_SUITE (
  g_test_add_func ("/pipeline-state/uniform-ancestry", test_pipeline_state_uniform_ancestry);
)
