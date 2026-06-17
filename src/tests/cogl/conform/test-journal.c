#include <cogl/cogl.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tests/cogl-test-utils.h"

static void
test_journal_unref_flush (void)
{
  CoglTexture *texture;
  CoglOffscreen *offscreen;
  CoglPipeline *pipeline;
  CoglColor color;
  const int width = 1;
  const int height = 1;
  const int stride = width * 4;
  uint8_t reference_data[] = {
    0x33, 0x33, 0x33, 0x33,
  };
  uint8_t data[G_N_ELEMENTS (reference_data)];

  G_STATIC_ASSERT (sizeof data == sizeof reference_data);

  texture = cogl_texture_2d_new_with_size (test_ctx, width, height);
  offscreen = cogl_offscreen_new_with_texture (texture);
  g_object_add_weak_pointer (G_OBJECT (offscreen), (gpointer *) &offscreen);

  pipeline = cogl_pipeline_new (test_ctx);
  cogl_color_init_from_4f (&color, 0.2f, 0.2f, 0.2f, 0.2f);
  cogl_pipeline_set_color (pipeline, &color);
  cogl_framebuffer_draw_rectangle (COGL_FRAMEBUFFER (offscreen),
                                   pipeline,
                                   -1, -1, 1, 1);
  g_object_unref (pipeline);

  g_object_unref (offscreen);
  g_assert_null (offscreen);

  cogl_texture_get_data (texture,
                         COGL_PIXEL_FORMAT_RGBA_8888_PRE,
                         stride, data);

  if (g_test_verbose () || cogl_test_verbose ())
    {
      g_printerr ("Texture data is ");
      for (int i = 0; i < G_N_ELEMENTS (data); ++i)
        {
          g_printerr ("0x%x, ", data[i]);
          if ((i + 1) % 4 == 0)
            g_printerr ("\n");
        }
      g_printerr ("\n");
    }

  g_assert_cmpmem (data, sizeof (data), reference_data, sizeof (reference_data));

  g_object_unref (texture);
}

COGL_TEST_SUITE (
  g_test_add_func ("/journal/unref-flush", test_journal_unref_flush);
)
