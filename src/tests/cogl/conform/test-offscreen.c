#define COGL_VERSION_MIN_REQUIRED COGL_VERSION_1_0

#include <cogl/cogl.h>

#include "tests/cogl-test-utils.h"

#define RED 0
#define GREEN 1
#define BLUE 2

typedef struct _TestState
{
  int fb_width;
  int fb_height;
} TestState;

static void
check_quadrant (TestState *state,
                int qx,
                int qy,
                uint32_t expected_rgba)
{
  /* The quadrants are all stuffed into the top right corner of the
     framebuffer */
  int x = state->fb_width * qx / 4 + state->fb_width / 2;
  int y = state->fb_height * qy / 4;
  int width = state->fb_width / 4;
  int height = state->fb_height / 4;

  /* Subtract a two-pixel gap around the edges to allow some rounding
     differences */
  x += 2;
  y += 2;
  width -= 4;
  height -= 4;

  test_utils_check_region (test_fb, x, y, width, height, expected_rgba);
}

static void
test_paint (TestState *state)
{
  CoglTexture *tex;
  CoglOffscreen *offscreen;
  CoglFramebuffer *framebuffer;
  CoglPipeline *opaque_pipeline;
  CoglPipeline *texture_pipeline;
  CoglColor color;

  tex = cogl_texture_2d_new_with_size (test_ctx,
                                       state->fb_width,
                                       state->fb_height);

  offscreen = cogl_offscreen_new_with_texture (tex);
  framebuffer = COGL_FRAMEBUFFER (offscreen);

  /* Set a scale and translate transform on the window framebuffer
   * before switching to the offscreen framebuffer so we can verify it
   * gets restored when we switch back
   *
   * The test is going to draw a grid of 4 colors to a texture which
   * we subsequently draw to the window with a fullscreen rectangle.
   * This transform will flip the texture left to right, scale it to a
   * quarter of the window size and slide it to the top right of the
   * window.
   */
  cogl_framebuffer_push_matrix (test_fb);
  cogl_framebuffer_translate (test_fb, 0.5, 0.5, 0);
  cogl_framebuffer_scale (test_fb, -0.5, 0.5, 1);

  /* Setup something other than the identity matrix for the modelview so we can
   * verify it gets restored when we call cogl_pop_framebuffer () */
  cogl_framebuffer_scale (test_fb, 2, 2, 1);


  opaque_pipeline = cogl_pipeline_new (test_ctx);
  /* red, top left */
  cogl_color_init_from_4f (&color, 1.0, 0.0, 0.0, 1.0);
  cogl_pipeline_set_color (opaque_pipeline, &color);
  cogl_framebuffer_draw_rectangle (framebuffer, opaque_pipeline,
                                   -0.5, 0.5, 0, 0);
  /* green, top right */
  /* red, top left */
  cogl_color_init_from_4f (&color, 0.0, 1.0, 0.0, 1.0);
  cogl_pipeline_set_color (opaque_pipeline, &color);
  cogl_framebuffer_draw_rectangle (framebuffer, opaque_pipeline,
                                   0, 0.5, 0.5, 0);
  /* blue, bottom left */
  cogl_color_init_from_4f (&color, 0.0, 0.0, 1.0, 1.0);
  cogl_pipeline_set_color (opaque_pipeline, &color);
  cogl_framebuffer_draw_rectangle (framebuffer, opaque_pipeline,
                                   -0.5, 0, 0, -0.5);
  /* white, bottom right */
  cogl_color_init_from_4f (&color, 1.0, 1.0, 1.0, 1.0);
  cogl_pipeline_set_color (opaque_pipeline, &color);
  cogl_framebuffer_draw_rectangle (framebuffer, opaque_pipeline,
                                   0, 0, 0.5, -0.5);

  /* Cogl should release the last reference when we call cogl_pop_framebuffer()
   */
  g_object_unref (offscreen);

  texture_pipeline = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_layer_texture (texture_pipeline, 0, tex);
  cogl_framebuffer_draw_rectangle (test_fb, texture_pipeline, -1, 1, 1, -1);

  g_object_unref (opaque_pipeline);
  g_object_unref (texture_pipeline);
  g_object_unref (tex);

  cogl_framebuffer_pop_matrix (test_fb);

  /* NB: The texture is drawn flipped horizontally and scaled to fit in the
   * top right corner of the window. */

  /* red, top right */
  check_quadrant (state, 1, 0, 0xff0000ff);
  /* green, top left */
  check_quadrant (state, 0, 0, 0x00ff00ff);
  /* blue, bottom right */
  check_quadrant (state, 1, 1, 0x0000ffff);
  /* white, bottom left */
  check_quadrant (state, 0, 1, 0xffffffff);
}

static void
test_flush (TestState *state)
{
  CoglPipeline *pipeline;
  CoglTexture *tex;
  CoglOffscreen *offscreen;
  CoglFramebuffer *framebuffer;
  CoglColor clear_color, pipeline_color;
  int i;

  pipeline = cogl_pipeline_new (test_ctx);
  cogl_color_init_from_4f (&pipeline_color, 1.0, 0.0, 0.0, 1.0);
  cogl_pipeline_set_color (pipeline, &pipeline_color);

  for (i = 0; i < 3; i++)
    {
      /* This tests that rendering to a framebuffer and then reading back
         the contents of the texture will automatically flush the
         journal */

      tex = cogl_texture_2d_new_with_size (test_ctx,
                                           16, 16); /* width/height */

      offscreen = cogl_offscreen_new_with_texture (tex);
      framebuffer = COGL_FRAMEBUFFER (offscreen);

      cogl_color_init_from_4f (&clear_color, 0.0, 0.0, 0.0, 1.0);
      cogl_framebuffer_clear (framebuffer, COGL_BUFFER_BIT_COLOR, &clear_color);

      cogl_framebuffer_draw_rectangle (framebuffer, pipeline, -1, -1, 1, 1);

      if (i == 0)
        /* First time check using read pixels on the offscreen */
        test_utils_check_region (framebuffer,
                                 1, 1, 15, 15, 0xff0000ff);
      else if (i == 1)
        {
          uint8_t data[16 * 4 * 16];
          int x, y;

          /* Second time try reading back the texture contents */
          cogl_texture_get_data (tex,
                                 COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                                 16 * 4, /* rowstride */
                                 data);

          for (y = 1; y < 15; y++)
            for (x = 1; x < 15; x++)
              test_utils_compare_pixel (data + x * 4 + y * 16 * 4,
                                        0xff0000ff);
        }

      if (i == 2)
        {
          /* Third time try drawing the texture to the screen */
          cogl_framebuffer_draw_rectangle (test_fb, pipeline,
                                           -1, -1, 1, 1);
          test_utils_check_region (test_fb,
                                   2, 2, /* x/y */
                                   state->fb_width - 4,
                                   state->fb_height - 4,
                                   0xff0000ff);
        }

      g_object_unref (tex);
      g_object_unref (offscreen);
    }

  g_object_unref (pipeline);
}

static void
test_offscreen (void)
{
  TestState state;

  state.fb_width = cogl_framebuffer_get_width (test_fb);
  state.fb_height = cogl_framebuffer_get_height (test_fb);

  test_paint (&state);
  test_flush (&state);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

COGL_TEST_SUITE (
  g_test_add_func ("/offscreen", test_offscreen);
)
