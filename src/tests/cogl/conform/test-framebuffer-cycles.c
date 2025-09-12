#include <cogl/cogl.h>

#include "tests/cogl-test-utils.h"

static void
test_framebuffer_cycles (void)
{
  g_autoptr (CoglTexture) tex1 = NULL;
  g_autoptr (CoglOffscreen) offscreen1 = NULL;
  g_autoptr (CoglPipeline) pipeline1 = NULL;
  g_autoptr (CoglTexture) tex2 = NULL;
  g_autoptr (CoglOffscreen) offscreen2 = NULL;
  g_autoptr (CoglPipeline) pipeline2 = NULL;

  tex1 = cogl_texture_2d_new_with_size (test_ctx, 100, 100);
  g_assert_nonnull (tex1);
  offscreen1 = cogl_offscreen_new_with_texture (tex1);
  g_assert_nonnull (offscreen1);

  tex2 = cogl_texture_2d_new_with_size (test_ctx, 100, 100);
  g_assert_nonnull (tex2);
  offscreen2 = cogl_offscreen_new_with_texture (tex2);
  g_assert_nonnull (offscreen2);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "_cogl_framebuffer_add_dependency: assertion '!find_cycle*");

  pipeline1 = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_layer_texture (pipeline1, 0, tex2);
  cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen1), pipeline1,
                                   -1, 1, 1, -1);

  pipeline2 = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_layer_texture (pipeline2, 0, tex1);
  cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen2), pipeline2,
                                   -1, 1, 1, -1);

  cogl_framebuffer_flush (COGL_FRAMEBUFFER (offscreen1));
  cogl_framebuffer_flush (COGL_FRAMEBUFFER (offscreen2));

  g_test_assert_expected_messages ();

  pipeline1 = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_layer_texture (pipeline1, 0, tex2);
  cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen1), pipeline1,
                                   -1, 1, 1, -1);
  cogl_framebuffer_flush (COGL_FRAMEBUFFER (offscreen1));

  pipeline2 = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_layer_texture (pipeline2, 0, tex1);
  cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen2), pipeline2,
                                   -1, 1, 1, -1);
  cogl_framebuffer_flush (COGL_FRAMEBUFFER (offscreen2));
}

COGL_TEST_SUITE (
  g_test_add_func ("/framebuffer/cycles", test_framebuffer_cycles);
)
