#include "config.h"

#include "cogl/cogl.h"
#include "cogl/driver/gl/cogl-pipeline-vertend-glsl-private.h"
#include "tests/cogl-test-utils.h"

static void
test_pipeline_vertend_glsl_point_size_shader (void)
{
  CoglPipeline *pipelines[4];
  CoglPipelineVertendShaderState *shader_states[G_N_ELEMENTS (pipelines)];
  int i;

  /* Default pipeline with zero point size */
  pipelines[0] = cogl_pipeline_new (test_ctx);

  /* Point size 1 */
  pipelines[1] = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_point_size (pipelines[1], 1.0f);

  /* Point size 2 */
  pipelines[2] = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_point_size (pipelines[2], 2.0f);

  /* Same as the first pipeline, but reached by restoring the old
   * state from a copy */
  pipelines[3] = cogl_pipeline_copy (pipelines[1]);
  cogl_pipeline_set_point_size (pipelines[3], 0.0f);

  /* Draw something with all of the pipelines to make sure their state
   * is flushed */
  for (i = 0; i < G_N_ELEMENTS (pipelines); i++)
    {
      cogl_framebuffer_draw_rectangle (test_fb,
                                       pipelines[i],
                                       0.0f, 0.0f,
                                       10.0f, 10.0f);
    }
  cogl_framebuffer_finish (test_fb);

  /* Get all of the shader states. These might be NULL if the driver
   * is not using GLSL */
  for (i = 0; i < G_N_ELEMENTS (pipelines); i++)
    {
      shader_states[i] =
        cogl_pipeline_vertend_glsl_get_shader_state (pipelines[i]);
    }

  /* If the first two pipelines are using GLSL then they should have
   * the same shader unless there is no builtin uniform for the point
   * size */
  if (shader_states[0])
    g_assert (shader_states[0] != shader_states[1]);

  /* The second and third pipelines should always have the same shader
   * state because only toggling between zero and non-zero should
   * change the shader */
  g_assert (shader_states[1] == shader_states[2]);

  /* The fourth pipeline should be exactly the same as the first */
  g_assert (shader_states[0] == shader_states[3]);

  for (i = 0; i < G_N_ELEMENTS (pipelines); i++)
    g_object_unref (pipelines[i]);
}

COGL_TEST_SUITE (
  g_test_add_func ("/pipeline/vertend/glsl/point-size-shader",
                   test_pipeline_vertend_glsl_point_size_shader);
)
