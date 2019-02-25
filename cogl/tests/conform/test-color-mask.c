#include <cogl/cogl.h>

#include "test-declarations.h"
#include "test-utils.h"

#define TEX_SIZE 128

#define NUM_FBOS 3

typedef struct _TestState
{
  int width;
  int height;

  CoglTexture *tex[NUM_FBOS];
  CoglFramebuffer *fbo[NUM_FBOS];
} TestState;

static void
paint (TestState *state)
{
  CoglPipeline *pipeline;
  CoglColor bg;
  int i;

  pipeline = cogl_pipeline_new (test_ctx);
  cogl_pipeline_set_color4ub (pipeline, 255, 255, 255, 255);

  cogl_framebuffer_draw_rectangle (state->fbo[0], pipeline, -1.0, -1.0, 1.0, 1.0);
  cogl_framebuffer_draw_rectangle (state->fbo[1], pipeline, -1.0, -1.0, 1.0, 1.0);
  cogl_framebuffer_draw_rectangle (state->fbo[2], pipeline, -1.0, -1.0, 1.0, 1.0);

  cogl_color_init_from_4ub (&bg, 128, 128, 128, 255);
  cogl_framebuffer_clear (test_fb, COGL_BUFFER_BIT_COLOR | COGL_BUFFER_BIT_DEPTH, &bg);

  cogl_object_unref (pipeline);

  /* Render all of the textures to the screen */
  for (i = 0; i < NUM_FBOS; i++)
    {
      pipeline = cogl_pipeline_new (test_ctx);
      cogl_pipeline_set_layer_texture (pipeline, 0, state->tex[i]);
      cogl_framebuffer_draw_rectangle (test_fb, pipeline,
                                       2.0f / NUM_FBOS * i - 1.0f, -1.0f,
                                       2.0f / NUM_FBOS * (i + 1) - 1.0f, 1.0f);
      cogl_object_unref (pipeline);
    }

  /* Verify all of the fbos drew the right color */
  for (i = 0; i < NUM_FBOS; i++)
    {
      uint8_t expected_colors[NUM_FBOS][4] =
        { { 0xff, 0x00, 0x00, 0xff },
          { 0x00, 0xff, 0x00, 0xff },
          { 0x00, 0x00, 0xff, 0xff } };

      test_utils_check_pixel_rgb (test_fb,
                                  state->width * (i + 0.5f) / NUM_FBOS,
                                  state->height / 2,
                                  expected_colors[i][0],
                                  expected_colors[i][1],
                                  expected_colors[i][2]);
    }
}

void
test_color_mask (void)
{
  TestState state;
  int i;

  state.width = cogl_framebuffer_get_width (test_fb);
  state.height = cogl_framebuffer_get_height (test_fb);

  for (i = 0; i < NUM_FBOS; i++)
    {
      state.tex[i] = test_utils_texture_new_with_size (test_ctx, 128, 128,
                                                 TEST_UTILS_TEXTURE_NO_ATLAS,
                                                 COGL_TEXTURE_COMPONENTS_RGB);


      state.fbo[i] = cogl_offscreen_new_with_texture (state.tex[i]);

      /* Clear the texture color bits */
      cogl_framebuffer_clear4f (state.fbo[i],
                                COGL_BUFFER_BIT_COLOR, 0, 0, 0, 1);

      cogl_framebuffer_set_color_mask (state.fbo[i],
                                       i == 0 ? COGL_COLOR_MASK_RED :
                                       i == 1 ? COGL_COLOR_MASK_GREEN :
                                       COGL_COLOR_MASK_BLUE);
    }

  paint (&state);

  if (cogl_test_verbose ())
    g_print ("OK\n");
}

