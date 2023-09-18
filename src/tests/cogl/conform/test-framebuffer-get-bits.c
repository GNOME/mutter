#include <cogl/cogl.h>

#include "tests/cogl-test-utils.h"

static void
test_framebuffer_get_bits (void)
{
  CoglRenderer *renderer;
  CoglTexture *tex_a;
  CoglOffscreen *offscreen_a;
  CoglFramebuffer *fb_a;
  CoglTexture *tex_rgba;
  CoglOffscreen *offscreen_rgba;
  CoglFramebuffer *fb_rgba;

  renderer = cogl_context_get_renderer (test_ctx);

  if (cogl_renderer_get_driver (renderer) != COGL_DRIVER_GL3)
    {
      g_test_skip ("Test requires OpenGL");
      return;
    }

  tex_a = cogl_texture_2d_new_with_size (test_ctx, 16, 16);
  offscreen_a = cogl_offscreen_new_with_texture (tex_a);
  fb_a = COGL_FRAMEBUFFER (offscreen_a);
  tex_rgba = cogl_texture_2d_new_with_size (test_ctx, 16, 16);
  offscreen_rgba = cogl_offscreen_new_with_texture (tex_rgba);
  fb_rgba = COGL_FRAMEBUFFER (offscreen_rgba);

  cogl_texture_set_components (tex_a,
                               COGL_TEXTURE_COMPONENTS_A);
  cogl_framebuffer_allocate (fb_a, NULL);
  cogl_framebuffer_allocate (fb_rgba, NULL);

  g_assert_cmpint (cogl_framebuffer_get_red_bits (fb_a), ==, 0);
  g_assert_cmpint (cogl_framebuffer_get_green_bits (fb_a), ==, 0);
  g_assert_cmpint (cogl_framebuffer_get_blue_bits (fb_a), ==, 0);
  g_assert_cmpint (cogl_framebuffer_get_alpha_bits (fb_a), >=, 1);

  g_assert_cmpint (cogl_framebuffer_get_red_bits (fb_rgba), >=, 1);
  g_assert_cmpint (cogl_framebuffer_get_green_bits (fb_rgba), >=, 1);
  g_assert_cmpint (cogl_framebuffer_get_blue_bits (fb_rgba), >=, 1);
  g_assert_cmpint (cogl_framebuffer_get_alpha_bits (fb_rgba), >=, 1);

  g_object_unref (fb_rgba);
  g_object_unref (tex_rgba);
  g_object_unref (fb_a);
  g_object_unref (tex_a);
}

COGL_TEST_SUITE (
  g_test_add_func ("/framebuffer/get-bits", test_framebuffer_get_bits);
)
