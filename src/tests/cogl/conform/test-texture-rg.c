#include <cogl/cogl.h>

#include <string.h>

#include "tests/cogl-test-utils.h"

#define TEX_WIDTH 8
#define TEX_HEIGHT 8

static CoglTexture *
make_texture (void)
{
  uint8_t tex_data[TEX_WIDTH * TEX_HEIGHT * 2], *p = tex_data;
  int x, y;

  for (y = 0; y < TEX_HEIGHT; y++)
    for (x = 0; x < TEX_WIDTH; x++)
      {
        *(p++) = x * 256 / TEX_WIDTH;
        *(p++) = y * 256 / TEX_HEIGHT;
      }

  return cogl_texture_2d_new_from_data (test_ctx,
                                        TEX_WIDTH, TEX_HEIGHT,
                                        COGL_PIXEL_FORMAT_RG_88,
                                        TEX_WIDTH * 2,
                                        tex_data,
                                        NULL);
}

static void
test_texture_rg (void)
{
  CoglPipeline *pipeline;
  CoglTexture *tex;
  int fb_width, fb_height;
  int x, y;

  if (!cogl_driver_has_feature (cogl_context_get_driver (test_ctx),
                                COGL_FEATURE_ID_TEXTURE_RG))
    {
      g_test_skip ("Missing TEXTURE_RG feature");
      return;
    }

  fb_width = cogl_framebuffer_get_width (test_fb);
  fb_height = cogl_framebuffer_get_height (test_fb);

  tex = make_texture ();

  g_assert_true (cogl_texture_get_components (tex) == COGL_TEXTURE_COMPONENTS_RG);

  pipeline = cogl_pipeline_new (test_ctx);

  cogl_pipeline_set_layer_texture (pipeline, 0, tex);
  cogl_pipeline_set_layer_filters (pipeline,
                                   0,
                                   COGL_PIPELINE_FILTER_NEAREST,
                                   COGL_PIPELINE_FILTER_NEAREST);

  cogl_framebuffer_draw_rectangle (test_fb,
                                   pipeline,
                                   -1.0f, 1.0f,
                                   1.0f, -1.0f);

  for (y = 0; y < TEX_HEIGHT; y++)
    for (x = 0; x < TEX_WIDTH; x++)
      {
        test_utils_check_pixel_rgb (test_fb,
                                    x * fb_width / TEX_WIDTH +
                                    fb_width / (TEX_WIDTH * 2),
                                    y * fb_height / TEX_HEIGHT +
                                    fb_height / (TEX_HEIGHT * 2),
                                    x * 256 / TEX_WIDTH,
                                    y * 256 / TEX_HEIGHT,
                                    0);
      }

  g_object_unref (pipeline);
  g_object_unref (tex);
}

COGL_TEST_SUITE (
  g_test_add_func ("/texture/rg", test_texture_rg);
)
