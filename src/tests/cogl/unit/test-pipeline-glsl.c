#include "config.h"

#include "cogl/cogl.h"
#include "tests/cogl-test-utils.h"

static void
test_pipeline_opengl_blend_enable (void)
{
  CoglPipeline *pipeline;
  CoglColor color;

  pipeline = cogl_pipeline_new (test_ctx);

  /* By default blending should be disabled */
  g_assert_cmpint (test_ctx->gl_blend_enable_cache, ==, 0);

  cogl_framebuffer_draw_rectangle (test_fb, pipeline, 0, 0, 1, 1);
  _cogl_framebuffer_flush_journal (test_fb);

  /* After drawing an opaque rectangle blending should still be
   * disabled */
  g_assert_cmpint (test_ctx->gl_blend_enable_cache, ==, 0);

  cogl_color_init_from_4f (&color, 0.0, 0.0, 0.0, 0.0);
  cogl_pipeline_set_color (pipeline, &color);
  cogl_framebuffer_draw_rectangle (test_fb, pipeline, 0, 0, 1, 1);
  _cogl_framebuffer_flush_journal (test_fb);

  /* After drawing a transparent rectangle blending should be enabled */
  g_assert_cmpint (test_ctx->gl_blend_enable_cache, ==, 1);

  cogl_pipeline_set_blend (pipeline, "RGBA=ADD(SRC_COLOR, 0)", NULL);
  cogl_framebuffer_draw_rectangle (test_fb, pipeline, 0, 0, 1, 1);
  _cogl_framebuffer_flush_journal (test_fb);

  /* After setting a blend string that effectively disables blending
   * then blending should be disabled */
  g_assert_cmpint (test_ctx->gl_blend_enable_cache, ==, 0);

  g_object_unref (pipeline);
}

COGL_TEST_SUITE (
  g_test_add_func ("/pipeline/opengl/blend-enable",
                   test_pipeline_opengl_blend_enable);
)
