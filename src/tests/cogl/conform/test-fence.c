#include <cogl/cogl.h>

#include "tests/cogl-test-utils.h"

/* I'm writing this on the train after having dinner at a churrascuria. */
#define MAGIC_CHUNK_O_DATA ((void *) 0xdeadbeef)

static GMainLoop *loop;

static gboolean
timeout (void *user_data)
{
  g_assert (!"timeout not reached");

  return FALSE;
}

static void
callback (CoglFence *fence,
          void *user_data)
{
  int fb_width = cogl_framebuffer_get_width (test_fb);
  int fb_height = cogl_framebuffer_get_height (test_fb);

  test_utils_check_pixel (test_fb, fb_width - 1, fb_height - 1, 0x00ff0000);
  g_assert (user_data == MAGIC_CHUNK_O_DATA && "callback data not mangled");

  g_main_loop_quit (loop);
}

static void
test_fence (void)
{
  GSource *cogl_source;
  int fb_width = cogl_framebuffer_get_width (test_fb);
  int fb_height = cogl_framebuffer_get_height (test_fb);
  CoglFenceClosure *closure;

  if (!cogl_has_feature (test_ctx, COGL_FEATURE_ID_FENCE))
    {
      g_test_skip ("Missing fence support");
      return;
    }

  cogl_source = cogl_glib_source_new (test_ctx, G_PRIORITY_DEFAULT);
  g_source_attach (cogl_source, NULL);
  loop = g_main_loop_new (NULL, TRUE);

  cogl_framebuffer_orthographic (test_fb, 0, 0, fb_width, fb_height, -1, 100);
  cogl_framebuffer_clear4f (test_fb, COGL_BUFFER_BIT_COLOR,
                            0.0f, 1.0f, 0.0f, 0.0f);

  closure = cogl_framebuffer_add_fence_callback (test_fb,
                                                 callback,
                                                 MAGIC_CHUNK_O_DATA);
  g_assert (closure != NULL);

  g_timeout_add_seconds (5, timeout, NULL);

  g_main_loop_run (loop);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

COGL_TEST_SUITE (
  g_test_add_func ("/fence", test_fence);
)
